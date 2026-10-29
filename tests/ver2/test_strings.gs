#include "test_script2.gs"

// ============================================================================
// STRING OPERATIONS TESTS
// ============================================================================

func test_StringConcatenation()
{
    str1 = "Hello";
    str2 = " World";
    result = str1 + str2;
    return result;
}

func test_StringLength()
{
    str1 = "Hello World";
    return strlen(str1);
}

func test_StringSubstring()
{
    text_str = "Hello World";
    return strsub(text_str, 6, 5); // Extract "World"
}

func test_StringFind()
{
    text_str = "Hello World";
    return strpos(text_str, "World");
}

func test_StringReplace()
{
    // strreplace is not implemented in GS9, simulate it
    text_str = "Hello World";
    pos = strpos(text_str, "World");
    if (pos >= 0) {
        part1 = strsub(text_str, 0, pos);
        part2 = strsub(text_str, pos + 5, -1); // Skip "World" (5 chars)
        result = part1 + "Universe" + part2;
        return result;
    }
    return text_str;
}

func test_StringSplit()
{
    text_str = "apple,banana,orange";
    parts = strexplode(text_str, 44); // 44 is ASCII code for ','
    return tabsize(&parts);
}

func test_StringToUpper()
{
    text_str = "hello world";
    return strupr(text_str);
}

func test_StringToLower()
{
    text_str = "HELLO WORLD";
    return strlwr(text_str);
}

func test_StringTrim()
{
    text_str = "  Hello World  ";
    return strtrim(text_str, " "); // strtrim requires 2 parameters in GS9
}
