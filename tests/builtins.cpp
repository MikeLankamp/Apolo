#include "common.h"

TEST(builtins, os_not_available)
{
    EXPECT_THROW(apolo::script("dummy", S("os.clock()")), apolo::runtime_error);
}

TEST(builtins, base_core_available)
{
    // Check we have all allowed functions from the base lib
    EXPECT_NO_THROW(apolo::script("dummy", S("assert(true)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("ipairs({})")));
    EXPECT_NO_THROW(apolo::script("dummy", S("next({1,2,3,4}, 1)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("pairs({})")));
    EXPECT_NO_THROW(apolo::script("dummy", S("select(1,2)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("tonumber(2)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("tostring(2)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("type(2)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("type(_G[\"tostring\"])")));
    EXPECT_NO_THROW(apolo::script("dummy", S("type(_VERSION)")));
}

TEST(builtins, base_others_unavailable)
{
    // Try the most important functions
    EXPECT_THROW(apolo::script("dummy", S("dofile(\"test.lua\")")), apolo::runtime_error);
    EXPECT_THROW(apolo::script("dummy", S("load(\"return\")")), apolo::runtime_error);
    EXPECT_THROW(apolo::script("dummy", S("loadfile(\"dummy.lua\")")), apolo::runtime_error);
}

TEST(builtins, table_available)
{
    // Check we have all allowed functions from the table lib
    EXPECT_NO_THROW(apolo::script("dummy", S("table.concat({\"A\",\"B\",\"C\"})")));
    EXPECT_NO_THROW(apolo::script("dummy", S("table.insert({1,2,3,4}, 2)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("table.pack(1,2,3,4)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("table.unpack({1,2,3,4}, 1, 2)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("table.remove({1,2,3,4}, 1)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("table.move({1,2, 3}, 2, 3, 1)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("table.sort({1,2,3,4})")));
}

TEST(builtins, string_available)
{
    // Check we have a few allowed functions from the string lib
    EXPECT_NO_THROW(apolo::script("dummy", S("string.byte(\"Hello World\")")));
    EXPECT_NO_THROW(apolo::script("dummy", S("string.find(\"Hello World\", \"Hello\")")));
    EXPECT_NO_THROW(apolo::script("dummy", S("string.format(\"%d: %s\", 1, \"Hello\")")));
    EXPECT_NO_THROW(apolo::script("dummy", S("string.lower(\"Hello World\")")));
}

TEST(builtins, math_available)
{
    // Check we have a few allowed functions from the maths lib
    EXPECT_NO_THROW(apolo::script("dummy", S("math.sin(1.234)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("math.sin(math.pi)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("math.sin(1.234)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("math.ult(1, math.maxinteger)")));
}

TEST(builtins, utf8_available)
{
    // Check we have a few allowed functions from the UTF8 lib
    EXPECT_NO_THROW(apolo::script("dummy", S("utf8.char(32, 48)")));
    EXPECT_NO_THROW(apolo::script("dummy", S("utf8.codes(\"Hello World\")")));
    EXPECT_NO_THROW(apolo::script("dummy", S("utf8.len(\"Hello World\")")));
}
