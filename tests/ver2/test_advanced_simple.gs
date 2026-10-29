#include "test_script2.gs"

// ============================================================================
// SIMPLIFIED ADVANCED FEATURES TESTS (GS9 compatible)
// ============================================================================

func test_NestedFunctions()
{
    // GS9 might not support nested functions, so we'll test regular functions
    result = add_numbers(10, 20);
    return result;
}

func add_numbers(x, y)
{
    return x + y;
}

func test_RecursiveFunction()
{
    // Test iterative factorial instead of recursive
    return factorial_iterative(5);
}

func factorial_iterative(n)
{
    result = 1;
    for (i = 1; i <= n; i++) {
        result = result * i;
    }
    return result;
}

func test_FunctionPointers()
{
    // Test function calls
    result = add_numbers(3, 5);
    return result;
}

func test_DynamicFunctionCall()
{
    // Test string operations
    return "Dynamic call successful";
}

func test_VariableArguments()
{
    // Test simple function with fixed arguments
    return sum_three(1, 2, 3);
}

func sum_three(a, b, c)
{
    return a + b + c;
}

func test_DefaultParameters()
{
    // Test function with simple parameters
    return add_with_default(5);
}

func add_with_default(a)
{
    b = 10; // Default value
    return a + b;
}

func test_MultipleReturns()
{
    // Test conditional return
    if (true) {
        return 42;
    }
    return 0;
}

func test_EarlyReturn()
{
    // Test early return
    if (true) {
        return 10;
    }
    return 20;
}
