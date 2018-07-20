#include <apolo/apolo.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace
{
    class Mock
    {
    public:
      MOCK_METHOD0(non_const_member, void());
      MOCK_CONST_METHOD0(const_member, void());
    };

    std::vector<char> S(const char* str)
    {
        return {str, str + strlen(str)};
    }

    bool s_called = false;

    void SetCalled()
    {
        s_called = true;
    }
}

TEST(register_global_function, free_function)
{
    s_called = false;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", &SetCalled);
    engine.CreateScript("dummy", S("foo()"));
    EXPECT_TRUE(s_called);
}

TEST(register_global_function, member_function)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::non_const_member);
    EXPECT_CALL(mock, non_const_member());
    engine.CreateScript("dummy", S("foo()"));
}

TEST(register_global_function, const_member_function)
{
    const Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::const_member);
    EXPECT_CALL(mock, const_member());
    engine.CreateScript("dummy", S("foo()"));
}

TEST(register_global_function, lambda)
{
    bool called = false;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", [&]() -> void { called = true; });
    engine.CreateScript("dummy", S("foo()"));
    EXPECT_TRUE(called);
}
