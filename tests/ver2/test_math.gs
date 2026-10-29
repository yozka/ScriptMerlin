#include "test_script2.gs"

// ============================================================================
// MATH LIBRARY TESTS
// ============================================================================

func test_SgnPositive()
{
    a = 42;
    return gs_sgn(a);
}

func test_SgnNegative()
{
    a = -42;
    return gs_sgn(a);
}

func test_SgnZero()
{
    a = 0;
    return gs_sgn(a);
}

func test_AbsPositive()
{
    a = 42;
    return gs_abs(a);
}

func test_AbsNegative()
{
    a = -42;
    return gs_abs(a);
}

func test_Floor()
{
    a = 3.7;
    return gs_floor(a);
}

func test_Ceil()
{
    a = 3.2;
    return gs_ceil(a);
}

func test_Round()
{
    a = 3.6;
    return gs_round(a);
}

func test_RoundDown()
{
    a = 3.4;
    return gs_round(a);
}

func test_Trunc()
{
    a = 3.14159;
    return gs_trunc(a, 2);
}

func test_Exp()
{
    a = 1.0;
    return gs_exp(a);
}

func test_Log()
{
    a = 2.718;
    return gs_log(a);
}

func test_Pow()
{
    a = 2.0;
    b = 3.0;
    return gs_pow(a, b);
}

func test_Sqrt()
{
    a = 16.0;
    return gs_sqrt(a);
}

func test_Sin()
{
    a = 0.0;
    return gs_sin(a);
}

func test_Cos()
{
    a = 0.0;
    return gs_cos(a);
}

func test_Tan()
{
    a = 0.0;
    return gs_tan(a);
}

func test_Asin()
{
    a = 1.0;
    return gs_asin(a);
}

func test_Acos()
{
    a = 1.0;
    return gs_acos(a);
}

func test_Atan()
{
    a = 1.0;
    return gs_atan(a);
}

func test_Atan2()
{
    a = 1.0;
    b = 1.0;
    return gs_atan2(a, b);
}

func test_Min()
{
    a = 5.0;
    b = 15.0;
    return gs_min(a, b);
}

func test_Max()
{
    a = 5.0;
    b = 15.0;
    return gs_max(a, b);
}

func test_Random()
{
    return gs_random();
}

func test_RandomRange()
{
    return gs_random(10, 20);
}
