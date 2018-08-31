#include "common.h"

using testing::InSequence;
using testing::StrictMock;

namespace
{
    class Mock
    {
    public:
      MOCK_METHOD0(non_const_member, void());
      MOCK_CONST_METHOD0(const_member, void());
    };
}

TEST(register_simple_object, no_registry)
{
    apolo::script script("dummy", S("function test(x) x:foo() end"));
    const auto mock = std::static_pointer_cast<Mock>(std::make_shared<StrictMock<Mock>>());
    EXPECT_THROW(script.call("test", mock), apolo::runtime_error);
}

#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
TEST(register_simple_object, register_method_twice)
{
    apolo::type_registry registry;
    auto& desc = registry.add_object_type<Mock>();
    desc.WithMethod("foo", &Mock::const_member);
    desc.WithMethod("bar", &Mock::non_const_member);
    EXPECT_DEATH(desc.WithMethod("foo", &Mock::const_member), "register_method");
    EXPECT_DEATH(desc.WithMethod("bar", &Mock::non_const_member), "register_method");
}
#endif

TEST(register_simple_object, basic)
{
    const auto registry = std::make_shared<apolo::type_registry>();
    registry->add_object_type<Mock>()
      .WithMethod("foo", &Mock::const_member)
      .WithMethod("bar", &Mock::non_const_member);

    const auto mock = std::static_pointer_cast<Mock>(std::make_shared<StrictMock<Mock>>());
    {
        InSequence s1;
        EXPECT_CALL(*mock, const_member());
        EXPECT_CALL(*mock, non_const_member());
    }

    // Scope the lifetime of the script to force cleanup before checking the mock's use count
    {
        apolo::script script("dummy", S("function test(x) x:foo() x:bar() end"), registry);
        script.call("test", mock);
    }

    // We should have only this reference left afterwards
    EXPECT_EQ(1, mock.use_count());
}

TEST(register_simple_object, call_method_with_invalid_self)
{
    const auto registry = std::make_shared<apolo::type_registry>();
    registry->add_object_type<Mock>()
      .WithMethod("foo", &Mock::const_member);

    apolo::script script("dummy", S("function test(x) x.foo(2) end"), registry);

    const auto mock = std::make_shared<Mock>();
    EXPECT_THROW(script.call("test", mock), apolo::runtime_error);
}
