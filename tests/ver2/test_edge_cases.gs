#include "test_script2.gs"

// ============================================================================
// EDGE CASES TESTS
// ============================================================================

func test_EmptyString()
{
    empty_str = "";
    return empty_str;
}



func test_VeryLargeNumbers()
{
    max_int = 2147483647;
    return max_int;
}

func test_VerySmallNumbers()
{
    min_int = -2147483648;
    return min_int;
}

func test_FloatingPointPrecision()
{
    pi = 3.14159;
    return pi;
}

func test_UnicodeStrings()
{
    unicode_str = "Привет мир";
    return unicode_str;
}
