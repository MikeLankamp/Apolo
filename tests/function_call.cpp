#include "common.h"

TEST(function_call, basic_argument_types)
{
    apolo::script script("dummy", S("function foo(x) return type(x) end"));

    EXPECT_EQ("number", script.call("foo", 0).as<std::string>());
    EXPECT_EQ("number", script.call("foo", 1).as<std::string>());
    EXPECT_EQ("number", script.call("foo", 2).as<std::string>());
    EXPECT_EQ("number", script.call("foo", 1.2).as<std::string>());
    EXPECT_EQ("boolean", script.call("foo", true).as<std::string>());
    EXPECT_EQ("boolean", script.call("foo", false).as<std::string>());
    EXPECT_EQ("string", script.call("foo", "Hello").as<std::string>());
    EXPECT_EQ("nil", script.call("foo").as<std::string>());
}

TEST(function_call, invalid_function_name)
{
    apolo::script script("dummy", S("function foo() end"));

    EXPECT_THROW(script.call("fooo"), apolo::runtime_error);
}

TEST(function_call, runtime_error_in_function)
{
    apolo::script script("dummy", S("function foo() unknown_function() end"));

    EXPECT_THROW(script.call("foo"), apolo::runtime_error);
}

TEST(function_call, call_with_yield_finishes)
{
    apolo::script script("dummy", S("function foo(x, y) yield(x,y) return x + y end"));

    auto value = script.call("foo", 1, 2);
    EXPECT_EQ(3, value.as<long long int>());
}
