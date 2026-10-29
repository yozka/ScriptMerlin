#include "test_script2.gs"

// ============================================================================
// TABLE OPERATIONS TESTS
// ============================================================================

func test_TableCreation()
{
    // Create a table
    mytable = {1, 2, 3, 4, 5};
    return "Table created successfully";
}

func test_TableAccess()
{
    mytable = {10, 20, 30, 42, 50};
    return mytable[3]; // Should return 42
}

func test_TableModification()
{
    mytable = {1, 2, 3, 4, 5};
    mytable[3] = 100;
    return mytable[3];
}

func test_TableIteration()
{
    mytable = {1, 2, 3, 4, 5};
    sum = 0;
    
    for (i = 0; i < tabsize(&mytable); i++) {
        sum += mytable[i];
    }
    
    return sum;
}

func test_TableSize()
{
    mytable = {1, 2, 3, 4, 5};
    return tabsize(&mytable);
}

func test_TableCopy()
{
    original = {10, 20, 30, 42, 50};
    copy = original;
    return copy[3];
}

func test_NestedTables()
{
    // Test table with different data types instead of nested tables
    mixed_table = {1, "hello", 3, true};
    return mixed_table[0] + mixed_table[2];
}

func process_table(table)
{
    return table[0] + table[1] + table[2];
}

func test_TableAsParameter()
{

    
    mytable = {1, 2, 3};
    return process_table(mytable);
}
