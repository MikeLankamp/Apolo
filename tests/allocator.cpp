
#include "common.h"
#include <cassert>
#include <cstdlib>

using testing::Test;

static std::size_t num_global_allocs {0};

extern "C" void* malloc (size_t size)
{
    return nullptr;
}

/*void* operator new(std::size_t bytes)
{
    ++num_global_allocs;
    return std::malloc(bytes);
}

void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    // Use std::aligned_alloc when its more widely supported
    assert(static_cast<std::size_t>(alignment) <= alignof(std::max_align_t));
    ++num_global_allocs;
    return std::malloc(bytes);
}

void operator delete(void* ptr)
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t)
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t, std::align_val_t)
{
    std::free(ptr);
}*/

class test_memory_resource : public apolo::memory_resource
{
private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        // Use std::aligned_alloc when its more widely supported
        assert(alignment <= alignof(std::max_align_t));
        return std::malloc(bytes);
    }

    void do_deallocate(void* p, std::size_t, std::size_t) override
    {
        std::free(p);
    }

    bool do_is_equal(const memory_resource& other) const noexcept override
    {
        return &other == this;
    }
};

class allocator : public Test
{
public:
    allocator()
    {
        num_global_allocs = 0;
        m_configuration.memory_resource(&m_resource);
    }

protected:
    template <typename Callable>
    static void expect_no_allocations(const Callable& callable)
    {
        auto allocs = num_global_allocs;
        callable();
        EXPECT_EQ(allocs, num_global_allocs);
    }

    test_memory_resource m_resource;
    apolo::configuration m_configuration;
};

TEST_F(allocator, construction)
{
    expect_no_allocations([this]{
        apolo::script("dummy", S(""), m_configuration);
    });
}
