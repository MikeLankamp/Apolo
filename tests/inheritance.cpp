#include "common.h"

using testing::InSequence;

namespace
{
    class Base
    {
    public:
        virtual ~Base() = default;

        virtual void base_method() = 0;
    };

    class Derived : public Base
    {
    public:
        MOCK_METHOD0(base_method, void());
        MOCK_METHOD0(derived_method, void());
    };
}

TEST(inheritance, derived_argument)
{
    const auto registry = std::make_shared<apolo::type_registry>();
    registry->add_object_type<Base>()
        .WithMethod("foo", &Base::base_method);

    registry->add_object_type<Derived>()
        .WithMethod("bar", &Derived::derived_method)
        .WithBase<Base>();

    const auto derived = std::make_shared<Derived>();

    {
        InSequence s;
        EXPECT_CALL(*derived, base_method());
        EXPECT_CALL(*derived, derived_method());
    }

    apolo::script script("dummy", S("function test(x) x:foo() x:bar() end"), registry);
    script.call("test", derived);
}

TEST(inheritance, base_argument)
{
    const auto registry = std::make_shared<apolo::type_registry>();
    registry->add_object_type<Base>()
        .WithMethod("foo", &Base::base_method);

    registry->add_object_type<Derived>()
        .WithMethod("bar", &Derived::derived_method)
        .WithBase<Base>();

    const auto derived = std::make_shared<Derived>();

    {
        InSequence s;
        EXPECT_CALL(*derived, base_method());
    }

    const auto base = std::static_pointer_cast<Base>(derived);

    apolo::script script("dummy", S("function test(x) x:foo() end"), registry);
    script.call("test", base);
}

#if GTEST_HAS_DEATH_TEST && !defined(NDEBUG)
TEST(inheritance, register_method_twice_base_first)
{
    apolo::type_registry registry;
    registry.add_object_type<Base>()
        .WithMethod("foo", &Base::base_method);

    auto& desc = registry.add_object_type<Derived>();
    desc.WithBase<Base>();
    EXPECT_DEATH(desc.WithMethod("foo", &Derived::derived_method), "register_method");
}

TEST(inheritance, register_method_twice_base_last)
{
    apolo::type_registry registry;
    registry.add_object_type<Base>()
        .WithMethod("foo", &Base::base_method);

    auto& desc = registry.add_object_type<Derived>();
    desc.WithMethod("foo", &Derived::derived_method);
    EXPECT_DEATH(desc.WithBase<Base>(), "register_method");
}
#endif
