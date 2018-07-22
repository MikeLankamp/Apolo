#pragma once

#include <lua/lua.hpp>

#include <any>
#include <cassert>
#include <cstddef>
#include <future>
#include <list>
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

class exception : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

class syntax_error : public exception
{
public:
  using exception::exception;
};

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

    // Helper type for passing along template packs.
    template <typename... T>
    struct pack {};

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
        using argument_pack = pack<Args...>;
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
        using argument_pack = pack<Args...>;
    };

    template <typename ObjectType>
    const std::string& GetMetatableName()
    {
        static const auto metatableName = std::string("ObjectType:") + typeid(ObjectType).name();
        return metatableName;
    }
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
    /*template <typename T, class = std::enable_if_t<std::is_class_v<T>, void>>
    value(std::shared_ptr<T> value)
        : m_storage(ObjectInfo{typeid(T), std::move(value)})
    {}*/

    // Apply a visitor to this value. The visitor can have overloads for each of the supported
    // types.
    template <typename Visitor>
    void visit(Visitor visitor) const
    {
        std::visit(detail::overloaded{
            //[&](const ObjectInfo& x) { visitor(x.m_type, x.m_value); },
            [&](const auto& x) { visitor(x); },
        }, m_storage);
    }

    bool operator==(const value& other) const { return m_storage == other.m_storage; }
    bool operator!=(const value& other) const { return m_storage != other.m_storage; }

private:
    /*struct ObjectInfo
    {
        std::type_index m_type;
        std::any m_value;

        bool operator==(const ObjectInfo& other) const { return std::tie(m_type, m_value) == std::tie(other.m_type, other.m_value); }
        bool operator!=(const ObjectInfo& other) const { return std::tie(m_type, m_value) != std::tie(other.m_type, other.m_value); }
    };*/

    std::variant<std::nullptr_t, bool, long long, double, std::string/*, ObjectInfo*/> m_storage;
};

class Script
{
public:
	virtual ~Script() = default;

	virtual std::future<void> CallFunction(const std::string& name, const std::vector<value>& parameters) = 0;
};

class ScriptEngine final
{
private:
    class LuaCallback;
    class TypeHandler;

    class SharedPointerCaster
    {
    public:
        virtual ~SharedPointerCaster() = default;
        virtual std::any Cast(const std::any& value) const = 0;
    };

    template <typename DerivedType, typename BaseType>
    class SpecificSharedPointerCaster : public SharedPointerCaster
    {
        static_assert(std::is_base_of_v<BaseType, DerivedType> && !std::is_same_v<BaseType, DerivedType>,
            "Specified type is not a base class of the derived class");

    public:
        std::any Cast(const std::any& value) const override
        {
            const auto* ptr = std::any_cast<std::shared_ptr<DerivedType>*>(value);
            assert(ptr != nullptr);
            return std::static_pointer_cast<BaseType>(*ptr);
        }
    };


public:
    struct ObjectTypeInfo
    {
        // The ID of the object type
        const std::type_index m_typeIndex;

        std::unique_ptr<TypeHandler> m_handler;

        // The base types of this object type
        std::unordered_map<std::type_index, std::unique_ptr<SharedPointerCaster>> m_bases;

        // The methods in the object type
        std::unordered_map<std::string, std::unique_ptr<LuaCallback>> m_methods;
    };

public:
	template <typename ObjectType>
	class ObjectDescription
	{
		static_assert(std::is_class_v<ObjectType>);

	public:
		ObjectDescription() = default;
		ObjectDescription(const ObjectDescription&) = delete;

		template <typename BaseType>
		ObjectDescription& WithBase()
		{
		    m_bases.emplace(typeid(BaseType), std::make_unique<SpecificSharedPointerCaster<ObjectType,BaseType>>());
			return *this;
		}

		template <typename R, typename... Args>
		ObjectDescription& WithMethod(const std::string& name, R(ObjectType::*callable)(Args...))
		{
			auto [it,success] = m_methods.emplace(name, std::make_unique<UnboundLuaCallback<ObjectType, decltype(callable), Args...>>(callable));
			assert(success);
			return *this;
		}

		template <typename R, typename... Args>
		ObjectDescription& WithMethod(const std::string& name, R(ObjectType::*callable)(Args...) const)
		{
			auto[it, success] = m_methods.emplace(name, std::make_unique<UnboundLuaCallback<ObjectType, decltype(callable), Args...>>(callable));
			assert(success);
			return *this;
		}

		ObjectTypeInfo Build()
		{
			return {
			    typeid(ObjectType),
			    std::make_unique<SpecificTypeHandler<ObjectType>>(),
			    std::move(m_bases),
			    std::move(m_methods)
			};
		}

	private:
		// The base types of this object. Their methods must be inherited
		std::unordered_map<std::type_index, std::unique_ptr<SharedPointerCaster>> m_bases;

        // The methods in the object type
		std::unordered_map<std::string, std::unique_ptr<LuaCallback>> m_methods;
	};

	ScriptEngine();
	~ScriptEngine();

	// Registers an object type for use in scripts
	// Native C++ objects can be passed along to script method calls only after
	// registering them via this function.
	void RegisterObjectType(ObjectTypeInfo info);

	// Find registered object type info based on the type
	const ObjectTypeInfo* GetObjectTypeInfo(std::type_index typeIndex) const;


	// Registers a std::function as global function
    template <typename R, typename... Args>
    void RegisterGlobalFunction(std::string name, std::function<R(Args...)> callable)
    {
        assert(m_globalFunctions.find(name) == m_globalFunctions.end());
        m_globalFunctions.emplace(std::move(name), std::make_unique<SimpleLuaCallback<R, Args...>>(std::move(callable)));
    }

    // Registers a generic callable as global function
    template <typename Callable>
    void RegisterGlobalFunction(std::string name, Callable&& callable)
    {
        using traits = detail::function_traits<Callable>;
        RegisterGlobalFunction<typename traits::return_type>(std::move(name), std::forward<Callable>(callable), typename traits::argument_pack{});
    }

    // Registers a member function on a specific object as a global function
    template <typename ObjectType, typename R, typename... Args>
    void RegisterGlobalFunction(std::string name, ObjectType& object, R (ObjectType::*callable)(Args...))
    {
        // Use a lambda to bind the member function pointer to the object
        RegisterGlobalFunction<R, Args...>(std::move(name), {[&object,callable](Args&&... args) {
            return (object.*callable)(std::forward<Args>(args)...);
        }});
    }

    // Registers a const-member function on a specific object as a global function
    template <typename ObjectType, typename R, typename... Args>
    void RegisterGlobalFunction(std::string name, const ObjectType& object, R (ObjectType::*callable)(Args...) const)
    {
        // Use a lambda to bind the member function pointer to the object
        RegisterGlobalFunction<R, Args...>(std::move(name), {[&object,callable](Args&&... args) {
            return (object.*callable)(std::forward<Args...>(args)...);
        }});
    }


	// Creates a script with given name from the given buffer.
	std::unique_ptr<apolo::Script> CreateScript(const std::string& name, const std::vector<char>& buffer);

	void Run();

	/// Creates a thread out of the new lua state
	//std::shared_ptr<Thread> CreateThread(lua_State* s, int index, const value* args, int nargs);
	//std::unique_ptr<Event>  CreateTimer(float seconds);

	//void SuspendThread(lua_State* s, std::shared_ptr<Event> e);
private:
	class Script;
	class Thread;

	static value ReadValue(lua_State& state, int index);
	static long long ReadScriptInteger(lua_State& state, int index);
	static double ReadScriptDouble(lua_State& state, int index);
	static std::string ReadScriptString(lua_State& state, int index);

	static int LuaTrampoline(lua_State* L);
	static void PushValue(lua_State& state, const value& value);
	static void CreateMetatable(lua_State& state, std::type_index typeIndex);

    template <typename R, typename Callable, typename... Args>
    void RegisterGlobalFunction(std::string name, Callable&& callable, detail::pack<Args...>)
    {
        RegisterGlobalFunction(std::move(name), std::function<R(Args...)>{std::move(callable)});
    }

	template<int Index, typename T>
	static std::enable_if_t<std::is_floating_point_v<T>, T> ReadArgument(lua_State& state, T*)
	{
	    return static_cast<T>(ReadScriptDouble(state, Index));
	};

	template<int Index, typename T>
	static std::enable_if_t<std::is_integral_v<T>, T> ReadArgument(lua_State& state, T*)
	{
	    return static_cast<T>(ReadScriptInteger(state, Index));
	};

	template<int Index>
	static std::string ReadArgument(lua_State& state, std::string*)
	{
	    return ReadScriptString(state, Index);
	};

	template <int Index>
	static auto ReadArguments(lua_State& state)
	{
	    if (lua_gettop(&state) + 1 != Index)
	    {
	        throw runtime_error("Insufficient arguments to function");
	    }
	    return std::make_tuple();
	}

	template <int Index, typename Head, typename... Args>
	static auto ReadArguments(std::enable_if_t<std::is_same_v<std::decay_t<Head>, std::vector<value>>, lua_State&> state)
	{
	    static_assert(sizeof...(Args) == 0, "std::vector<value> must be last argument in function");
	    std::vector<value> values;
	    int maxArg = lua_gettop(&state);
	    for (int i = Index; i <= maxArg; ++i)
	    {
	        values.push_back(ReadValue(state, i));
	    }
	    return std::make_tuple(values);
	}

	template <int Index, typename Head, typename... Args>
	static auto ReadArguments(std::enable_if_t<!std::is_same_v<std::decay_t<Head>, std::vector<value>>, lua_State&> state)
	{
	    auto arg = ReadArgument<Index>(state, std::add_pointer_t<Head>{});
	    return std::tuple_cat(std::make_tuple(arg), ReadArguments<Index + 1, Args...>(state));
	}

	class TypeHandler
	{
	public:
	    virtual ~TypeHandler() = default;

	    virtual const std::string& GetMetatableName() const = 0;
	    virtual bool PushScriptObjectReference(lua_State& state, const std::any& object) const = 0;
	    virtual void DestroyScriptObjectReference(void* ref) const = 0;
	};

	template <typename ObjectType>
    class SpecificTypeHandler : public TypeHandler
    {
        using SharedPointer = std::shared_ptr<ObjectType>;

    public:
        const std::string& GetMetatableName() const override
        {
            return detail::GetMetatableName<ObjectType>();
        }

        bool PushScriptObjectReference(lua_State& state, const std::any& object) const override
        {
            const auto* ptr = std::any_cast<SharedPointer>(&object);
            if (ptr != nullptr)
            {
                // Create new userdatum
                void* mem = lua_newuserdata(&state, sizeof(SharedPointer));
                new(mem) SharedPointer{ *ptr };
                return true;
            }
            return false;
        }

        void DestroyScriptObjectReference(void* reference) const override
        {
            auto* ptr = static_cast<SharedPointer*>(reference);
            if (ptr != nullptr)
            {
                ptr->~SharedPointer();
            }
        }
    };


	class LuaCallback
	{
	public:
	    virtual ~LuaCallback() = default;
	    virtual int Invoke(lua_State& state) = 0;

	protected:
	    template <typename Callable, typename ArgsTuple>
	    int Invoke(lua_State& state, Callable &&callable, ArgsTuple&& argsTuple)
	    {
            return PushResult(state, [&](){
                return std::apply(std::forward<Callable>(callable), std::forward<ArgsTuple>(argsTuple));
            });
	    }

	private:
        template <
            typename Callable,
            typename std::enable_if_t<std::is_void_v<std::result_of_t<Callable()>>, int*> = nullptr>
        int PushResult(lua_State&, Callable&& callable)
        {
            callable();
            return 0;
        }

        template <
            typename Callable,
            typename std::enable_if_t<!std::is_void_v<std::result_of_t<Callable()>>, int*> = nullptr>
        int PushResult(lua_State& state, Callable&& callable)
        {
            PushValue(state, callable());
            return 1;
        }
	};

	template <typename ObjectType, typename Callable, typename... Args>
	class UnboundLuaCallback : public LuaCallback
	{
	public:
	    UnboundLuaCallback(Callable callable)
	        : m_callable(callable)
	    {
	    }

	    int Invoke(lua_State& state) override
	    {
	        // Check that the first argument is a native object reference
            auto* ref = static_cast<std::shared_ptr<ObjectType>*>(luaL_checkudata(&state, 1, detail::GetMetatableName<ObjectType>().c_str()));
	        if (ref != nullptr)
	        {
	            // It is; read the arguments from Lua and call the callback
	            assert(*ref != nullptr);
	            auto args = ReadArguments<2, std::decay_t<Args>...>(state);
	            auto applyArgs = std::tuple_cat(std::tie(**ref), args);
	            return LuaCallback::Invoke(state, m_callable, applyArgs);
	        }
	        throw runtime_error("Wrong arguments to function");
	    }

	private:
	    Callable m_callable;
	};

	template <typename R, typename... Args>
	class SimpleLuaCallback : public LuaCallback
	{
	public:
	    SimpleLuaCallback(std::function<R(Args...)> callable)
	        : m_callable(std::move(callable))
	    {
	    }

	    int Invoke(lua_State& state) override
	    {
	        auto args = ReadArguments<1, std::decay_t<Args>...>(state);
	        return LuaCallback::Invoke(state, m_callable, args);
	    }

	private:
	    std::function<R(Args...)> m_callable;
	};

	std::future<void> CreateThread(lua_State* s, int index, const std::vector<value>& arguments);

private:
	// All registered object types
	std::unordered_map<std::type_index, ObjectTypeInfo> m_objectTypes;

	// All registered global functions
	std::unordered_map<std::string, std::unique_ptr<LuaCallback>> m_globalFunctions;

	// All ready threads -- ready to be run
	std::list<std::shared_ptr<Thread>> m_readyThreads;
};

/*class Thread : public Waitable, public std::enable_shared_from_this<Thread>
{
    friend void ScriptScheduler::Update();
    friend void ScriptScheduler::SuspendThread(lua_State*, std::shared_ptr<Event>);

    lua_State*  m_state;        // Thread state
    int         m_nargs;        // Arguments for lua_resume()
    value       m_retval;       // Return value of the thread function
    std::shared_ptr<Event> m_event;        // Event we're waiting on
    bool        m_finished;     // Are we done?

public:
    void Wake();
    void Wait();
    Thread(lua_State* state, int nargs);
};*/

}
