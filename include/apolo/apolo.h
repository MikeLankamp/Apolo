#pragma once

#include <lua/lua.hpp>

#include <cassert>
#include <functional>
#include <memory>
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


    template <typename T>
    void push_value(lua_State& state, std::enable_if_t<std::is_integral_v<T>, T> value)
    {
        lua_pushnumber(&state, static_cast<lua_Number>(value));
    }

    template <typename T>
    void push_value(lua_State& state, std::enable_if_t<std::is_floating_point_v<T>, T> value)
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
    value(T value) : m_storage(static_cast<long long>(value)) {}

    // Construct a value from a floating-point value
    template <typename T, typename std::enable_if_t<std::is_floating_point_v<T>,int*> = nullptr>
    value(T value) : m_storage(static_cast<double>(value)) {}

    // Construct a value from a boolean
    value(bool value) : m_storage(value) {}

    // Construct a value from a string
    value(std::string value) : m_storage(std::move(value)) {}

    // Construct a value from a raw string
    value(const char* value) : m_storage(std::string(value)) {}

    // Construct a value from a shared pointer to an object
    template <typename T, class = std::enable_if_t<std::is_class_v<T>, void>>
    value(std::shared_ptr<T> value)
        : m_storage(object_info{typeid(T), reinterpret_cast<std::uintptr_t>(value.get())})
    {}

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
    static std::enable_if_t<std::is_integral_v<T>, T> read_value(lua_State& state, int index, T*)
    {
        return static_cast<T>(detail::read_integer(state, index));
    };

    static std::string read_value(lua_State& state, int index, std::string*)
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
        virtual int invoke(lua_State& state) = 0;

    protected:
        template <typename Callable, typename ArgsTuple>
        int invoke(lua_State& state, Callable &&callable, ArgsTuple&& argsTuple)
        {
            return push_value(state, [&](){
                return std::apply(std::forward<Callable>(callable), std::forward<ArgsTuple>(argsTuple));
            });
        }

    private:
        template <typename Callable>
        std::enable_if_t<std::is_void_v<std::result_of_t<Callable()>>, int>
        push_value(lua_State&, Callable&& callable)
        {
            callable();
            return 0;
        }

        template <typename Callable>
        std::enable_if_t<!std::is_void_v<std::result_of_t<Callable()>>, int>
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

        int invoke(lua_State& state) override
        {
            auto args = read_arguments<1, std::decay_t<Args>...>(state);
            return detail::lua_callback::invoke(state, m_callable, args);
        }

    private:
        std::function<R(Args...)> m_callable;
    };

    template <typename ObjectType, typename Callable, typename... Args>
    class unbound_lua_callback : public detail::lua_callback
    {
    public:
        unbound_lua_callback(Callable callable)
            : m_callable(callable)
        {
        }

        int invoke(lua_State& state) override
        {
            // Check that the first argument is a native object reference
            auto* ref = static_cast<std::shared_ptr<ObjectType>*>(luaL_testudata(&state, 1, detail::metatable_name<ObjectType>().c_str()));
            if (ref != nullptr)
            {
                // It is; read the arguments from Lua and call the callback
                assert(*ref != nullptr);
                auto args = read_arguments<2, std::decay_t<Args>...>(state);
                auto applyArgs = std::tuple_cat(std::tie(**ref), args);
                return detail::lua_callback::invoke(state, m_callable, applyArgs);
            }
            throw runtime_error("Wrong arguments to function");
        }

    private:
        Callable m_callable;
    };
}

struct object_type_info
{
    // The ID of the object type
    const std::type_index m_typeIndex;

    // The methods in the object type
    std::unordered_map<std::string, std::unique_ptr<detail::lua_callback>> m_methods;
};

template <typename ObjectType>
class object_description
{
    static_assert(std::is_class_v<ObjectType>);

public:
    template <typename R, typename... Args>
    object_description& WithMethod(const std::string& name, R(ObjectType::*callable)(Args...))
    {
        auto [it,success] = m_methods.emplace(name,
            std::make_unique<detail::unbound_lua_callback<ObjectType, decltype(callable), Args...>>(callable));
        assert(success);
        return *this;
    }

    template <typename R, typename... Args>
    object_description& WithMethod(const std::string& name, R(ObjectType::*callable)(Args...) const)
    {
        auto[it, success] = m_methods.emplace(name,
            std::make_unique<detail::unbound_lua_callback<ObjectType, decltype(callable), Args...>>(callable));
        assert(success);
        return *this;
    }

    object_type_info Build()
    {
        return {
            typeid(ObjectType),
            std::move(m_methods)
        };
    }

private:
    // The methods in the object type
    std::unordered_map<std::string, std::unique_ptr<detail::lua_callback>> m_methods;
};

//
// Registry for method and class information
//
// Construct a registry, register functions and classes and pass it in to \ref script instances to allow
// the registered methods and classes to be used in those script instances.
//
class type_registry
{
public:
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
    void add_object_type(object_type_info info);

    //
    // Finds a registered object type info based on the type.
    //
    const object_type_info* get_object_type(std::type_index typeIndex) const;

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
    std::unordered_map<std::type_index, object_type_info> m_object_types;
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
	    // Push function on the stack
        lua_getglobal(m_state.get(), name.c_str());
        try
        {
            if (!lua_isfunction(m_state.get(), -1))
            {
                // Callback is not a function
                throw runtime_error("Calling undefined function \"" + name + "\"");
            }

            // Push arguments
            (push_value(*m_state, args), ...);
        }
        catch (...)
        {
            // Remove function from stack
            lua_pop(m_state.get(), 1);
            throw;
        }

        // Call the function
        switch (lua_pcall(m_state.get(), sizeof...(args), 1, 0))
        {
        case 0:
            break;
        case LUA_ERRMEM:
            throw std::bad_alloc();
        default:
            throw runtime_error(lua_tostring(m_state.get(), -1));
        }

        // Read the return value
        auto value = detail::read_value(*m_state, -1);
        lua_pop(m_state.get(), 1);
        return value;
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
        const object_type_info* info = nullptr;
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

    std::shared_ptr<type_registry> m_registry;
    detail::lua_state_ptr m_state;
};

}
