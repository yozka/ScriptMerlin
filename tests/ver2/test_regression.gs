#include "test_script2.gs"

// ============================================================================
// REGRESSION TESTS
// ============================================================================

func test_PreviousBug1()
{
    // Test for a previously fixed bug
    a = 10;
    b = 20;
    c = a + b;
    return c + 12; // Should be 42
}

func test_PreviousBug2()
{
    // Test for another previously fixed bug
    text_str = "Hello";
    if (text_str == "Hello") {
        return "Fixed";
    }
    return "Not fixed";
}

func test_PreviousBug3()
{
    // Test for floating point bug
    a = 3.14;
    b = 0.0;
    c = a + b;
    return c;
}
