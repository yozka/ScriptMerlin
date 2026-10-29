#include "test_script2.gs"

// ============================================================================
// ERROR HANDLING TESTS
// ============================================================================

func test_DivisionByZero()
{
    // This should be handled gracefully
    try {
        result = 10 / 0;
        return result;
    } catch {
        return 0; // Return safe value
    }
}

func test_InvalidArrayAccess()
{
    mytable = {1, 2, 3};
    // This should be handled gracefully
    try {
        value = mytable[10]; // Invalid index
        return value;
    } catch {
        return 0; // Return safe value
    }
}

func test_UndefinedFunction()
{
    // This should be handled gracefully
    try {
        result = undefined_function();
        return result;
    } catch {
        return 0; // Return safe value
    }
}

func test_TypeMismatch()
{
    // This should be handled gracefully
    try {
        text_str = "Hello";
        result = text_str + 42; // Type mismatch
        return result;
    } catch {
        return "Error handled";
    }
}

func test_StackOverflow()
{
    // This should be handled gracefully
    try {
        func recursive_func()
        {
            return recursive_func();
        }
        return recursive_func();
    } catch {
        return 0; // Return safe value
    }
}
