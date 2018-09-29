#include <apolo/apolo.h>
#include <array>
#include "lua/lualib.h"
#include <cstring>

namespace apolo
{

namespace
{
    template <typename Callable>
    int catch_exceptions(lua_State* state, const Callable& callable)
    {
        try
        {
            return callable();
        }
        catch (std::exception& ex)
        {
            lua_pushstring(state, ex.what());
            lua_error(state);
        }
        catch (...)
        {
            lua_pushstring(state, "unknown exception");
            lua_error(state);
        }
        return 0;
    }

    int lua_trampoline(lua_State* state)
    {
        return catch_exceptions(state, [&]{
            auto callback = static_cast<detail::lua_callback*>(lua_touserdata(state, lua_upvalueindex(1)));
            return callback->invoke(*state);
        });
    }

    auto create_lua_state()
    {
        detail::lua_state_ptr state(luaL_newstate());
        if (state == nullptr)
        {
            throw std::bad_alloc();
        }
        return state;
    }

    std::string trim(const std::string& str)
    {
        auto begin = str.begin(), end = str.end();
        while (isspace(*begin)) ++begin;
        while (end > begin && isspace(*(end - 1))) --end;
        return std::string(begin, end);
    }

    static constexpr const char* SELF_KEY_NAME = "script_self";
}

namespace detail
{
    value read_value(lua_State& state, int index)
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

    long long read_integer(lua_State& state, int index)
    {
        if (!lua_isnumber(&state, index))
        {
            throw runtime_error("Wrong arguments to function");
        }
        return static_cast<long long>(lua_tonumber(&state, index));
    }

    double read_double(lua_State& state, int index)
    {
        if (!lua_isnumber(&state, index))
        {
            throw runtime_error("Wrong arguments to function");
        }
        return static_cast<double>(lua_tonumber(&state, index));
    }

    std::string read_string(lua_State& state, int index)
    {
        if (!lua_isstring(&state, index))
        {
            throw runtime_error("Wrong arguments to function");
        }
        return lua_tostring(&state, index);
    }
}

const type_registry::object_type_info_base* type_registry::get_object_type(std::type_index typeIndex) const
{
    auto it = m_object_types.find(typeIndex);
    return (it != m_object_types.end()) ? it->second.get() : nullptr;
}

template <typename Whitelist>
static bool contains(const Whitelist& whitelist, const char* value)
{
    return std::find_if(whitelist.begin(), whitelist.end(), [value](const char* x)
    {
        return std::strcmp(x, value) == 0;
    }) != whitelist.end();
}

template <typename Whitelist>
static void filter_global_table(lua_State* L, const Whitelist& whitelist)
{
    lua_pushglobaltable(L);

    // Filter the returned table by the whitelisted keys
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        if (lua_type(L, -2) != LUA_TSTRING || !contains(whitelist, lua_tostring(L, -2)))
        {
            // Clear the field
            lua_pushvalue(L, -2);
            lua_pushnil(L);
            lua_rawset(L, -5);
        }
        // Pop the value, leave the key for lua_next
        lua_pop(L, 1);
    }
}

// The libraries we want to import into new evironments
static const std::array<luaL_Reg, 4> s_builtin_libs =
{{
  {LUA_TABLIBNAME, &luaopen_table},
  {LUA_STRLIBNAME, &luaopen_string},
  {LUA_MATHLIBNAME, &luaopen_math},
  {LUA_UTF8LIBNAME, &luaopen_utf8},
}};

// The whitelisted fields from Lua's base library
static constexpr std::array<const char*, 10> baselib_whitelist =
{{
    "assert", "pairs", "ipairs", "next", "select", "tonumber", "tostring", "type", "_G", "_VERSION"
}};


void script::load_builtins()
{
    // The "base" lib is special because:
    // a) it loads directly into the global table, and
    // b) it contains several methods with are undesired in a sandboxed environment
    // So we call it and then filter out the undesired methods
    luaopen_base(m_state.get());
    lua_pop(m_state.get(), 1);
    filter_global_table(m_state.get(), baselib_whitelist);

    // Import the normal builtin libraries
    for (const auto& lib : s_builtin_libs)
    {
        luaL_requiref(m_state.get(), lib.name, lib.func, 1);
        lua_pop(m_state.get(), 1);
    }

    // Add our custom global methods
    lua_pushcfunction(m_state.get(), &script::builtin_yield);
    lua_setglobal(m_state.get(), "yield");

    lua_pushcfunction(m_state.get(), &script::builtin_require);
    lua_setglobal(m_state.get(), "require");
}

script::script(const std::string& name, const std::vector<char>& buffer, const configuration& config, std::shared_ptr<type_registry> registry)
    : m_configuration(config)
    , m_registry(std::move(registry))
    , m_state(create_lua_state())
{
    // Store a reference to ourselves so we can get the script instance from the state.
    lua_pushlightuserdata(m_state.get(), this);
    lua_setfield(m_state.get(), LUA_REGISTRYINDEX, SELF_KEY_NAME);

    // Load the built-in methods
    load_builtins();

    if (m_registry != nullptr)
    {
        // Register the free functions as global functions before executing
        for (const auto& [method_name, callback] : m_registry->free_functions())
        {
            lua_pushlightuserdata(m_state.get(), callback.get());
            lua_pushcclosure(m_state.get(), &lua_trampoline, 1);
            lua_setglobal(m_state.get(), method_name.c_str());
        }
    }

    run(buffer, name);
}

void script::run(const script_data& buffer, const std::string& name)
{
    // Load script into state
    switch (luaL_loadbuffer(m_state.get(), buffer.data(), buffer.size(), name.c_str()))
    {
    case LUA_OK:
        break;
    case LUA_ERRMEM:
        throw std::bad_alloc();
    case LUA_ERRSYNTAX:
        throw syntax_error(lua_tostring(m_state.get(), -1));
    default:
        throw runtime_error(lua_tostring(m_state.get(), -1));
    }

    // Execute top-level chunk
    switch (lua_pcall(m_state.get(), 0, 0, 0))
    {
    case LUA_OK:
        break;
    case LUA_ERRMEM:
        throw std::bad_alloc();
    default:
        throw runtime_error(lua_tostring(m_state.get(), -1));
    }
}

void script::set_object_methods(lua_State& state, std::type_index type) const
{
    assert(m_registry != nullptr);
    auto* info = m_registry->get_object_type(type);
    assert(info != nullptr);

    for (const auto& [name, callback] : info->methods())
    {
        lua_pushlightuserdata(&state, callback.get());
        lua_pushcclosure(&state, &lua_trampoline, 1);
        lua_setfield(&state, -2, name.c_str());
    }
}

configuration script::default_configuration()
{
    return configuration{};
}

script* script::script_from_state(lua_State& state)
{
    lua_getfield(&state, LUA_REGISTRYINDEX, SELF_KEY_NAME);
    script* s = static_cast<script*>(lua_touserdata(&state, -1));
    lua_pop(&state, 1);
    return s;
}

int script::builtin_require(lua_State* state)
{
    script* s = script_from_state(*state);
    if (s == nullptr)
    {
        return luaL_error(state, "Invalid call to require()");
    }

    const char* libname = lua_tostring(state, 1);
    if (libname == nullptr)
    {
        return luaL_error(state, "Missing argument to require()");
    }

    return catch_exceptions(state, [&]{
        s->load_library(libname);
        return 0;
    });
}

void script::load_library(const std::string& libname)
{
    const std::string sanitized_libname = trim(libname);
    if (sanitized_libname.empty())
    {
        throw apolo::runtime_error("invalid library name");
    }

    const script_load_function load_fn = m_configuration.load_function();
    if (!load_fn)
    {
        throw apolo::runtime_error("cannot load libraries");
    }

    if (m_loaded_libraries.find(sanitized_libname) == m_loaded_libraries.end())
    {
        // Library hasn't been loaded yet -- load it
        m_loaded_libraries.insert(sanitized_libname);

        run(load_fn(sanitized_libname), sanitized_libname);
    }
}

int script::builtin_yield(lua_State* state)
{
    return lua_yield(state, lua_gettop(state));
}

}
