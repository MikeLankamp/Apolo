#include "common.h"
#include <optional>

namespace
{
    // The core types that can be represented by apolo::value
    enum class Type {
        Nil,
        Boolean,
        Integer,
        Float,
        String,
        Object
    };

    Type get_type(const apolo::value& value)
    {
        std::optional<Type> type;
        value.visit(overloaded{
           [&](std::nullptr_t) { type = Type::Nil; },
           [&](bool) { type = Type::Boolean; },
           [&](long long) { type = Type::Integer; },
           [&](double) { type = Type::Float; },
           [&](const std::string&) { type = Type::String; },
           [&](std::type_index, uintptr_t) { type = Type::Object; },
        });
        assert(type);
        return *type;
    }
}

TEST(value, empty)
{
    EXPECT_EQ(Type::Nil, get_type(apolo::value()));
}

TEST(value, nullptr_t)
{
    EXPECT_EQ(Type::Nil, get_type(apolo::value(nullptr)));
}

TEST(value, boolean)
{
    EXPECT_EQ(Type::Boolean, get_type(apolo::value(true)));
}

TEST(value, integers)
{
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<char>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<short>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<int>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<long>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<long long>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<unsigned char>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<unsigned short>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<unsigned int>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<unsigned long>(2))));
    EXPECT_EQ(Type::Integer, get_type(apolo::value(static_cast<unsigned long long>(2))));
}

TEST(value, floats)
{
    EXPECT_EQ(Type::Float, get_type(apolo::value(static_cast<float>(2.5))));
    EXPECT_EQ(Type::Float, get_type(apolo::value(static_cast<double>(2.5))));
    EXPECT_EQ(Type::Float, get_type(apolo::value(static_cast<long double>(2.5))));
}

TEST(value, strings)
{
    EXPECT_EQ(Type::String, get_type(apolo::value("Hello World")));
    EXPECT_EQ(Type::String, get_type(apolo::value(std::string("Hello World"))));
}

