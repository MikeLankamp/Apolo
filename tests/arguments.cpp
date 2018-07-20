#include "common.h"
#include <gmock/gmock.h>

namespace
{
    class Mock
    {
    public:
      MOCK_METHOD0(non_const_member, void());
      MOCK_CONST_METHOD0(const_member, void());

      MOCK_METHOD1(args_integer, void(int));
      MOCK_METHOD1(args_string, void(const std::string&));
      MOCK_METHOD5(args_signed_integers, void(char, short, int, long, long long));
      MOCK_METHOD5(args_unsigned_integers, void(unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long));
      MOCK_METHOD2(args_floats, void(float, double));

      MOCK_METHOD2(args_variable, void(int, std::vector<apolo::value>&));
    };
}

TEST(arguments, arguments_signed_integers)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_signed_integers);
    EXPECT_CALL(mock, args_signed_integers(1, 2, 3, 4, 5));
    engine.CreateScript("dummy", S("foo(1,2,3,4,5)"));
}

TEST(arguments, arguments_unsigned_integers)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_unsigned_integers);
    EXPECT_CALL(mock, args_unsigned_integers(1, 2, 3, 4, 5));
    engine.CreateScript("dummy", S("foo(1,2,3,4,5)"));
}

TEST(arguments, arguments_float)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_floats);
    EXPECT_CALL(mock, args_floats(1.5f, 2.5));
    engine.CreateScript("dummy", S("foo(1.5,2.5)"));
}

TEST(arguments, arguments_string)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_string);
    EXPECT_CALL(mock, args_string("Hello World"));
    engine.CreateScript("dummy", S("foo(\"Hello World\")"));
}

TEST(arguments, too_few_arguments)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_string);
    EXPECT_THROW(engine.CreateScript("dummy", S("foo()")), apolo::runtime_error);
}

TEST(arguments, too_many_arguments)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_string);
    EXPECT_THROW(engine.CreateScript("dummy", S("foo(\"Hello World\", \"Hi\")")), apolo::runtime_error);
}

TEST(arguments, invalid_argument_types)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_string);
    EXPECT_THROW(engine.CreateScript("dummy", S("foo(2)")), apolo::runtime_error);
}

TEST(arguments, no_implicit_conversion_from_string_to_number)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_integer);
    EXPECT_THROW(engine.CreateScript("dummy", S("foo(\"2\")")), apolo::runtime_error);
}

TEST(arguments, no_implicit_conversion_from_number_to_string)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_string);
    EXPECT_THROW(engine.CreateScript("dummy", S("foo(2)")), apolo::runtime_error);
}

TEST(arguments, variable_arguments)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_variable);
    std::vector<apolo::value> expected_args{{"Hi"}, {2}, {4.51}};
    EXPECT_CALL(mock, args_variable(42, expected_args));
    engine.CreateScript("dummy", S("foo(42, \"Hi\", 2, 4.51)"));
}

TEST(arguments, empty_variable_arguments)
{
    Mock mock;
    apolo::ScriptEngine engine;
    engine.RegisterGlobalFunction("foo", mock, &Mock::args_variable);
    std::vector<apolo::value> expected_args;
    EXPECT_CALL(mock, args_variable(42, expected_args));
    engine.CreateScript("dummy", S("foo(42)"));
}
