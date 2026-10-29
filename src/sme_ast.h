#pragma once

#include <memory>
#include <string>
#include <vector>

namespace sme {

// Операции AST. And/Or — это &&/|| и слова and/or. Короткой схемы нет:
// компилятор всегда вычисляет обе стороны и ставит LogicAnd/LogicOr.
enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,
    BitAnd, BitOr, BitXor, Shl, Shr
};

enum class UnaryOp { Neg, Not, BitNot };

// Узел выражения. Один struct на все виды: kind говорит, какие поля живые.
// Это проще визитора, но легко забыть заполнить поле — компилятор тогда
// молча возьмёт ноль или пустую строку.
struct Expr {
    int line = 1;
    enum class Kind {
        Int, Float, Bool, Null, String,
        Name, Binary, Unary, Call, Index, CharIndex, Member, List, Assign,
        Modify, Ternary, Cast, Addr, Deref, PostInc, PostDec, NewObj, Query
        // Bool парсер не создаёт: true/false становятся Int 1/0.
        // Query — это ?имя. Компилируется как код типа, не как «аргумент передали».
        // CharIndex — синтаксис s[[i]], байт строки, а не s[i] (строка из символа).
        // Call в левой части присваивания — это var(индекс), ссылка на глобал.
    } kind = Kind::Null;

    BinaryOp binary = BinaryOp::Add;
    UnaryOp unary = UnaryOp::Not;
    int intValue = 0;
    float floatValue = 0.f;
    bool boolValue = false;
    bool prefix = false; // ++x, а не x++
    std::string text;    // имя, текст строки, тип каста или имя типа newobj
    std::string format;  // формат printf для каста, например "%02i" или "%.2f"
    std::vector<std::unique_ptr<Expr>> kids;
};

// Оператор. Блок не создаёт область видимости: все локалы функции
// собираются заранее и живут весь вызов. For — цикл i = a .. b,
// ForC — классический for(;;).
struct Stmt {
    int line = 1;
    enum class Kind {
        Var, If, While, For, Do, ForC, Return, Break, Continue, Expr,
        Block, Goto, Label, Stop
    } kind = Kind::Expr;
    std::string name;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> expr2; // конец диапазона for или условие for(;;)
    std::unique_ptr<Expr> expr3; // шаг классического for
    std::vector<std::unique_ptr<Stmt>> body;
    std::vector<std::unique_ptr<Stmt>> elseBody;
    bool hasElse = false;
};

struct Function {
    int line = 1;
    std::string name;
    std::vector<std::string> params;
    std::vector<std::unique_ptr<Stmt>> body;
};

// Вид глобала из объявления int/float/str/tab/ref/ptr/var на верхнем уровне.
// Начальное значение задаёт #def, не «int x = 1» — такой инициализатор парсер отвергает.
enum class GlobalKind { Dynamic, Int, Float, Str, Tab, Ref, Ptr };

struct GlobalDecl {
    std::string name;
    GlobalKind kind = GlobalKind::Dynamic;
};

struct Program {
    std::vector<GlobalDecl> globals;
    std::vector<Function> functions;
};

}  // пространство имён sme
