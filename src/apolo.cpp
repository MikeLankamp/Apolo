#include <apolo/apolo.h>

namespace apolo
{

namespace
{
    int lua_trampoline(lua_State* state)
    {
        try
        {
            auto callback = static_cast<detail::lua_callback*>(lua_touserdata(state, lua_upvalueindex(1)));
            return callback->invoke(*state);
        }
        catch (std::exception& ex)
        {
            lua_pushstring(state, ex.what());
            lua_error(state);
        }
        return 0;
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

void type_registry::add_object_type(object_type_info info)
{
    auto index = info.m_typeIndex;

    // Check the type hasn't been registered yet
    assert(m_object_types.find(index) == m_object_types.end());

    m_object_types.emplace(index, std::move(info));
}

const object_type_info* type_registry::get_object_type(std::type_index typeIndex) const
{
    auto it = m_object_types.find(typeIndex);
    return (it != m_object_types.end()) ? &it->second : nullptr;
}

script::script(const std::string& name, const std::vector<char>& buffer, std::shared_ptr<type_registry> registry)
    : m_registry(std::move(registry))
    , m_state(create_lua_state())
{
    if (m_registry != nullptr)
    {
        // Register the free functions as global functions before executing
        for (const auto& [name, callback] : m_registry->free_functions())
        {
            lua_pushlightuserdata(m_state.get(), callback.get());
            lua_pushcclosure(m_state.get(), &lua_trampoline, 1);
            lua_setglobal(m_state.get(), name.c_str());
        }
    }

    // Load script into state
    switch (luaL_loadbuffer(m_state.get(), buffer.data(), buffer.size(), name.c_str()))
    {
    case 0:
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
    case 0:
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

    for (const auto& [name, callback] : info->m_methods)
    {
        lua_pushlightuserdata(&state, callback.get());
        lua_pushcclosure(&state, &lua_trampoline, 1);
        lua_setfield(&state, -2, name.c_str());
    }
}

}
