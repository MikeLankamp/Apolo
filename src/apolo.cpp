    #include <apolo/apolo.h>

#include <cassert>
#include <sstream>

namespace apolo
{

namespace
{
    struct LuaStateDeleter
    {
        void operator()(lua_State* state)
        {
            lua_close(state);
        }
    };

    using LuaStateUniquePtr = std::unique_ptr<lua_State, LuaStateDeleter>;

    class LuaReference
    {
    public:
        LuaReference(lua_State* state, int luaRef)
        : m_state(state)
        , m_luaRef(luaRef)
        {
        }

        LuaReference(const LuaReference&) = delete;

        LuaReference(LuaReference&& other)
        {
            m_state = other.m_state;
            m_luaRef = other.m_luaRef;
            other.m_state = nullptr;
            other.m_luaRef = LUA_NOREF;
        }

        ~LuaReference()
        {
            if (m_luaRef != LUA_NOREF)
            {
                luaL_unref(m_state, LUA_REGISTRYINDEX, m_luaRef);
            }
        }

        // Store a reference to the item taken from the top of the stack
        // so it doesn't get garbage collected.
        static LuaReference CreateFromStack(lua_State* state)
        {
            return LuaReference(state, luaL_ref(state, LUA_REGISTRYINDEX));
        }

    private:
        lua_State* m_state;
        int m_luaRef;
    };
}

int ScriptEngine::LuaTrampoline(lua_State* state)
{
    try
    {
        auto callback = static_cast<LuaCallback*>(lua_touserdata(state, lua_upvalueindex(1)));
        return callback->Invoke(*state);
    }
    catch (std::exception& ex)
    {
        lua_pushstring(state, ex.what());
        lua_error(state);
    }
    return 0;
}

value ScriptEngine::ReadValue(lua_State& state, int index)
{
    switch (lua_type(&state, index))
    {
    case LUA_TNIL:
        return value();

    case LUA_TNUMBER:
        if (lua_isinteger(&state, index))
        {
            return value(lua_tointeger(&state, index));
        }
        return value(lua_tonumber(&state, index));

    case LUA_TBOOLEAN:
        return value(lua_toboolean(&state, index) != 0);

    case LUA_TSTRING:
        return value(std::string{ lua_tostring(&state, index) });

    default:
        break;
    }
    throw runtime_error("Wrong arguments to function");
}

long long ScriptEngine::ReadScriptInteger(lua_State& state, int index)
{
    if (!lua_isnumber(&state, index))
    {
        throw runtime_error("Wrong arguments to function");
    }
    return static_cast<long long>(lua_tonumber(&state, index));
}

double ScriptEngine::ReadScriptDouble(lua_State& state, int index)
{
    if (!lua_isnumber(&state, index))
    {
        throw runtime_error("Wrong arguments to function");
    }
    return static_cast<double>(lua_tonumber(&state, index));
}

std::string ScriptEngine::ReadScriptString(lua_State& state, int index)
{
    if (!lua_isstring(&state, index))
    {
        throw runtime_error("Wrong arguments to function");
    }
    return lua_tostring(&state, index);
}

class ScriptEngine::Script : public apolo::Script
{
public:
    Script(ScriptEngine& scriptEngine)
    : m_scriptEngine(scriptEngine)
    , m_state(luaL_newstate())
    {
        // Create Lua state
        if (m_state == nullptr)
        {
            throw std::bad_alloc();
        }

        // Store reference to self so we can get the Script from the state.
        lua_pushlightuserdata(m_state.get(), this);
        lua_setfield(m_state.get(), LUA_REGISTRYINDEX, SELF_KEY_NAME);
    }

    void ExecuteBuffer(const std::string name, const std::vector<char>& buffer)
    {
        // Load script into state
        int error;
        if ((error = luaL_loadbuffer(m_state.get(), buffer.data(), buffer.size(), name.c_str())) != 0)
        {
            // Error during load
            switch (error)
            {
            case LUA_ERRMEM:
                throw std::bad_alloc();
            case LUA_ERRSYNTAX:
                throw syntax_error(lua_tostring(m_state.get(), -1));
            default:
                throw runtime_error(lua_tostring(m_state.get(), -1));
            }
        }

        // Execute top-level chunk.
        // This sets up functions, includes files, sets up globals, etc.
        if ((error = lua_pcall(m_state.get(), 0, 0, 0)) != 0)
        {
            switch (error)
            {
            case LUA_ERRMEM:
                throw std::bad_alloc();
            default:
                throw runtime_error(lua_tostring(m_state.get(), -1));
            }
        }
    }

    static Script* GetFromState(lua_State& state)
    {
        lua_getfield(&state, LUA_REGISTRYINDEX, SELF_KEY_NAME);
        Script* script = static_cast<Script*>(lua_touserdata(&state, -1));
        lua_pop(&state, 1);
        return script;
    }

    void SetObjectMethods(lua_State& state, std::type_index type) const
    {
        auto* info = m_scriptEngine.GetObjectTypeInfo(type);
        assert(info != nullptr);

        // Register base classes first
        for (const auto& [baseType, caster] : info->m_bases)
        {
            SetObjectMethods(state, baseType);
        }

        // Register this object's methods
        for (const auto& [name, callback] : info->m_methods)
        {
            lua_pushlightuserdata(&state, callback.get());
            lua_pushcclosure(&state, &LuaTrampoline, 1);
            lua_setfield(&state, -2, name.c_str());
        }
    }

    void PushScriptObject(lua_State& state, std::type_index type, const std::any& object) const
    {
        auto* info = m_scriptEngine.GetObjectTypeInfo(type);
        if (info != nullptr)
        {
            if (info->m_handler->PushScriptObjectReference(state, object))
            {
                // Associate metatable for the userdata
                if (luaL_newmetatable(&state, info->m_handler->GetMetatableName().c_str()))
                {
                    // Populate the new metatable with the object's methods
                    SetObjectMethods(state, type);

                    // Make __index refer to the metatable so all methods in the metatable become available on the object
                    lua_pushvalue(&state, -1);
                    lua_setfield(&state, -1, "__index");

                    // Add garbage collection for references
                    lua_pushlightuserdata(&state, const_cast<void*>(static_cast<const void*>(info)));
                    lua_pushcclosure(&state, &Script::DestroyScriptObjectReference, 1);
                    lua_setfield(&state, -2, "__gc");
                }

                lua_setmetatable(&state, -2);
            }
        }
    }

    static int DestroyScriptObjectReference(lua_State* state)
    {
        const auto* info = static_cast<ScriptEngine::ObjectTypeInfo*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (info != nullptr)
        {
            // Get the to-be-GC'd argument and validate that it's a reference of the correct type
            void* ref = luaL_checkudata(state, 1, info->m_handler->GetMetatableName().c_str());

            info->m_handler->DestroyScriptObjectReference(ref);
        }
        return 0;
    }

    void RegisterGlobalFunction(const std::string& name, LuaCallback& callback)
    {
        lua_pushlightuserdata(m_state.get(), &callback);
        lua_pushcclosure(m_state.get(), &LuaTrampoline, 1);
        lua_setglobal(m_state.get(), name.c_str());
    }

    std::future<void> CallFunction(const std::string& name, const std::vector<value>& parameters) override
    {
        lua_getglobal(m_state.get(), name.c_str());
        auto future = m_scriptEngine.CreateThread(m_state.get(), -1, parameters);
        lua_pop(m_state.get(), 1);
        return future;
    }

private:
    static constexpr const char* SELF_KEY_NAME = "ScriptSelf";

    ScriptEngine& m_scriptEngine;
    LuaStateUniquePtr m_state;
};

class ScriptEngine::Thread
{
public:
    Thread(lua_State* state, LuaReference&& threadRef, int nArgs)
    : m_state(state)
    , m_threadRef(std::move(threadRef))
    , m_nArgs(nArgs)
    {
    }

    auto GetFinishedFuture()
    {
        return m_promise.get_future();
    }

    bool Run()
    {
        int err = lua_resume(m_state, nullptr, m_nArgs);
        if (err == LUA_YIELD)
        {
            // Coroutine yielded; remember number of arguments
            m_nArgs = lua_gettop(m_state);
            return false;
        }

        if (err == LUA_ERRRUN)
        {
            // Error occured during execution; pop error and thread
            throw runtime_error(lua_tostring(m_state, -1));
        }

        // Coroutine is finished
        m_promise.set_value();
        return true;
    }

private:
    std::promise<void> m_promise;	// Finished promise
    lua_State* m_state;				// Thread state
    LuaReference m_threadRef;		// Reference to the Lua thread
    int m_nArgs;					// Arguments for lua_resume()
};

ScriptEngine::ScriptEngine()
{
}

ScriptEngine::~ScriptEngine()
{
}

void ScriptEngine::Run()
{
    // Update timers
    /*float t = GetGameTime();
	for (TimerList::iterator p = g_Timers.begin(); p != g_Timers.end();)
	{
		TimerList::iterator cur = p++;
		if ((*cur)->Check(t))
		{
			g_Timers.erase(cur);
		}
	}*/

    // Run threads
    while (!m_readyThreads.empty())
    {
        // Execute first Lua thread
        auto t = m_readyThreads.front();
        m_readyThreads.pop_front();

        const bool finished = t->Run();
        if (!finished)
        {
            m_readyThreads.push_back(t);
        }
    }
}

/*
void ScriptEngine::Builtin_Require(const std::string& name)
{
	if (nargs != 1 || !lua_tostring(L, 1))
	{
		Error(L, "invalid arguments");
		return 0;
	}
	const char* libname = lua_tostring(L, 1);

	// Find the dirname path of this Script's filename
	auto sl = m_name.find_last_of("\\/");
	auto dir = (sl != std::string::npos) ? m_name.substr(0, sl) : m_name;

	// First try to load the script from our dir, then from the library dir
	const std::string paths[2] = {
		dir + "/" + libname,
		LIBRARY_PATH + "/" + libname,
	};

	int i;
	for (i = 0; i < 2; i++)
	{
		auto stream = g_AssetLoader->LoadScript(paths[i]);
		if (stream != nullptr)
		{
			try {
				Load(L, *stream, paths[i].c_str());
			}
			catch (BadFileException&) {
			}
			break;
		}
	}

	if (i == 2) {
		Error(L, "cannot find library script `%s'", libname);
	}
	return 0;
}
 */

std::unique_ptr<apolo::Script> ScriptEngine::CreateScript(const std::string& name, const std::vector<char>& buffer)
{
    auto script = std::make_unique<Script>(*this);
    for (const auto& [name, callback] : m_globalFunctions)
    {
        script->RegisterGlobalFunction(name, *callback);
    }
    script->ExecuteBuffer(name, buffer);
    return script;
}

std::future<void> ScriptEngine::CreateThread(lua_State* s, int index, const std::vector<value>& arguments)
{
    if (!lua_isfunction(s, index))
    {
        // Callback is not a function
        return {};
    }

    // Create the Lua thread
    // This is a non-owning pointer because the reference manages its lifetime
    auto d = lua_newthread(s);
    if (d == nullptr)
    {
        return {};
    }

    // Store a reference to the thread (taken from the top of the stack)
    // so it doesn't get garbage collected.
    auto ref = LuaReference::CreateFromStack(s);

    // Move function from source stack to destination stack
    lua_pushvalue(s, index);
    lua_xmove(s, d, 1);
    assert(lua_isfunction(d, -1));

    // Push the parameters
    for (const auto& arg : arguments)
    {
        PushValue(*d, arg);
    }

    // Create the thread
    auto t = std::make_shared<Thread>(d, std::move(ref), arguments.size());

    // Ready to run
    auto future = t->GetFinishedFuture();
    m_readyThreads.push_back(std::move(t));
    return future;
}

void ScriptEngine::RegisterObjectType(ObjectTypeInfo info)
{
    auto index = info.m_typeIndex;

    // Check the type hasn't been registered yet
    assert(m_objectTypes.find(index) == m_objectTypes.end());

    // Check that the base types have been registered
    for (const auto& [type, caster] : info.m_bases)
    {
        assert(m_objectTypes.find(type) != m_objectTypes.end());
    }

    m_objectTypes.emplace(index, std::move(info));
}

const ScriptEngine::ObjectTypeInfo* ScriptEngine::GetObjectTypeInfo(std::type_index typeIndex) const
{
    auto it = m_objectTypes.find(typeIndex);
    return (it != m_objectTypes.end()) ? &it->second : nullptr;
}

void ScriptEngine::PushValue(lua_State& state, const value& value)
{
    value.visit(detail::overloaded{
        [&](nullptr_t) { lua_pushnil(&state); },
            [&](bool arg) { lua_pushboolean(&state, arg); },
            [&](long long arg) { lua_pushnumber(&state, static_cast<lua_Number>(arg)); },
            [&](double arg) { lua_pushnumber(&state, arg); },
            [&](const std::string& arg) { lua_pushstring(&state, arg.c_str()); },
            [&](std::type_index type, const std::any& object)
            {
                auto* script = Script::GetFromState(state);
                if (script != nullptr)
                {
                    script->PushScriptObject(state, type, object);
                }
            },
    });
}

}
