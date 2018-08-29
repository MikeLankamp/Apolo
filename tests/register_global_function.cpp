#include "common.h"

namespace
{
    class Mock
    {
    public:
      MOCK_METHOD0(non_const_member, void());
      MOCK_CONST_METHOD0(const_member, void());
    };

    bool s_called = false;

    void SetCalled()
    {
        s_called = true;
    }
}

TEST(register_global_function, no_registry)
{
    EXPECT_THROW(apolo::script("dummy", S("foo()")), apolo::runtime_error);
}

TEST(register_global_function, free_function)
{
    s_called = false;
    auto registry = std::make_shared<apolo::type_registry>();
    registry->add_free_function("foo", &SetCalled);
    apolo::script("dummy", S("foo()"), registry);
    EXPECT_TRUE(s_called);
}

TEST(register_global_function, member_function)
{
    Mock mock;
    auto registry = std::make_shared<apolo::type_registry>();
    registry->add_free_function("foo", mock, &Mock::non_const_member);
    EXPECT_CALL(mock, non_const_member());
    apolo::script("dummy", S("foo()"), registry);
}

TEST(register_global_function, const_member_function)
{
    const Mock mock;
    auto registry = std::make_shared<apolo::type_registry>();
    registry->add_free_function("foo", mock, &Mock::const_member);
    EXPECT_CALL(mock, const_member());
    apolo::script("dummy", S("foo()"), registry);
}

TEST(register_global_function, lambda)
{
    bool called = false;
    auto registry = std::make_shared<apolo::type_registry>();
    registry->add_free_function("foo", [&]() -> void { called = true; });
    apolo::script("dummy", S("foo()"), registry);
    EXPECT_TRUE(called);
}

TEST(register_global_function, exception_in_function)
{
    auto registry = std::make_shared<apolo::type_registry>();
    registry->add_free_function("foo", [&]() -> void
    {
        throw std::runtime_error("");
    });
    EXPECT_THROW(apolo::script("dummy", S("foo()"), registry), apolo::runtime_error);
}
