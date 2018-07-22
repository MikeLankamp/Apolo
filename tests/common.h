#include <apolo/apolo.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

// Convenience method for turning a string literal into a character vector
inline std::vector<char> S(const char* str)
{
    return {str, str + strlen(str)};
}

// Helper types for overloading on type for visiting a value
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

namespace apolo
{
    // Method for printing contents of apolo::value for GTest error reporting
    inline void PrintTo(const value& value, ::std::ostream* os)
    {
        value.visit(overloaded{
            [&](std::nullptr_t) { *os << "nil"; },
            [&](const std::string& x) { *os << '\"' << x << '\"'; },
            [&](const auto& x) { *os << x; }
        });
    }
}
