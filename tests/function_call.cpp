#include "common.h"

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
