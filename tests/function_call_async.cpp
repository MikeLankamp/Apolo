#include "common.h"

TEST(function_call_async, invalid_function_name)
{
    apolo::script script("dummy", S("function foo() end"));

    apolo::cooperative_executor executor;
    EXPECT_THROW(script.call_async(executor, "fooo"), apolo::runtime_error);
}

TEST(function_call_async, runtime_error_in_function)
{
    apolo::script script("dummy", S("function foo() unknown_function() end"));

    apolo::cooperative_executor executor;
    auto future = script.call_async(executor, "foo", 1, 2);
    EXPECT_NO_THROW(executor.run());
    EXPECT_THROW(future.get(), apolo::runtime_error);
}

TEST(function_call, call_async_with_yield_finishes)
{
    apolo::script script("dummy", S("function foo(x, y) yield(x,y) return x + y end"));

    apolo::cooperative_executor executor;
    auto future = script.call_async(executor, "foo", 1, 2);
    executor.run();
    EXPECT_EQ(3, future.get().as<long long int>());
}
