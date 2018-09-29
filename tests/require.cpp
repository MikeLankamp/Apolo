#include "common.h"

using ::testing::Return;
using ::testing::Test;

class MockLoader
{
public:
    MOCK_METHOD1(load, apolo::script_data(const std::string&));
};

class MockObject
{
public:
    MOCK_METHOD1(dummy, void(int));
};

class require : public Test
{
public:
    require()
    {
        registry = std::make_shared<apolo::type_registry>();
        registry->load_function(std::bind(&MockLoader::load, std::ref(loader), std::placeholders::_1));
    }

protected:
    MockLoader loader;
    std::shared_ptr<apolo::type_registry> registry;
};


TEST_F(require, require_calls_load_function)
{
    EXPECT_CALL(loader, load("foo"));
    apolo::script("dummy", S("require(\"foo\")"), registry);

    EXPECT_CALL(loader, load("bar"));
    apolo::script("dummy", S("require(\"bar\")"), registry);
}

TEST_F(require, require_unique_load_same_script)
{
    EXPECT_CALL(loader, load("foo"))
        .WillOnce(Return(S("")));
    apolo::script("dummy", S("require(\"foo\") require(\"foo\") require(\" foo \")"), registry);
}

TEST_F(require, require_executes_loaded_script)
{
    MockObject obj;
    registry->add_free_function("dummy", obj, &MockObject::dummy);
    EXPECT_CALL(obj, dummy(42));
    EXPECT_CALL(loader, load("foo"))
        .WillOnce(Return(S("dummy(42)")));
    apolo::script("dummy", S("require(\"foo\")"), registry);
}

TEST_F(require, require_recursive)
{
    MockObject obj;
    registry->add_free_function("dummy", obj, &MockObject::dummy);
    EXPECT_CALL(obj, dummy(42));
    EXPECT_CALL(loader, load("bar"))
        .WillOnce(Return(S("dummy(42)")));
    EXPECT_CALL(loader, load("foo"))
        .WillOnce(Return(S("require(\"bar\")")));
    apolo::script("dummy", S("require(\"foo\")"), registry);
}

TEST_F(require, require_unique_load_recursive)
{
    EXPECT_CALL(loader, load("foo"))
        .WillOnce(Return(S("require(\"foo\")")));
    apolo::script("dummy", S("require(\"foo\")"), registry);
}

TEST_F(require, require_without_registry)
{
    EXPECT_THROW(apolo::script("dummy", S("require(\"foo\")")), apolo::runtime_error);
}

TEST_F(require, require_without_load_function)
{
    auto dummy_registry = std::make_shared<apolo::type_registry>();
    EXPECT_THROW(apolo::script("dummy", S("require(\"foo\")"), dummy_registry), apolo::runtime_error);
}

TEST_F(require, require_with_empty_string)
{
    EXPECT_THROW(apolo::script("dummy", S("require(\"\")"), registry), apolo::runtime_error);
    EXPECT_THROW(apolo::script("dummy", S("require(\" \")"), registry), apolo::runtime_error);
    EXPECT_THROW(apolo::script("dummy", S("require(\"\t\")"), registry), apolo::runtime_error);
}
