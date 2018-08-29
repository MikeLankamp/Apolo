#include "common.h"

TEST(script, empty_script)
{
    apolo::script("dummy", S(""));
}

TEST(script, syntax_error)
{
    EXPECT_THROW(apolo::script("dummy", S("x = x = x")), apolo::syntax_error);
}

