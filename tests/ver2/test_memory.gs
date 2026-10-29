#include "test_script2.gs"

// ============================================================================
// MEMORY MANAGEMENT TESTS
// ============================================================================

func test_LargeStringAllocation()
{
    result = "";
    for (i = 0; i < 10000; i++) {
        result += "x";
    }
    return result;
}

func test_LargeTableAllocation()
{
    mytable = {};
    // Сначала создаем таблицу нужного размера
    tabresize(&mytable, 100);
    // Затем заполняем значениями
    for (i = 0; i < 100; i++) {
        mytable[i] = "test string " + (str)i;
    }
    return tabsize(&mytable);
}

func test_MemoryLeakPrevention()
{
    // Test that memory is properly cleaned up
    for (j = 0; j < 100; j++) {
        temp_table = {1, 2, 3, 4, 5};
        temp_string = "temporary string";
    }
    return 42; // Should still work after many allocations
}
