#pragma once

#include <cstdio>
#include <string>

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

#define SME_ASSERT_WITHOUT_MESSAGE(expression) assert(expression)
#define SME_ASSERT_WITH_MESSAGE(expression, message) \
    do { \
        const bool sme_assert_condition = static_cast<bool>(expression); \
        if (!sme_assert_condition) { \
            std::fprintf(stderr, "%s\n", (message)); \
            assert(sme_assert_condition); \
        } \
    } while (false)
#define SME_ASSERT_SELECT(_1, _2, NAME, ...) NAME
#define SME_ASSERT(...) \
    SME_ASSERT_SELECT(__VA_ARGS__, SME_ASSERT_WITH_MESSAGE, SME_ASSERT_WITHOUT_MESSAGE, unused)(__VA_ARGS__)
#define SME_ASSERT_FAIL(message) \
    do { \
        std::fprintf(stderr, "%s\n", std::string(message).c_str()); \
        assert(false); \
    } while (false)
#else
#define SME_ASSERT(...) ((void)0)
#define SME_ASSERT_FAIL(message) ((void)0)
#endif
