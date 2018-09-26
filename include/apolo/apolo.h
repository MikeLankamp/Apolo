#pragma once

#include <lua/lua.hpp>

#include <cassert>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <string>
#include <stdexcept>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace apolo
{

// Base class for all apolo exceptions
class exception : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// A script has bad syntax
class syntax_error : public exception
{
public:
  using exception::exception;
};

// Indicates a runtime error has occurred
class runtime_error : public exception
{
public:
  using exception::exception;
};

namespace detail
{
    // Helper types for overloading on type for std::visit
    template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
    template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

    // function_traits: helper types to get argument and return type for lambdas
    template <typename T, typename Enable = void>
    struct function_traits;

    // For generic types, directly use the result of the signature of its 'operator()', if it exists
    template <typename T>
    struct function_traits<T, typename std::enable_if_t<std::is_class_v<T>>> : function_traits<decltype(&T::operator())>
    {};

    // Specialize for pointers to member function
    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType(ClassType::*)(Args...), void>
    {
        using return_type = ReturnType;
        using signature = ReturnType(Args...);
    };

    // Specialize for pointers to const-member function
    template <typename ClassType, typename ReturnType, typename... Args>
    struct function_traits<ReturnType(ClassType::*)(Args...) const, void>
        : function_traits<ReturnType(ClassType::*)(Args...), void>
    {};

    // Specialize for function pointers
    template <typename ReturnType, typename... Args>
    struct function_traits<ReturnType (*)(Args...), void>
    {
        using return_type = ReturnType;
        using signature = ReturnType(Args...);
    };


    template <typename ObjectType>
    const std::string& metatable_name()
    {
        static const auto metatableName = std::string("ObjectType:") + typeid(ObjectType).name();
        return metatableName;
    }


    template <typename T, std::enable_if_t<std::is_integral_v<T>, void*> = nullptr>
    void push_value(lua_State& state, T value)
    {
        lua_pushinteger(&state, static_cast<lua_Integer>(value));
    }

    template <typename T, std::enable_if_t<std::is_floating_point_v<T>, void*> = nullptr>
    void push_value(lua_State& state, T value)
    {
        lua_pushnumber(&state, static_cast<lua_Number>(value));
    }

    inline void push_value(lua_State& state, bool value)
    {
        lua_pushboolean(&state, value);
    }

    inline void push_value(lua_State& state, const std::string& value)
    {
        lua_pushstring(&state, value.c_str());
    }

    inline void push_value(lua_State& state, const char* value)
    {
        lua_pushstring(&state, value);
    }


    struct lua_state_delete
    {
        void operator()(lua_State* state)
        {
            lua_close(state);
        }
    };

    using lua_state_ptr = std::unique_ptr<lua_State, lua_state_delete>;

    // Moveable type for holding Lua references from native code
    class lua_ref
    {
    public:
        lua_ref() : m_state(nullptr), m_ref(LUA_REFNIL) {}

        // Non-copyable
        lua_ref(const lua_ref&) = delete;
        lua_ref& operator=(const lua_ref&) = delete;

        lua_ref(lua_ref&& ref)
          : m_state(ref.m_state)
          , m_ref(ref.m_ref)
        {
            ref.release();
        }

        lua_ref& operator=(lua_ref&& ref)
        {
            release();
            m_state = ref.m_state;
            m_ref = ref.m_ref;
            ref.release();
            return *this;
        }

        static lua_ref pop_from_stack(lua_State& state)
        {
            return lua_ref(state, luaL_ref(&state, LUA_REGISTRYINDEX));
        }

        ~lua_ref()
        {
            release();
        }

    private:
        void release()
        {
            if (m_state != nullptr)
            {
                luaL_unref(m_state, LUA_REGISTRYINDEX, m_ref);
                m_state = nullptr;
                m_ref = LUA_REFNIL;
            }
        }

        lua_ref(lua_State& state, int ref)
            : m_state(&state)
            , m_ref(ref)
        {
        }

        lua_State* m_state;
        int m_ref;
    };
}

// Variant-like type for Lua-compatible values
class value
{
public:
    // Construct an empty value
    value() : m_storage(nullptr) { }

    // Construct an empty value
    value(std::nullptr_t) : m_storage(nullptr) {}

    // Construct a value from an integer value
    template <typename T, typename std::enable_if_t<std::is_integral_v<T>,int*> = nullptr>
    value(T val) : m_storage(static_cast<long long>(val)) {}

    // Construct a value from a floating-point value
    template <typename T, typename std::enable_if_t<std::is_floating_point_v<T>,int*> = nullptr>
    value(T val) : m_storage(static_cast<double>(val)) {}

    // Construct a value from a boolean
    value(bool val) : m_storage(val) {}

    // Construct a value from a string
    value(std::string val) : m_storage(std::move(val)) {}

    // Construct a value from a raw string
    value(const char* val) : m_storage(std::string(val)) {}

    // Construct a value from a shared pointer to an object
    template <typename T, class = std::enable_if_t<std::is_class_v<T>, void>>
    value(std::shared_ptr<T> val)
        : m_storage(object_info{typeid(T), reinterpret_cast<std::uintptr_t>(val.get())})
    {}

    template <typename T>
    const T& as() const
    {
        return std::get<T>(m_storage);
    }

    // Apply a visitor to this value. The visitor can have overloads for each of the supported
    // types.
    template <typename Visitor>
    void visit(Visitor visitor) const
    {
        std::visit(detail::overloaded{
            [&](const object_info& x) { visitor(x.m_type, x.m_address); },
            [&](const auto& x) { visitor(x); },
        }, m_storage);
    }

    bool operator==(const value& other) const { return m_storage == other.m_storage; }
    bool operator!=(const value& other) const { return m_storage != other.m_storage; }

private:
    struct object_info
    {
        std::type_index m_type;
        std::uintptr_t m_address;

        bool operator==(const object_info& other) const { return std::tie(m_type, m_address) == std::tie(other.m_type, other.m_address); }
        bool operator!=(const object_info& other) const { return std::tie(m_type, m_address) != std::tie(other.m_type, other.m_address); }
    };

    std::variant<std::nullptr_t, bool, long long, double, std::string, object_info> m_storage;
};

namespace detail
{
    value read_value(lua_State& state, int index);
    long long read_integer(lua_State& state, int index);
    double read_double(lua_State& state, int index);
    std::string read_string(lua_State& state, int index);

    template <typename T>
    static std::enable_if_t<std::is_floating_point_v<T>, T> read_value(lua_State& state, int index, T*)
    {
        return static_cast<T>(detail::read_double(state, index));
    };

    template <typename T>
    inline std::enable_if_t<std::is_integral_v<T>, T> read_value(lua_State& state, int index, T*)
    {
        return static_cast<T>(detail::read_integer(state, index));
    };

    inline std::string read_value(lua_State& state, int index, std::string*)
    {
        return detail::read_string(state, index);
    };

    template <int Index>
    static auto read_arguments(lua_State& state)
    {
        if (lua_gettop(&state) + 1 != Index)
        {
            throw runtime_error("Insufficient arguments to function");
        }
        return std::make_tuple();
    }

    template <int Index, typename Head, typename... Args>
    static auto read_arguments(std::enable_if_t<std::is_same_v<std::decay_t<Head>, std::vector<value>>, lua_State&> state)
    {
        static_assert(sizeof...(Args) == 0, "std::vector<value> must be last argument in function");
        std::vector<value> values;
        int maxArg = lua_gettop(&state);
        for (int i = Index; i <= maxArg; ++i)
        {
            values.push_back(detail::read_value(state, i));
        }
        return std::make_tuple(values);
    }

    template <int Index, typename Head, typename... Args>
    static auto read_arguments(std::enable_if_t<!std::is_same_v<std::decay_t<Head>, std::vector<value>>, lua_State&> state)
    {
        auto arg = detail::read_value(state, Index, std::add_pointer_t<Head>{});
        return std::tuple_cat(std::make_tuple(arg), read_arguments<Index + 1, Args...>(state));
    }


    class lua_callback
    {
    public:
        virtual ~lua_callback() = default;
        virtual int invoke(lua_State& state) const = 0;

    protected:
        template <typename Callable, typename ArgsTuple>
        int invoke(lua_State& state, Callable &&callable, ArgsTuple&& argsTuple) const
        {
            return push_value(state, [&](){
                return std::apply(std::forward<Callable>(callable), std::forward<ArgsTuple>(argsTuple));
            });
        }

    private:
        template <typename Callable>
        static std::enable_if_t<std::is_void_v<std::result_of_t<Callable()>>, int>
        push_value(lua_State&, Callable&& callable)
        {
            callable();
            return 0;
        }

        template <typename Callable>
        static std::enable_if_t<!std::is_void_v<std::result_of_t<Callable()>>, int>
        push_value(lua_State& state, Callable&& callable)
        {
            push_value(state, callable());
            return 1;
        }
    };

    template <typename R, typename... Args>
    class simple_lua_callback: public detail::lua_callback
    {
    public:
        simple_lua_callback(std::function<R(Args...)> callable)
            : m_callable(std::move(callable))
        {
        }

        int invoke(lua_State& state) const override
        {
            auto args = read_arguments<1, std::decay_t<Args>...>(state);
            return detail::lua_callback::invoke(state, m_callable, args);
        }

    private:
        std::function<R(Args...)> m_callable;
    };

    template <typename ObjectType>
    class object_lua_callback : public detail::lua_callback
    {
    public:
        int invoke(lua_State& state) const override
        {
            // Check that the first argument is a native object reference
            auto* ref = static_cast<std::shared_ptr<ObjectType>*>(luaL_testudata(&state, 1, detail::metatable_name<ObjectType>().c_str()));
            if (ref == nullptr)
            {
                throw runtime_error("Wrong arguments to function");
            }
            assert(*ref != nullptr);
            return invoke(**ref, state);
        }

        virtual int invoke(ObjectType& object, lua_State& state) const = 0;
    };

    template <typename ObjectType, typename Callable, typename... Args>
    class unbound_lua_callback : public object_lua_callback<ObjectType>
    {
    public:
        unbound_lua_callback(Callable callable)
            : m_callable(callable)
        {
        }

        int invoke(ObjectType& object, lua_State& state) const override
        {
            // Read the arguments from Lua and call the callback
            auto args = read_arguments<2, std::decay_t<Args>...>(state);
            auto applyArgs = std::tuple_cat(std::tie(object), args);
            return detail::lua_callback::invoke(state, m_callable, applyArgs);
        }

    private:
        Callable m_callable;
    };

    template <typename DerivedType, typename BaseType>
    class object_cast_lua_callback : public detail::object_lua_callback<DerivedType>
    {
    public:
        object_cast_lua_callback(const object_lua_callback<BaseType>& callback)
            : m_callback(callback)
        {
        }

        int invoke(DerivedType& object, lua_State& state) const override
        {
            return m_callback.invoke(object, state);
        }

    private:
        const object_lua_callback<BaseType>& m_callback;
    };
}

//
// Registry for method and class information
//
// Construct a registry, register functions and classes and pass it in to \ref script instances to allow
// the registered methods and classes to be used in those script instances.
//
class type_registry
{
public:
    class object_type_info_base
    {
    public:
        virtual ~object_type_info_base() = default;

        const auto& methods() const
        {
            return m_methods;
        }

    protected:
        void register_method(std::string name, std::unique_ptr<detail::lua_callback> callback)
        {
            bool success = m_methods.emplace(std::move(name), std::move(callback)).second;
            assert(success);
            (void)success;
        }

    private:
        // The methods in the object type
        std::unordered_map<std::string, std::unique_ptr<detail::lua_callback>> m_methods;
    };

    template <typename ObjectType>
    class object_type_info : public object_type_info_base
    {
        static_assert(std::is_class_v<ObjectType>);

    public:
        template <typename R, typename... Args>
        object_type_info& WithMethod(std::string name, R(ObjectType::*callable)(Args...))
        {
            register_method(std::move(name),
                std::make_unique<detail::unbound_lua_callback<ObjectType, decltype(callable), Args...>>(callable));
            return *this;
        }

        template <typename R, typename... Args>
        object_type_info& WithMethod(std::string name, R(ObjectType::*callable)(Args...) const)
        {
            register_method(std::move(name),
                std::make_unique<detail::unbound_lua_callback<ObjectType, decltype(callable), Args...>>(callable));
            return *this;
        }

        template <typename BaseType>
        object_type_info& WithBase()
        {
            static_assert(std::is_base_of_v<BaseType, ObjectType>);

            const auto* info = m_registry.get_object_type(typeid(BaseType));
            assert(info != nullptr);
            for (const auto& [name, method] : info->methods())
            {
                const auto& base_method = static_cast<const detail::object_lua_callback<BaseType>&>(*method);
                register_method(name,
                  std::make_unique<detail::object_cast_lua_callback<ObjectType,BaseType>>(base_method));
            }

            return *this;
        }

        object_type_info(const type_registry& registry)
            : m_registry(registry)
        {
        }

    private:
        const type_registry& m_registry;
    };

    //
    // Adds a generic callable as global function
    // \param[in] name the name to register the function as
    // \param[in] callable the callable object to register as function
    //
    template <typename Callable>
    void add_free_function(std::string name, Callable&& callable)
    {
        using Signature = typename detail::function_traits<Callable>::signature;
        add_free_function(std::move(name), std::function<Signature>(std::forward<Callable>(callable)));
    }

    //
    // Adds a member function on a specific object as a global function
    // \param[in] name the name to register the function as
    // \param[in] object the instance of the object that the member function will be called on
    // \param[in] callable the member function to register as function
    //
    template <typename ObjectType, typename R, typename... Args>
    void add_free_function(std::string name, ObjectType& object, R (ObjectType::*callable)(Args...))
    {
        // Use a lambda to bind the member function pointer to the object
        add_free_function(std::move(name), std::function<R(Args...)>([&object,callable](Args&&... args) {
            return (object.*callable)(std::forward<Args>(args)...);
        }));
    }

    //
    // Adds a const-member function on a specific object as a global function
    // \param[in] name the name to register the function as
    // \param[in] object the instance of the object that the member function will be called on
    // \param[in] callable the const member function to register as function
    //
    template <typename ObjectType, typename R, typename... Args>
    void add_free_function(std::string name, const ObjectType& object, R (ObjectType::*callable)(Args...) const)
    {
        // Use a lambda to bind the member function pointer to the object
        add_free_function(std::move(name), std::function<R(Args...)>([&object,callable](Args&&... args) {
            return (object.*callable)(std::forward<Args...>(args)...);
        }));
    }

    //
    // Returns a reference to the the registered free functions
    //
    const auto& free_functions() const
    {
        return m_free_functions;
    }

    //
    // Registers an object type for use in scripts
    // Native C++ objects can be passed along to script method calls only after
    // registering them via this function.
    //
    template <typename ObjectType>
    object_type_info<ObjectType>& add_object_type()
    {
        // Check the type hasn't been registered yet
        auto index = std::type_index{typeid(ObjectType)};
        assert(m_object_types.find(index) == m_object_types.end());

        auto info = std::make_unique<object_type_info<ObjectType>>(*this);
        auto& raw_info = *info;
        m_object_types.emplace(index, std::move(info));
        return raw_info;
    }

    //
    // Finds a registered object type info based on the type.
    //
    const object_type_info_base* get_object_type(std::type_index typeIndex) const;

private:
    // Adds a std::function as global function
    template <typename R, typename... Args>
    void add_free_function(std::string name, std::function<R(Args...)> callable)
    {
        assert(m_free_functions.find(name) == m_free_functions.end());
        m_free_functions.emplace(std::move(name),
        std::make_unique<detail::simple_lua_callback<R, Args...>>(std::move(callable)));
    }

    std::unordered_map<std::string, std::unique_ptr<detail::lua_callback>> m_free_functions;
    std::unordered_map<std::type_index, std::unique_ptr<object_type_info_base>> m_object_types;
};

class thread
{
public:
    // The status of the thread
    enum class status
    {
        yielded,
        finished,
    };

    // Creates a thread from the specified state and calls the function at the top of the stack.
    // It expects the top of the stack contains the function to-be-run in the thread, plus
    // \a nargs of arguments. The top \a nargs + 1 stack values are moved to the newly created thread.
    thread(lua_State& state, int nargs)
        : m_nargs(nargs)
    {
        // Create the new thread
        m_state = lua_newthread(&state);
        m_ref   = detail::lua_ref::pop_from_stack(state);

        // Move the callable plus arguments over
        lua_xmove(&state, m_state, nargs + 1);
    }

    // Run the thread until it yields or finishes.
    // Exceptions thrown while running the thread will mark the thread as finished.
    // The exceptions themselves are returned via the future.
    // \return the status of the thread after running it.
    status run() noexcept
    {
        if (is_runnable())
        {
            try
            {
                // Resume/start the function
                switch (lua_resume(m_state, nullptr, std::max(0, m_nargs)))
                {
                case LUA_OK:
                {
                    // Thread finished successfully; read the return value, if any
                    value value;
                    if (lua_gettop(m_state) > 0)
                    {
                        value = detail::read_value(*m_state, -1);
                        lua_pop(m_state, 1);
                    }
                    m_promise.set_value(value);
                    m_nargs = -1;
                    break;
                }
                case LUA_YIELD:
                    // We don't care about the yield arguments
                    lua_pop(m_state, lua_gettop(m_state));
                    m_nargs = -1;
                    return status::yielded;
                case LUA_ERRMEM:
                    throw std::bad_alloc();
                default:
                    throw runtime_error(lua_tostring(m_state, -1));
                }
            }
            catch (...)
            {
                m_promise.set_exception(std::current_exception());
            }
        }
        return status::finished;
    }

    // Get the future that will return the result of this thread
    std::future<value> get_future()
    {
        return m_promise.get_future();
    }

private:
    bool is_runnable() const
    {
        switch (lua_status(m_state))
        {
        case LUA_YIELD:
            return true;
        case LUA_OK:
            return m_nargs >= 0;
        }
        return false;
    }

    lua_State* m_state;
    detail::lua_ref m_ref;
    int m_nargs;
    std::promise<value> m_promise;
};

// Executors manage the execution of script threads. These threads are started by calling
// \a script::call_async and passing in an executor.
class executor
{
public:
    virtual ~executor() = default;

    // Adds a thread to the executor to manage
    virtual void add_thread(thread thread) = 0;
};

// An executor that cooperatively runs the threads added to it.
// It runs each thread until it yields, then runs the next one.
class cooperative_executor final : public executor
{
public:
    void add_thread(thread thread) override
    {
        threads.push(std::move(thread));
    }

    // Runs all added threads until they finish
    void run()
    {
        while (!threads.empty())
        {
            auto thread = std::move(threads.front());
            threads.pop();

            // Run the thread
            switch (thread.run())
            {
            case thread::status::yielded:
                // Push the thread back on the queue
                threads.push(std::move(thread));
                break;

            case thread::status::finished:
                // Thread finished successfully
                break;
            }
        }
    }

private:
    std::queue<thread> threads;
};

class script final
{
public:
    //
    // Constructs a named script object.
    //
    // \param name[in] the name of the script. This is used when reporting errors.
    // \param buffer[in] the data of the string. This can be the source code or a compiled script binary.
    // \param registry[in] (optional) a registry of functions and object types that will be integrated with this script.
    //
    // \note The free functions from the registry are available to the top-level code in the script as well.
    //      Registered object types are supported for passing shared pointers of as arguments in #call.
    script(const std::string& name, const std::vector<char>& buffer, std::shared_ptr<type_registry> registry);

    //
    // Constructs a named script object without any registered types.
    //
    // \param name[in] the name of the script. This is used when reporting errors.
    // \param buffer[in] the data of the string. This can be the source code or a compiled script binary.
    script(const std::string& name, const std::vector<char>& buffer)
        : script(name, buffer, nullptr)
    {
    }

    //
    // Calls a function in this script.
    //
    // Finds the function with \p name and calls it with arguments \p args.
    // Supported argument types are: integers, floating-points, strings and shared_ptr of classes in the registry
    // specified in the constructor.
    //
    // \param name the name of the function to call.
    // \param args the arguments to pass to the function.
    // \return the return value of the function. If the function returns multiple values, only the first one is returned.
    // \throws apolo::runtime_error if an error occurred during execution of the function.
    //
	template <typename... Args>
    value call(const std::string& name, Args&& ...args)
    {
	    cooperative_executor executor;
	    auto future = call_async(executor, name, std::forward<Args>(args)...);
	    executor.run();
	    return future.get();
    }

    //
    // Calls a function in this script, asynchronously.
    //
    // This method works just like \a call, except it doesn't immediately execute the function
	// and return the result. Instead, execution of the function is handled by \p executor,
	// and a future to the result is returned.
	//
	// Note: it is the responsibility of the caller to ensure that the script is not destroyed
	// while asynchronous calls are still running.
    //
    template <typename... Args>
    std::future<value> call_async(executor& executor, const std::string& name, Args&& ...args)
    {
             // Get the function
        lua_getglobal(m_state.get(), name.c_str());
        if (!lua_isfunction(m_state.get(), -1))
        {
            // Callback is not a function
            throw runtime_error("Calling undefined function \"" + name + "\"");
        }

        // Push arguments
        (push_value(*m_state.get(), args), ...);

        thread t(*m_state.get(), sizeof...(args));
        auto future = t.get_future();
        executor.add_thread(std::move(t));
        return future;
    }

private:
    template <typename T>
    void push_value(lua_State& state, const T& value)
    {
        detail::push_value(state, value);
    }

    void set_object_methods(lua_State& state, std::type_index type) const;

    template <typename T>
    void push_value(lua_State& state, const std::shared_ptr<T>& value)
    {
        const type_registry::object_type_info_base* info = nullptr;
        if (m_registry != nullptr)
        {
            info = m_registry->get_object_type(typeid(T));
        }

        if (info == nullptr)
        {
            throw runtime_error("Calling script function with reference to unregistered type");
        }

        // Create new userdatum on the stack and copy the shared pointer into it
        void* mem = lua_newuserdata(&state, sizeof(std::shared_ptr<T>));
        new(mem) std::shared_ptr<T>{ value };

        // Associate metatable for the userdata
        if (luaL_newmetatable(&state, detail::metatable_name<T>().c_str()))
        {
            // Populate the new metatable with the object's methods
            set_object_methods(state, typeid(T));

            // Make __index refer to the metatable so all methods in the metatable become available on the object
            lua_pushvalue(&state, -1);
            lua_setfield(&state, -1, "__index");

            // Add garbage collection for references
            lua_pushcfunction(&state, &script::destroy_object_reference<T>);
            lua_setfield(&state, -2, "__gc");
        }

        lua_setmetatable(&state, -2);
    }

    template <typename T>
    static int destroy_object_reference(lua_State* state)
    {
        // Get the to-be-GC'd argument and validate that it's a reference of the correct type
        void* ref = luaL_checkudata(state, 1, detail::metatable_name<T>().c_str());
        if (ref != nullptr)
        {
            auto* ptr = static_cast<std::shared_ptr<T>*>(ref);
            ptr->~shared_ptr();
        }
        return 0;
    }

    void load_builtins();
    static int builtin_yield(lua_State* state);

    std::shared_ptr<type_registry> m_registry;
    detail::lua_state_ptr m_state;
};

}
