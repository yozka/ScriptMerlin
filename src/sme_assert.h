#pragma once

#include <cstdio>

#if SME_ENABLE_ASSERTS
#ifdef NDEBUG
#undef NDEBUG
#define SME_ASSERT_RESTORE_NDEBUG
#endif

#include <cassert>

#ifdef SME_ASSERT_RESTORE_NDEBUG
#define NDEBUG
#undef SME_ASSERT_RESTORE_NDEBUG
#endif

#define SME_ASSERT(expression) assert(expression)
#define SME_ASSERT_FAIL(message) \
    do { \
        std::fprintf(stderr, "%s\n", (message)); \
        assert(false); \
    } while (false)
#else
#define SME_ASSERT(expression) ((void)0)
#define SME_ASSERT_FAIL(message) ((void)0)
#endif
