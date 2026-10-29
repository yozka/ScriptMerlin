#include "test_script2.gs"

// ============================================================================
// PERFORMANCE TESTS
// ============================================================================

func test_LargeLoop()
{
    sum = 0;
    for (i = 0; i < 100000; i++) {
        sum += 1;
    }
    return sum;
}

func test_StringOperations()
{
    result = "";
    for (i = 0; i < 1000; i++) {
        result += "a";
    }
    return result;
}

func test_TableOperations()
{
    mytable = {};
    // Сначала создаем таблицу нужного размера
    tabresize(&mytable, 100);
    // Затем заполняем значениями
    for (i = 0; i < 100; i++) {
        mytable[i] = i;
    }
    return tabsize(&mytable);
}

func simple_func()
{
    return 1;
}

func test_FunctionCalls()
{
    sum = 0;
    for (i = 0; i < 1000; i++) {
        sum += simple_func();
    }
    return sum;
}

int perf_mark;

func perf_mix(n)
{
    if (n < 0) return 0;
    if (n == 0) return 2;
    return (n * 3 + 1) % 97;
}

// Один проход трогает арифметику, биты, сравнения, ветки, float, строки,
// таблицу, ссылки, глобал, вызов скрипта, call по индексу, нативы и хост.
func test_Workload()
{
    obj = newobj<TStruct>();
    obj.test_int = 1;
    obj.test_str = "x";
    rows = tab(8);
    text = "seed";
    sum = 0;
    perf_mark = 0;
    fid = gs_fid("perf_mix");
    gid = gs_gid("perf_mark");
    for (i = 0; i < 1000; i++) {
        n = i % 64;
        acc = ((n + 5 - 1) * 3 / 2) % 17;
        if (n % 2 == 0) {
            acc += 1;
        } else {
            acc -= 1;
        }
        bits = ((n & 15) | 4) ^ 1;
        bits = (bits << 2) >> 1;
        flipped = ~n & 255;
        cmp = (n < 32) + (n <= 32) + (n > 32) + (n >= 32) + (n == 0) + (n != 1);
        logic = (n > 0 && n < 50) || (n > 60);
        f = ((float)n + 0.25) * 1.5;
        whole = (int)floor(f);
        text = "n" + (str)(n % 10);
        text[[0]] = 'N';
        rows[n % 8] = acc;
        slot = &(rows[n % 8]);
        *slot += 1;
        var(gid) = n % 5;
        gref = &perf_mark;
        *gref += 1;
        obj.test_int = n;
        obj.test_str = text;
        added = obj.add(acc, whole);
        obj.run();
        called = perf_mix(n - 70);
        indirect = call(n % 20, fid);
        native = abs(0 - n) + gs_abs(n) + gs_sgn(n - 32) + (int)sqrt((float)(n + 1));
        spins = 0;
        left = n % 3;
        while (spins < left) {
            spins++;
        }
        once = 0;
        do once++; while(0);
        kind = typeof(rows) + typeof(text) + typeof(f) + typeof(NULL) + ?obj;
        sum += acc + bits + flipped + cmp + logic + whole + rows[n % 8] + added;
        sum += called + indirect + native + spins + once + kind + perf_mark + obj.test_int;
        sum += strlen(text) + strlen(obj.test_out);
    }
    return sum;
}
