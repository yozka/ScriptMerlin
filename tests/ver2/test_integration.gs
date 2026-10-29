#include "test_script2.gs"

// ============================================================================
// INTEGRATION TESTS
// ============================================================================

func test_CompleteGameFlow()
{
    // Simulate a complete game flow
    player_x = 100;
    player_y = 200;
    health = 100;
    score = 0;
    
    // Move player
    player_x += 10;
    
    // Check collision
    if (player_x > 150) {
        health -= 10;
    }
    
    // Update score
    score += 100;
    
    // Check win condition
    if (score >= 100 && health > 0) {
        return 1; // Game flow completed
    }
    return 0;
}

func test_MultipleSystems()
{
    // Test multiple systems working together
    
    // Math system
    result1 = 10 + 5;
    
    // String system
    result2 = "Hello" + " World";
    
    // Table system
    table = {result1, 42, 100};
    
    // File system (simulated)
    file_handle = 1;
    
    if (result1 == 15 && result2 == "Hello World" && tabsize(&table) == 3 && file_handle > 0) {
        return 1; // All systems working
    }
    return 0;
}

func test_StressTest()
{
    // Stress test multiple operations
    sum = 0;
    str_result = "";
    table = {};
    tabresize(&table, 100); // Pre-allocate table with 100 elements
    
    for (i = 0; i < 100; i++) {
        sum += i;
        str_result += (str)i;
        table[i] = i * 2;
    }
    
    if (sum == 4950 && strlen(str_result) > 0 && tabsize(&table) == 100) {
        return 1; // Stress test passed
    }
    return 0;
}
