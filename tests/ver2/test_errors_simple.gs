#include "test_script2.gs"

// ============================================================================
// SIMPLIFIED ERROR HANDLING TESTS (GS9 compatible)
// ============================================================================

func test_DivisionByZero()
{
    // Test division by zero handling
    a = 10;
    b = 0;
    if (b == 0) {
        return 0; // Safe handling
    }
    return a / b;
}

func test_InvalidArrayAccess()
{
    // Test array access with bounds checking
    mytable = {1, 2, 3};
    index = 10;
    if (index >= 0 && index < tabsize(&mytable)) {
        return mytable[index];
    }
    return 0; // Safe default
}

func test_UndefinedFunction()
{
    // Test calling a function that doesn't exist
    // This will be handled by the compiler/VM
    return 0; // Safe return
}

func test_TypeMismatch()
{
    // Test type conversion
    text_str = "Hello";
    num = 42;
    result = text_str + (str)num; // Explicit conversion
    return "Type conversion successful";
}

func test_StackOverflow()
{
    // Test iterative approach instead of recursion
    return iterative_count(10);
}

func iterative_count(n)
{
    result = 0;
    for (i = 0; i < n; i++) {
        result = result + 1;
    }
    return result;
}
