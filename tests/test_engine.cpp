#include "sme/sme_engine.h"
#include "sme/sme_error.h"
#include "sme/sme_object.h"
#include "sme/smelib_default.h"
#include "sme/smelib_gs.h"
#include "sme/smelib_math.h"

// Спецификация языка. Каждый тест гоняет .gs через Engine и сверяет результат.
// Поведение, которое здесь зафиксировано, менять без правки теста нельзя:
// нет короткой схемы &&/||, >> логический, sizeof строки на 1 больше длины,
// блоки не создают область видимости.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

TEST(ErrorResult, Void) {
    sme::error::Result<void> success;
    EXPECT_FALSE(success.error);
    EXPECT_TRUE(static_cast<bool>(success));

    sme::error::Result<void> failure(sme::error::Error(
        sme::error::Code::UNKNOWN, "operation failed"));
    ASSERT_TRUE(failure.error);
    EXPECT_EQ(failure.error->Message(), "operation failed");
    EXPECT_FALSE(static_cast<bool>(failure));
}

TEST(ErrorResult, Value) {
    sme::error::Result<sme::Value> success(sme::Value::Integer(42));
    EXPECT_FALSE(success.error);
    EXPECT_TRUE(static_cast<bool>(success));
    EXPECT_TRUE(success.IsInteger());
    EXPECT_EQ(success.ToInteger(), 42);

    sme::error::Result<sme::Value> failure(sme::Value::Null(), sme::error::Error(
        sme::error::Code::UNKNOWN, "operation failed"));
    ASSERT_TRUE(failure.error);
    EXPECT_FALSE(static_cast<bool>(failure));

    EXPECT_TRUE((std::is_base_of_v<sme::Value, sme::error::Result<sme::Value>>));
}

TEST(RuntimeError, CallbackErrorReturnsLocationAndStack) {
    sme::Engine engine;
    engine.RegisterFn("fail", [](sme::Engine& callbackEngine, sme::Arguments) {
        callbackEngine.RuntimeError(sme::error::Code::VM_EXECUTION_FAILURE, "callback failed");
        return sme::Value::Integer(99);
    });
    ASSERT_FALSE(engine.LoadSource("func main() {\n  fail();\n  return 42;\n}\n"));

    auto result = engine.Call("main");
    ASSERT_TRUE(result.error);
    EXPECT_EQ(result.error->Code(), sme::error::Code::VM_EXECUTION_FAILURE);
    EXPECT_EQ(result.error->Message(), "callback failed");
    EXPECT_EQ(result.error->Location().function, "main");
    EXPECT_EQ(result.error->Location().line, 2);
    ASSERT_FALSE(result.error->Stack().empty());
    EXPECT_EQ(result.error->Stack().back().function, "main");
}

TEST(RuntimeError, FirstErrorWinsUntilControlCheckpoint) {
    sme::Engine engine;
    ASSERT_FALSE(engine.LoadSource("func main() {\n  var first = 1 / 0;\n  var second = 2 / 0;\n  return 42;\n}\n"));

    auto result = engine.Call("main");
    ASSERT_TRUE(result.error);
    EXPECT_EQ(result.error->Code(), sme::error::Code::VM_DIVISION_BY_ZERO);
    EXPECT_EQ(result.error->Location().line, 2);
}

TEST(RuntimeError, IgnoredNestedCallErrorStopsOuterScript) {
    sme::Engine engine;
    bool continued = false;
    engine.RegisterFn("nested", [](sme::Engine& callbackEngine, sme::Arguments) {
        (void)callbackEngine.Call("inner");
        return sme::Value::Null();
    });
    engine.RegisterFn("continued", [&continued](sme::Engine&, sme::Arguments) {
        continued = true;
        return sme::Value::Null();
    });
    ASSERT_FALSE(engine.LoadSource(R"(
        func inner() { return 1 / 0; }
        func outer() {
            nested();
            continued();
            return 42;
        }
    )"));

    auto result = engine.Call("outer");
    ASSERT_TRUE(result.error);
    EXPECT_EQ(result.error->Code(), sme::error::Code::VM_DIVISION_BY_ZERO);
    EXPECT_FALSE(continued);
}

TEST(RuntimeError, ResizeTableUsesRuntimeError) {
    sme::Engine engine;
    sme::library::InstallDefault(engine);
    ASSERT_FALSE(engine.LoadSource("func main() { tabresize(1, 2); return 42; }"));

    auto result = engine.Call("main");
    ASSERT_TRUE(result.error);
    EXPECT_EQ(result.error->Code(), sme::error::Code::ENGINE_TABRESIZE_EXPECTS_TABLE);
}

TEST(Engine, HostConstantsAreReadOnlyAndSetGlobalStaysMutable) {
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    engine.SetGlobal("counter", sme::Value::Integer(3));
    engine.RegisterInteger("APP_VERSION", 7);
    engine.RegisterString("APP_NAME", "Ecovale");
    ASSERT_FALSE(engine.LoadSource(R"(
        func main() {
            counter += 2;
            return counter + APP_VERSION;
        }
        func appName() { return APP_NAME; }
    )"));

    auto result = engine.Call("main");
    ASSERT_FALSE(result.error);
    EXPECT_EQ(result.ToInteger(), 12);
    engine.SetGlobal("counter", sme::Value::Integer(10));
    result = engine.Call("main");
    ASSERT_FALSE(result.error);
    EXPECT_EQ(result.ToInteger(), 19);

    auto appName = engine.Call("appName");
    ASSERT_FALSE(appName.error);
    EXPECT_EQ(appName.CastString(), "Ecovale");
    EXPECT_FALSE(engine.FindGlobal("APP_VERSION"));
    EXPECT_FALSE(engine.FindGlobal("APP_NAME"));
}

TEST(Engine, HostConstantsCannotBeAssigned) {
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    engine.RegisterInteger("MAX_COUNT", 10);

    auto error = engine.LoadSource("func main() { MAX_COUNT = 11; }");
    ASSERT_TRUE(error);
    EXPECT_EQ(error->Code(), sme::error::Code::COMPILATION_INVALID_ASSIGNMENT_TARGET);
    EXPECT_NE(error->Message().find("cannot assign to constant"), std::string::npos);
}

std::string scriptPath(const char* relative) {
    return std::string(MLN_TESTS_DIR) + "/" + relative;
}

sme::Engine& cachedEngine(const char* relative)
{
    static std::unordered_map<std::string, std::unique_ptr<sme::Engine>> cache;
    auto& slot = cache[relative];
    if (!slot) {
        auto engine = std::make_unique<sme::Engine>();
        engine->SetFileSystem(sme::MakeStdFileSystem());
        sme::library::InstallDefault(*engine);
		sme::library::InstallMath(*engine);
		sme::library::InstallGS(*engine);
        EXPECT_FALSE(engine->Load(scriptPath(relative)).has_value());
        slot = std::move(engine);
    }
    return *slot;
}

struct Result {
    sme::Engine* engine = nullptr;
    sme::Value value;

    std::string Text() const { return value.CastString(); }
    int Integer() const {
        if (const auto* number = std::get_if<int>(&value)) return *number;
        ADD_FAILURE() << "expected int, got " << value.TypeName() << " " << value.CastString();
        return 0;
    }
    float Floating() const {
        if (const auto* number = std::get_if<float>(&value)) return *number;
        if (const auto* number = std::get_if<int>(&value)) return static_cast<float>(*number);
        ADD_FAILURE() << "expected float, got " << value.TypeName();
        return 0.f;
    }
    bool Boolean() const {
        if (const auto* number = std::get_if<int>(&value)) return *number != 0;
        ADD_FAILURE() << "expected int, got " << value.TypeName();
        return false;
    }
};

sme::Value callValue(sme::Engine& engine, std::string_view function) {
    auto callResult = engine.Call(function);
    if (callResult.error) {
        ADD_FAILURE() << callResult.error->Message();
        return sme::Value::Null();
    }
    return std::move(callResult);
}

Result run(const char* file, const char* function) {
    Result result;
    result.engine = &cachedEngine(file);
    result.value = callValue(*result.engine, function);
    return result;
}

class TStruct : public sme::IScriptObject {
public:
    static int alive;

    TStruct() { ++alive; }
    TStruct(std::string str, int n)
        : _testInt(n)
        , _testStr(std::move(str))
    {
        ++alive;
    }
    ~TStruct() override { --alive; }

    sme::Value GetProperty(sme::Engine& engine, std::string_view name) const override {
        if (name == "test_int") return sme::Value::Integer(_testInt);
        if (name == "test_str") return sme::Value::String(_testStr);
        if (name == "test_out") return sme::Value::String(_testOut);
        engine.RuntimeError(sme::error::Code::OBJECT_UNKNOWN_PROPERTY,
            "unknown property '" + std::string(name) + "'");
        return sme::Value::Null();
    }

    void SetProperty(sme::Engine& engine, std::string_view name, const sme::Value& value) override {
        if (name == "test_int" && value.IsInteger()) {
            _testInt = std::get<int>(value);
            return;
        }
        if ((name == "test_str" || name == "test_out") && value.IsString()) {
            std::string text = value.ToString();
            if (name == "test_str") _testStr = std::move(text);
            else _testOut = std::move(text);
            return;
        }
        engine.RuntimeError(sme::error::Code::OBJECT_UNKNOWN_PROPERTY,
            "unknown property '" + std::string(name) + "'");
    }

    sme::Value Call(sme::Engine& engine, std::string_view name, sme::Arguments args) override {
        if (name == "run") {
            _testOut = _testStr + std::to_string(_testInt);
            return sme::Value::Null();
        }
        if (name == "join" && args.size() == 2 && args[0].IsInteger() && args[1].IsString()) {
            return sme::Value::String(std::to_string(std::get<int>(args[0])) + args[1].ToString());
        }
        if (name == "add" && args.size() == 2 && args[0].IsInteger() && args[1].IsInteger()) {
            return sme::Value::Integer(std::get<int>(args[0]) + std::get<int>(args[1]));
        }
        engine.RuntimeError(sme::error::Code::OBJECT_UNKNOWN_METHOD,
            "unknown method '" + std::string(name) + "'");
        return sme::Value::Null();
    }

private:
    int _testInt = 0;
    std::string _testStr;
    std::string _testOut;
};

int TStruct::alive = 0;

std::shared_ptr<TStruct> makeTStruct(sme::Engine&, std::span<const sme::Value> args) {
    if (args.size() >= 2 && args[0].IsString() && args[1].IsInteger()) {
        return std::make_shared<TStruct>(args[0].ToString(), std::get<int>(args[1]));
    }
    return std::make_shared<TStruct>();
}

}  // анонимное пространство имён

TEST(Variables, String)
{
    Result script = run("ver2/test_script.gs", "test_VariableString");
    EXPECT_EQ(script.Text(), "Test String");
}

TEST(Variables, Integer)
{
    Result script = run("ver2/test_script.gs", "test_VariableInteger");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(Variables, Float)
{
    Result script = run("ver2/test_script.gs", "test_VariableFloat");
    EXPECT_FLOAT_EQ(script.Floating(), 76.57f);
}

TEST(Variables, Bool)
{
    Result script = run("ver2/test_script.gs", "test_VariableBool");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(Variables, Binary)
{
    Result script = run("ver2/test_script.gs", "test_VariableBinary");
    EXPECT_EQ(script.Integer(), 0b01010110);
}

TEST(Variables, Hexadecimal)
{
    Result script = run("ver2/test_script.gs", "test_VariableHexadecimal");
    EXPECT_EQ(script.Integer(), 0xFA73);
}

TEST(Logic, NOT)
{
    Result script = run("ver2/test_script.gs", "test_LogicNOT");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(Logic, AND)
{
    Result script = run("ver2/test_script.gs", "test_LogicAND");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(Logic, OR)
{
    Result script = run("ver2/test_script.gs", "test_LogicOR");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(Logic, FusedComparisonKeepsIntegerPrecision)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    engine.LoadSource(
        "func compare_large_integers() {\n"
        "    a = 16777217;\n"
        "    b = 16777216;\n"
        "    if (a > b) return 1;\n"
        "    return 0;\n"
        "}\n");
    EXPECT_EQ(std::get<int>(callValue(engine, "compare_large_integers")), 1);
}

TEST(Parser, FnIsNotFunctionKeyword)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    auto errors = engine.LoadSource("fn old_function() { return 1; }");
    ASSERT_TRUE(errors.has_value());
    EXPECT_EQ(errors->Code(), sme::error::Code::PARSE_EXPECTED_FUNCTION_OR_DECLARATION);
    EXPECT_EQ(errors->Location().line, 1);

    sme::Engine validEngine;
    validEngine.SetFileSystem(sme::MakeStdFileSystem());
    EXPECT_FALSE(validEngine.LoadSource("func new_function() { return 1; }").has_value());
}

TEST(LoadErrors, PreprocessorStopsAtFirstErrorBeforeLexing)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    const auto error = engine.LoadSource("#unknown\n#unknown\n@\n", "bad.gs");

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->Code(), sme::error::Code::PREPROCESSING_UNKNOWN_DIRECTIVE);
    EXPECT_EQ(error->Location().source, "bad.gs");
    EXPECT_EQ(error->Location().line, 1);
}

TEST(LoadErrors, LexerStopsAtFirstErrorBeforeParsing)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    const auto error = engine.LoadSource("@ @ @ @ @ @ @ @ @ @ @ @", "bad.gs");

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->Code(), sme::error::Code::LEXING_UNEXPECTED_CHARACTER);
    EXPECT_EQ(error->Location().source, "bad.gs");
    EXPECT_EQ(error->Location().line, 1);
}

TEST(LoadErrors, ParserStopsAtFirstErrorBeforeCompilation)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    const auto error = engine.LoadSource(
        "func broken() {\n"
        "  var first = ;\n"
        "  var second = ;\n"
        "}\n",
        "bad.gs");

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->Code(), sme::error::Code::PARSE_EXPECTED_EXPRESSION);
    EXPECT_EQ(error->Location().source, "bad.gs");
}

TEST(LoadErrors, CompilerStopsAtFirstError)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    const auto error = engine.LoadSource(
        "func first() { break; }\n"
        "func second() { continue; }\n",
        "bad.gs");

    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(error->Code(), sme::error::Code::COMPILATION_BREAK_OUTSIDE_LOOP);
    EXPECT_EQ(error->Location().source, "bad.gs");
}

TEST(ArithmeticOperator, SIGN_Float)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSIGN_Float");
    EXPECT_FLOAT_EQ(script.Floating(), -68.14f);
}

TEST(ArithmeticOperator, SIGN_Int)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSIGN_Int");
    EXPECT_FLOAT_EQ(script.Integer(), 67);
}

TEST(ArithmeticOperator, ADD_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_FloatFloat");
    EXPECT_FLOAT_EQ(script.Floating(), 10123.36f);
}

TEST(ArithmeticOperator, ADD_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_FloatInt");
    EXPECT_FLOAT_EQ(script.Floating(), 10125.12f);
}

TEST(ArithmeticOperator, ADD_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_IntFloat");
    EXPECT_FLOAT_EQ(script.Floating(), 1129.24f);
}

TEST(ArithmeticOperator, ADD_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_IntInt");
    EXPECT_FLOAT_EQ(script.Integer(), 27);
}

TEST(ArithmeticOperator, SUB_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_FloatFloat");
    EXPECT_FLOAT_EQ(script.Floating(), 33354.17f);
}

TEST(ArithmeticOperator, SUB_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_FloatInt");
    EXPECT_FLOAT_EQ(script.Floating(), 39278.1f);
}

TEST(ArithmeticOperator, SUB_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_IntFloat");
    EXPECT_NEAR(script.Floating(), 29.01f, 0.001f);
}

TEST(ArithmeticOperator, SUB_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_IntInt");
    EXPECT_FLOAT_EQ(script.Integer(), 16);
}

TEST(ArithmeticOperator, MUL_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_FloatFloat");
    EXPECT_FLOAT_EQ(script.Floating(), 2074.26f);
}

TEST(ArithmeticOperator, MUL_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_IntFloat");
    EXPECT_FLOAT_EQ(script.Floating(), 1561.5f);
}

TEST(ArithmeticOperator, MUL_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_FloatInt");
    EXPECT_FLOAT_EQ(script.Floating(), 1280.43f);
}

TEST(ArithmeticOperator, MUL_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_IntInt");
    EXPECT_FLOAT_EQ(script.Integer(), 126);
}

TEST(ArithmeticOperator, DIV_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_FloatFloat");
    EXPECT_NEAR(script.Floating(), 7.59987f, 0.001f);
}

TEST(ArithmeticOperator, DIV_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_FloatInt");
    EXPECT_FLOAT_EQ(script.Floating(), 7.6f);
}

TEST(ArithmeticOperator, DIV_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_IntFloat");
    EXPECT_NEAR(script.Floating(), 3.03f, 0.001f);
}

TEST(ArithmeticOperator, DIV_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_IntInt");
    EXPECT_FLOAT_EQ(script.Integer(), 25);
}

TEST(ArithmeticOperator, MOD)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMOD");
    EXPECT_FLOAT_EQ(script.Floating(), 3.0f);
}

TEST(BitOperator, NOT)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorNOT");
    EXPECT_EQ(script.Integer(), 0b01010110);
}

TEST(BitOperator, AND)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorAND");
    EXPECT_EQ(script.Integer(), 0b10100111);
}

TEST(BitOperator, OR)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorOR");
    EXPECT_EQ(script.Integer(), 0b11111101);
}

TEST(BitOperator, XOR)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorXOR");
    EXPECT_EQ(script.Integer(), 0b11011000);
}

TEST(BitOperator, ShiftLeft)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorShiftLeft");
    EXPECT_EQ(script.Integer(), 26);
}

TEST(BitOperator, ShiftRight)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorShiftRight");
    EXPECT_EQ(script.Integer(), 3);
}

TEST(AssignmentOperator, Set)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSet");
    EXPECT_EQ(script.Integer(), 41);
}

TEST(AssignmentOperator, SetADD)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetADD");
    EXPECT_EQ(script.Integer(), 84);
}

TEST(AssignmentOperator, SetSUB)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetSUB");
    EXPECT_EQ(script.Integer(), 74);
}

TEST(AssignmentOperator, SetMUL)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetMUL");
    EXPECT_EQ(script.Integer(), 2048);
}

TEST(AssignmentOperator, SetDIV)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetDIV");
    EXPECT_EQ(script.Integer(), 32);
}

TEST(AssignmentOperator, SetMOD)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetMOD");
    EXPECT_EQ(script.Integer(), 84);
}

TEST(AssignmentOperator, SetBitAND)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetBitAND");
    EXPECT_EQ(script.Integer(), 0b00010101);
}

TEST(AssignmentOperator, SetBitOR)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetBitOR");
    EXPECT_EQ(script.Integer(), 0b11110111);
}

TEST(AssignmentOperator, SetBitXOR)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetBitXOR");
    EXPECT_EQ(script.Integer(), 0b11100010);
}

TEST(AssignmentOperator, SetShiftLeft)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetShiftLeft");
    EXPECT_EQ(script.Integer(), 52);
}

TEST(AssignmentOperator, SetShiftRight)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetShiftRight");
    EXPECT_EQ(script.Integer(), 6);
}

TEST(AssignmentOperator, Increment)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorIncrement");
    EXPECT_EQ(script.Integer(), 61);
}

TEST(AssignmentOperator, Decrement)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorDecrement");
    EXPECT_EQ(script.Integer(), 59);
}

TEST(RelationalOperator, Equal)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorEqual");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(RelationalOperator, NotEqual)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorNotEqual");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(RelationalOperator, Less)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorLess");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(RelationalOperator, LessOrEqual)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorLessOrEqual");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(RelationalOperator, Greater)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorGreater");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(RelationalOperator, GreaterOrEqual)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorGreaterOrEqual");
    EXPECT_EQ(script.Boolean(), true);
}

TEST(TernaryOperator, True)
{
    Result script = run("ver2/test_script.gs", "test_TernaryOperatorTrue");
    EXPECT_EQ(script.Integer(), 2);
}

TEST(TernaryOperator, False)
{
    Result script = run("ver2/test_script.gs", "test_TernaryOperatorFalse");
    EXPECT_EQ(script.Integer(), 3);
}

TEST(IndexingOperator, String)
{
    Result script = run("ver2/test_script.gs", "test_IndexingOperatorString");
    EXPECT_EQ(script.Text(), "not_implemented");
}

TEST(IndexingOperator, Table)
{
    Result script = run("ver2/test_script.gs", "test_IndexingOperatorTable");
    EXPECT_EQ(script.Integer(), 99);
}

TEST(Branching, If)
{
    Result script = run("ver2/test_script.gs", "test_BranchingIf");
    EXPECT_EQ(script.Integer(), 70);
}

TEST(Branching, Else)
{
    Result script = run("ver2/test_script.gs", "test_BranchingElse");
    EXPECT_EQ(script.Integer(), 60);
}

TEST(Branching, Deeper)
{
    Result script = run("ver2/test_script.gs", "test_BranchingDeeper");
    EXPECT_EQ(script.Integer(), 50);
}

TEST(Branching, Goto)
{
    Result script = run("ver2/test_script.gs", "test_BranchingGoto");
    EXPECT_EQ(script.Integer(), 20);
}

TEST(Looping, While)
{
    Result script = run("ver2/test_script.gs", "test_LoopingWhile");
    EXPECT_EQ(script.Integer(), 1000);
}

TEST(Looping, WhileBreak)
{
    Result script = run("ver2/test_script.gs", "test_LoopingWhileBreak");
    EXPECT_EQ(script.Integer(), 1031);
}

TEST(Looping, WhileContinue)
{
    Result script = run("ver2/test_script.gs", "test_LoopingWhileContinue");
    EXPECT_EQ(script.Integer(), 1032);
}

TEST(Looping, DoWhile)
{
    Result script = run("ver2/test_script.gs", "test_LoopingDoWhile");
    EXPECT_EQ(script.Integer(), 1010);
}

TEST(Looping, For)
{
    Result script = run("ver2/test_script.gs", "test_LoopingFor");
    EXPECT_EQ(script.Integer(), 1020);
}

TEST(Looping, ForBreak)
{
    Result script = run("ver2/test_script.gs", "test_LoopingForBreak");
    EXPECT_EQ(script.Integer(), 1030);
}

TEST(Looping, ForContinue)
{
    Result script = run("ver2/test_script.gs", "test_LoopingForContinue");
    EXPECT_EQ(script.Integer(), 1040);
}

TEST(Functions, Call)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsCall");
    EXPECT_EQ(script.Integer(), 321);
}

TEST(Functions, CalllParameters)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsCallParameters");
    EXPECT_EQ(script.Integer(), 50);
}

TEST(Functions, Return)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsReturn");
    EXPECT_EQ(script.Integer(), 1060);
}

TEST(Functions, ReturnParametersZero)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsReturnParametersZero");
    EXPECT_EQ(script.Integer(), 160);
}

TEST(Functions, ReturnBrackets)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsReturnBrackets");
    EXPECT_EQ(script.Integer(), 161);
}

TEST(Functions, ChangesContent)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsChangesContent");
    EXPECT_EQ(script.Text(), "n0t1,2,3-n1t1,2,3-");
}

TEST(Functions, DifferentTypes)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsDifferentTypes");
    EXPECT_EQ(script.Text(), "first parameter is number: 1 second parameter: 2first parameter is string: 10 second parameter: 0");
}

TEST(Functions, Callback)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsCallback");
    EXPECT_EQ(script.Text(), "callback_1callback_2error");
}

TEST(Variables, Globals)
{
    Result script = run("ver2/test_script.gs", "test_VariablesGlobals");
    EXPECT_EQ(script.Text(), "0,0,tab[0,0,0]1,test,tab[1,2,3]2,test,tab[1,2,3]");
}

TEST(Variables, Constants)
{
    Result script = run("ver2/test_script.gs", "test_VariablesConstants");
    EXPECT_EQ(script.Text(), "10yozka");
}

TEST(Variables, Function)
{
    Result script = run("ver2/test_script.gs", "test_VariablesFunction");
    EXPECT_EQ(script.Text(), "MyFunc1 10MyFunc2 10");
}

TEST(Variables, Acess)
{
    Result script = run("ver2/test_script.gs", "test_VariablesAcess");
    EXPECT_EQ(script.Text(), "10;test_access ok");
}

TEST(Compile, Include)
{
    Result script = run("ver2/test_script.gs", "test_Include");
    EXPECT_EQ(script.Text(), "Test Include Function");
}

TEST(Compile, Log)
{
    Result script = run("ver2/test_script.gs", "test_Include");
    EXPECT_EQ(script.Text(), "Test Include Function");
}

TEST(Converting, FloatToString)
{
    Result script = run("ver2/test_script.gs", "test_FloatToString");
    EXPECT_EQ(script.Text(), "0.50.6");
}

TEST(Converting, StringToFloat)
{
    Result script = run("ver2/test_script.gs", "test_StringToFloat");
    EXPECT_EQ(script.Floating(), 1.1f);
}

TEST(MathLibrary, SgnPositive)
{
    Result script = run("ver2/test_math.gs", "test_SgnPositive");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(MathLibrary, SgnNegative)
{
    Result script = run("ver2/test_math.gs", "test_SgnNegative");
    EXPECT_EQ(script.Integer(), -1);
}

TEST(MathLibrary, SgnZero)
{
    Result script = run("ver2/test_math.gs", "test_SgnZero");
    EXPECT_EQ(script.Integer(), 0);
}

TEST(MathLibrary, AbsPositive)
{
    Result script = run("ver2/test_math.gs", "test_AbsPositive");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(MathLibrary, AbsNegative)
{
    Result script = run("ver2/test_math.gs", "test_AbsNegative");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(MathLibrary, Floor)
{
    Result script = run("ver2/test_math.gs", "test_Floor");
    EXPECT_FLOAT_EQ(script.Floating(), 3.0f);
}

TEST(MathLibrary, Ceil)
{
    Result script = run("ver2/test_math.gs", "test_Ceil");
    EXPECT_FLOAT_EQ(script.Floating(), 4.0f);
}

TEST(MathLibrary, Round)
{
    Result script = run("ver2/test_math.gs", "test_Round");
    EXPECT_FLOAT_EQ(script.Floating(), 4.0f);
}

TEST(MathLibrary, RoundDown)
{
    Result script = run("ver2/test_math.gs", "test_RoundDown");
    EXPECT_FLOAT_EQ(script.Floating(), 3.0f);
}

TEST(MathLibrary, Trunc)
{
    Result script = run("ver2/test_math.gs", "test_Trunc");
    EXPECT_FLOAT_EQ(script.Floating(), 3.14f);
}

TEST(MathLibrary, Exp)
{
    Result script = run("ver2/test_math.gs", "test_Exp");
    EXPECT_NEAR(script.Floating(), 2.718f, 0.001f);
}

TEST(MathLibrary, Log)
{
    Result script = run("ver2/test_math.gs", "test_Log");
    EXPECT_NEAR(script.Floating(), 1.0f, 0.001f);
}

TEST(MathLibrary, Pow)
{
    Result script = run("ver2/test_math.gs", "test_Pow");
    EXPECT_FLOAT_EQ(script.Floating(), 8.0f);
}

TEST(MathLibrary, Sqrt)
{
    Result script = run("ver2/test_math.gs", "test_Sqrt");
    EXPECT_FLOAT_EQ(script.Floating(), 4.0f);
}

TEST(MathLibrary, Sin)
{
    Result script = run("ver2/test_math.gs", "test_Sin");
    EXPECT_NEAR(script.Floating(), 0.0f, 0.001f);
}

TEST(MathLibrary, Cos)
{
    Result script = run("ver2/test_math.gs", "test_Cos");
    EXPECT_NEAR(script.Floating(), 1.0f, 0.001f);
}

TEST(MathLibrary, Tan)
{
    Result script = run("ver2/test_math.gs", "test_Tan");
    EXPECT_NEAR(script.Floating(), 0.0f, 0.001f);
}

TEST(MathLibrary, Asin)
{
    Result script = run("ver2/test_math.gs", "test_Asin");
    EXPECT_NEAR(script.Floating(), 1.5708f, 0.001f);
}

TEST(MathLibrary, Acos)
{
    Result script = run("ver2/test_math.gs", "test_Acos");
    EXPECT_NEAR(script.Floating(), 0.0f, 0.001f);
}

TEST(MathLibrary, Atan)
{
    Result script = run("ver2/test_math.gs", "test_Atan");
    EXPECT_NEAR(script.Floating(), 0.7854f, 0.001f);
}

TEST(AdvancedFeatures, NestedFunctions)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_NestedFunctions");
    EXPECT_EQ(script.Integer(), 30);
}

TEST(AdvancedFeatures, RecursiveFunction)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_RecursiveFunction");
    EXPECT_EQ(script.Integer(), 120);
}

TEST(AdvancedFeatures, FunctionPointers)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_FunctionPointers");
    EXPECT_EQ(script.Integer(), 8);
}

TEST(AdvancedFeatures, DynamicFunctionCall)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_DynamicFunctionCall");
    EXPECT_EQ(script.Text(), "Dynamic call successful");
}

TEST(AdvancedFeatures, VariableArguments)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_VariableArguments");
    EXPECT_EQ(script.Integer(), 6);
}

TEST(AdvancedFeatures, DefaultParameters)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_DefaultParameters");
    EXPECT_EQ(script.Integer(), 15);
}

TEST(AdvancedFeatures, MultipleReturns)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_MultipleReturns");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(AdvancedFeatures, EarlyReturn)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_EarlyReturn");
    EXPECT_EQ(script.Integer(), 10);
}

TEST(TableOperations, TableCreation)
{
    Result script = run("ver2/test_tables.gs", "test_TableCreation");
    EXPECT_EQ(script.Text(), "Table created successfully");
}

TEST(TableOperations, TableAccess)
{
    Result script = run("ver2/test_tables.gs", "test_TableAccess");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(TableOperations, TableModification)
{
    Result script = run("ver2/test_tables.gs", "test_TableModification");
    EXPECT_EQ(script.Integer(), 100);
}

TEST(TableOperations, TableIteration)
{
    Result script = run("ver2/test_tables.gs", "test_TableIteration");
    EXPECT_EQ(script.Integer(), 15);
}

TEST(TableOperations, TableSize)
{
    Result script = run("ver2/test_tables.gs", "test_TableSize");
    EXPECT_EQ(script.Integer(), 5);
}

TEST(TableOperations, TableCopy)
{
    Result script = run("ver2/test_tables.gs", "test_TableCopy");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(TableOperations, NestedTables)
{
    Result script = run("ver2/test_tables.gs", "test_NestedTables");
    EXPECT_EQ(script.Integer(), 4);
}

TEST(TableOperations, TableAsParameter)
{
    Result script = run("ver2/test_tables.gs", "test_TableAsParameter");
    EXPECT_EQ(script.Integer(), 6);
}

TEST(StringOperations, StringConcatenation)
{
    Result script = run("ver2/test_strings.gs", "test_StringConcatenation");
    EXPECT_EQ(script.Text(), "Hello World");
}

TEST(StringOperations, StringLength)
{
    Result script = run("ver2/test_strings.gs", "test_StringLength");
    EXPECT_EQ(script.Integer(), 11);
}

TEST(StringOperations, StringSubstring)
{
    Result script = run("ver2/test_strings.gs", "test_StringSubstring");
    EXPECT_EQ(script.Text(), "World");
}

TEST(StringOperations, StringSubstringEdgeCases)
{
    Result script = run("ver2/test_strings.gs", "test_StringSubstringEdgeCases");
    EXPECT_EQ(script.Text(), "Hello||World");
}

TEST(StringOperations, StringFind)
{
    Result script = run("ver2/test_strings.gs", "test_StringFind");
    EXPECT_EQ(script.Integer(), 6);
}

TEST(StringOperations, StringReplace)
{
    Result script = run("ver2/test_strings.gs", "test_StringReplace");
    EXPECT_EQ(script.Text(), "Hello Universe");
}

TEST(StringOperations, StringSplit)
{
    Result script = run("ver2/test_strings.gs", "test_StringSplit");
    EXPECT_EQ(script.Integer(), 3);
}

TEST(StringOperations, StringToUpper)
{
    Result script = run("ver2/test_strings.gs", "test_StringToUpper");
    EXPECT_EQ(script.Text(), "HELLO WORLD");
}

TEST(StringOperations, StringToLower)
{
    Result script = run("ver2/test_strings.gs", "test_StringToLower");
    EXPECT_EQ(script.Text(), "hello world");
}

TEST(StringOperations, StringTrim)
{
    Result script = run("ver2/test_strings.gs", "test_StringTrim");
    EXPECT_EQ(script.Text(), "Hello World");
}

TEST(ErrorHandling, DivisionByZero)
{
    Result script = run("ver2/test_errors_simple.gs", "test_DivisionByZero");
    EXPECT_EQ(script.Integer(), 0);
}

TEST(ErrorHandling, InvalidArrayAccess)
{
    Result script = run("ver2/test_errors_simple.gs", "test_InvalidArrayAccess");
    EXPECT_EQ(script.Integer(), 0);
}

TEST(ErrorHandling, UndefinedFunction)
{
    Result script = run("ver2/test_errors_simple.gs", "test_UndefinedFunction");
    EXPECT_EQ(script.Integer(), 0);
}

TEST(ErrorHandling, TypeMismatch)
{
    Result script = run("ver2/test_errors_simple.gs", "test_TypeMismatch");
    EXPECT_EQ(script.Text(), "Type conversion successful");
}

TEST(ErrorHandling, StackOverflow)
{
    Result script = run("ver2/test_errors_simple.gs", "test_StackOverflow");
    EXPECT_EQ(script.Integer(), 10);
}

TEST(MemoryManagement, LargeStringAllocation)
{
    Result script = run("ver2/test_memory.gs", "test_LargeStringAllocation");
    EXPECT_EQ(script.Text().size(), 10000);
}

TEST(MemoryManagement, LargeTableAllocation)
{
    Result script = run("ver2/test_memory.gs", "test_LargeTableAllocation");
    EXPECT_EQ(script.Integer(), 100);
}

TEST(MemoryManagement, MemoryLeakPrevention)
{
    Result script = run("ver2/test_memory.gs", "test_MemoryLeakPrevention");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(GameSpecific, PlayerMovement)
{
    Result script = run("ver2/test_game.gs", "test_PlayerMovement");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(GameSpecific, CollisionDetection)
{
    Result script = run("ver2/test_game.gs", "test_CollisionDetection");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(GameSpecific, InventorySystem)
{
    Result script = run("ver2/test_game.gs", "test_InventorySystem");
    EXPECT_EQ(script.Integer(), 3);
}

TEST(GameSpecific, DialogSystem)
{
    Result script = run("ver2/test_game.gs", "test_DialogSystem");
    EXPECT_EQ(script.Text(), "Dialog displayed");
}

TEST(GameSpecific, SoundSystem)
{
    Result script = run("ver2/test_game.gs", "test_SoundSystem");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(GameSpecific, InputHandling)
{
    Result script = run("ver2/test_game.gs", "test_InputHandling");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(GameSpecific, SaveLoadSystem)
{
    Result script = run("ver2/test_game.gs", "test_SaveLoadSystem");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(Integration, CompleteGameFlow)
{
    Result script = run("ver2/test_integration.gs", "test_CompleteGameFlow");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(Integration, MultipleSystems)
{
    Result script = run("ver2/test_integration.gs", "test_MultipleSystems");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(Integration, StressTest)
{
    Result script = run("ver2/test_integration.gs", "test_StressTest");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(EdgeCases, EmptyString)
{
    Result script = run("ver2/test_edge_cases.gs", "test_EmptyString");
    EXPECT_EQ(script.Text(), "");
}

TEST(EdgeCases, VeryLargeNumbers)
{
    Result script = run("ver2/test_edge_cases.gs", "test_VeryLargeNumbers");
    EXPECT_EQ(script.Integer(), 2147483647);
}

TEST(EdgeCases, VerySmallNumbers)
{
    Result script = run("ver2/test_edge_cases.gs", "test_VerySmallNumbers");
    EXPECT_EQ(script.Integer(), -2147483648);
}

TEST(EdgeCases, IntegerArithmeticOverflow)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    engine.LoadSource(R"(
        func add_overflow() { return 2147483647 + 1; }
        func sub_overflow() { return -2147483647 - 2; }
        func mul_overflow() { return 1073741824 * 2; }
    )");

    auto add = engine.Call("add_overflow");
    ASSERT_TRUE(add.error);
    EXPECT_EQ(add.error->Code(), sme::error::Code::VM_INTEGER_OVERFLOW);
    auto sub = engine.Call("sub_overflow");
    ASSERT_TRUE(sub.error);
    EXPECT_EQ(sub.error->Code(), sme::error::Code::VM_INTEGER_OVERFLOW);
    auto mul = engine.Call("mul_overflow");
    ASSERT_TRUE(mul.error);
    EXPECT_EQ(mul.error->Code(), sme::error::Code::VM_INTEGER_OVERFLOW);
}

TEST(EdgeCases, FloatingPointPrecision)
{
    Result script = run("ver2/test_edge_cases.gs", "test_FloatingPointPrecision");
    EXPECT_NEAR(script.Floating(), 3.14159f, 0.00001f);
}

TEST(EdgeCases, UnicodeStrings)
{
    Result script = run("ver2/test_edge_cases.gs", "test_UnicodeStrings");
    EXPECT_EQ(script.Text(), "Привет мир");
}

TEST(Regression, PreviousBug1)
{
    Result script = run("ver2/test_regression.gs", "test_PreviousBug1");
    EXPECT_EQ(script.Integer(), 42);
}

TEST(Regression, PreviousBug2)
{
    Result script = run("ver2/test_regression.gs", "test_PreviousBug2");
    EXPECT_EQ(script.Text(), "Fixed");
}

TEST(Regression, PreviousBug3)
{
    Result script = run("ver2/test_regression.gs", "test_PreviousBug3");
    EXPECT_FLOAT_EQ(script.Floating(), 3.14f);
}

TEST(IScriptObject, test_PropCtor)
{
    TStruct::alive = 0;
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.RegisterFactory("TStruct", makeTStruct);
    engine.Load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = callValue(engine, "test_PropCtor");
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(value.CastString(), "hello world42");
}

TEST(IScriptObject, ConstructorArgs)
{
    std::vector<sme::Value> received;
    sme::Engine engine;
    engine.RegisterFactory("TStruct", [&](sme::Engine& callbackEngine, std::span<const sme::Value> args) {
        received.assign(args.begin(), args.end());
        return makeTStruct(callbackEngine, args);
    });
    ASSERT_FALSE(engine.LoadSource(R"(
        func from_literals() {
            obj = newobj<TStruct>("bla bla bla", 42);
            return obj.test_str + (str)obj.test_int;
        }
        func from_exprs() {
            s = "hello";
            n = 20 + 22;
            obj = newobj<TStruct>(s, n);
            return obj.test_str + (str)obj.test_int;
        }
    )"));

    sme::Value literals = callValue(engine, "from_literals");
    ASSERT_EQ(received.size(), 2u);
    ASSERT_TRUE(received[0].IsString());
    ASSERT_TRUE(received[1].IsInteger());
    EXPECT_EQ(received[0].ToString(), "bla bla bla");
    EXPECT_EQ(std::get<int>(received[1]), 42);
    EXPECT_EQ(literals.CastString(), "bla bla bla42");

    received.clear();
    sme::Value exprs = callValue(engine, "from_exprs");
    ASSERT_EQ(received.size(), 2u);
    ASSERT_TRUE(received[0].IsString());
    ASSERT_TRUE(received[1].IsInteger());
    EXPECT_EQ(received[0].ToString(), "hello");
    EXPECT_EQ(std::get<int>(received[1]), 42);
    EXPECT_EQ(exprs.CastString(), "hello42");
}

TEST(IScriptObject, test_Prop)
{
    TStruct::alive = 0;
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.RegisterFactory("TStruct", makeTStruct);
    engine.Load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = callValue(engine, "test_Prop");
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(value.CastString(), "hello world42");
}

TEST(IScriptObject, test_PropCall)
{
    TStruct::alive = 0;
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.RegisterFactory("TStruct", makeTStruct);
    engine.Load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = callValue(engine, "test_PropCall");
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(value.CastString(), "7 items");
}

TEST(IScriptObject, test_PropAdd)
{
    TStruct::alive = 0;
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.RegisterFactory("TStruct", makeTStruct);
    engine.Load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = callValue(engine, "test_PropAdd");
    ASSERT_TRUE(value.IsInteger());
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(std::get<int>(value), 43);
}

TEST(IScriptObject, test_PropSharedLifetime)
{
    TStruct::alive = 0;
    std::shared_ptr<TStruct> kept;
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.RegisterFactory("TStruct", [&kept](sme::Engine& engine, std::span<const sme::Value> args) {
        kept = makeTStruct(engine, args);
        return kept;
    });
    engine.Load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = callValue(engine, "test_PropRelease");
    ASSERT_TRUE(value.IsInteger());
    EXPECT_EQ(std::get<int>(value), 0);
    EXPECT_EQ(TStruct::alive, 1);
    kept.reset();
    EXPECT_EQ(TStruct::alive, 0);
}

TEST(Value, AsNumber)
{
    const sme::Value stringValue = sme::Value::String("text");
    ASSERT_NE(stringValue.AsStringData(), nullptr);
    EXPECT_EQ(*stringValue.AsStringData(), "text");
    const sme::Value sharedString(std::in_place_type<sme::SharedString>,
        std::make_shared<std::string>("shared"));
    ASSERT_NE(sharedString.AsStringData(), nullptr);
    EXPECT_EQ(*sharedString.AsStringData(), "shared");
    EXPECT_EQ(sme::Value::Integer(42).AsStringData(), nullptr);
    EXPECT_EQ(sme::Value::Integer(42).AsInteger(), 42);
    EXPECT_EQ(sme::Value::Boolean(true).AsInteger(), 1);
    EXPECT_FALSE(sme::Value::Float(2.5f).AsInteger().has_value());
    EXPECT_EQ(sme::Value::Integer(42).AsNumber(), 42.0);
    EXPECT_EQ(sme::Value::Float(2.5f).AsNumber(), 2.5);
    EXPECT_EQ(sme::Value::Boolean(true).AsNumber(), 1.0);
    EXPECT_FALSE(sme::Value::String("42").AsNumber().has_value());
    EXPECT_EQ(sme::Value::Integer(42).ToFloat(), 42.0f);
    EXPECT_EQ(sme::Value::Float(2.5f).ToInteger(), 2);
    EXPECT_EQ(sme::Value::String("42").ToFloat(3.5f), 3.5f);
    EXPECT_EQ(sme::Value::String("42").ToInteger(7), 7);
    EXPECT_EQ(sme::Value::String("text").ToString(), "text");
    EXPECT_EQ(sme::Value::String("text").ToString("default"), "text");
    EXPECT_EQ(sme::Value::Integer(42).ToString("default"), "default");
    EXPECT_EQ(sme::Value::String("text").Length(), 4u);
    EXPECT_EQ(sme::Value::Null().TypeCode(), 0);
    EXPECT_EQ(sme::Value::Integer(42).TypeCode(), 1);
    EXPECT_EQ(sme::Value::Float(2.5f).TypeCode(), 2);
    EXPECT_EQ(sme::Value::String("text").TypeCode(), 3);

    auto table = std::make_shared<sme::Table>();
    table->emplace_back(42);
    const sme::Value valueTable(std::in_place_type<sme::TableHandle>, table);
    EXPECT_EQ(valueTable.Length(), 1u);
    EXPECT_EQ(valueTable.TypeCode(), 4);
    EXPECT_EQ(sme::Value::Pointer().TypeCode(), 6);
    EXPECT_EQ(sme::Value::Boolean(true).TypeCode(), 1);
    EXPECT_EQ(sme::Value::Shared(sme::Value::Integer(1)).TypeCode(), 0);
#if SME_TEST_ASSERTS_ENABLED
    EXPECT_DEATH(sme::Value::Integer(42).Length(), "len expects a string or a table");
#else
    EXPECT_EQ(sme::Value::Integer(42).Length(), 0u);
#endif
}

TEST(ValueOwnership, ValuesOutliveEngineAndCalls)
{
    sme::Value value;
    sme::Value table;
    {
        sme::Engine engine;
        engine.SetFileSystem(sme::MakeStdFileSystem());
        engine.LoadSource(R"(
            func text() { return "a string that exceeds inline storage"; }
            func table() { return [1, 2, 3]; }
            func other() { return 42; }
            func copy_string() {
                var original = "long enough";
                var copy = original;
                copy[[0]] = 'X';
                return original + ":" + copy;
            }
            func reference_string() {
                var text = "original string";
                var ref = &text;
                *ref = "updated string";
                return text;
            }
        )");
        value = callValue(engine, "text");
        table = callValue(engine, "table");
        EXPECT_TRUE(value.IsString());
        EXPECT_EQ(value.CastString(), "a string that exceeds inline storage");
        EXPECT_EQ(table.CastString(), "[1, 2, 3]");
        for (int i = 0; i < 100; ++i) {
            EXPECT_EQ(std::get<int>(callValue(engine, "other")), 42);
        }
        EXPECT_EQ(value.CastString(), "a string that exceeds inline storage");
        EXPECT_EQ(callValue(engine, "copy_string").CastString(), "long enough:Xong enough");
        sme::Value referenceString = callValue(engine, "reference_string");
        EXPECT_TRUE(std::holds_alternative<sme::SharedString>(referenceString));
        EXPECT_EQ(referenceString.CastString(), "updated string");
    }

    sme::Engine reader;
    EXPECT_EQ(value.CastString(), "a string that exceeds inline storage");
    EXPECT_EQ(table.CastString(), "[1, 2, 3]");
}

TEST(ValueOwnership, CyclicTableOwnershipIsRejected)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    engine.LoadSource(R"(
        func cycle() {
            var table = [0];
            table[0] = table;
            return table;
        }
    )");

    auto result = engine.Call("cycle");
    ASSERT_TRUE(result.error);
    EXPECT_EQ(result.error->Code(), sme::error::Code::VM_CYCLIC_TABLE_OWNERSHIP);
}

TEST(ValueOwnership, TablesAndSharedValuesAreAccessibleDirectly)
{
    auto items = std::make_shared<sme::Table>();
    items->emplace_back(42);
    sme::Value table(std::in_place_type<sme::TableHandle>, items);
    const auto* storedTable = std::get_if<sme::TableHandle>(&table);
    ASSERT_NE(storedTable, nullptr);
    ASSERT_TRUE(*storedTable);
    ASSERT_EQ((*storedTable)->size(), 1u);
    EXPECT_EQ(std::get<int>((**storedTable)[0]), 42);

    sme::Value linked = sme::Value::Shared(sme::Value::Integer(7));
    sme::Value linkedCopy = linked;
    const auto* sharedValue = std::get_if<sme::SharedValue>(&linked);
    ASSERT_NE(sharedValue, nullptr);
    ASSERT_TRUE(*sharedValue);
    EXPECT_EQ(std::get<int>(**sharedValue), 7);
    **sharedValue = sme::Value::Integer(11);
    EXPECT_EQ(std::get<int>(*std::get<sme::SharedValue>(linkedCopy)), 11);
}

TEST(IScriptObject, test_PropCompound)
{
    TStruct::alive = 0;
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.RegisterFactory("TStruct", makeTStruct);
    engine.Load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = callValue(engine, "test_PropCompound");
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(value.CastString(), "16abc");
}

int globalInt(sme::Engine& engine, const char* name) {
    const std::optional<size_t> index = engine.FindGlobal(name);
    EXPECT_NE(index, std::nullopt);
    const sme::Value value = engine.GlobalAt(index.value_or(0));
    if (const auto* number = std::get_if<int>(&value)) return *number;
    ADD_FAILURE() << name;
    return 0;
}

TEST(Language, OutParams)
{
    Result script = run("ver2/test_language.gs", "test_OutParams");
    EXPECT_EQ(script.Text(), "1:1:1");
}

TEST(Language, RefInc)
{
    Result script = run("ver2/test_language.gs", "test_RefInc");
    EXPECT_EQ(script.Integer(), 7);
}

TEST(Language, TableRef)
{
    Result script = run("ver2/test_language.gs", "test_TableRef");
    EXPECT_EQ(script.Text(), "11,7,30");
}

TEST(Language, RefAlias)
{
    Result script = run("ver2/test_language.gs", "test_RefAlias");
    EXPECT_EQ(script.Integer(), 8);
}

TEST(Language, RefIdentity)
{
    Result script = run("ver2/test_language.gs", "test_RefIdentity");
    EXPECT_EQ(script.Integer(), 10);
}

TEST(Language, DefaultParams)
{
    Result script = run("ver2/test_language.gs", "test_DefaultParams");
    EXPECT_EQ(script.Text(), "127,327,345,27");
}

TEST(Language, Typeof)
{
    Result script = run("ver2/test_language.gs", "test_Typeof");
    EXPECT_EQ(script.Text(), "132460");
}

TEST(Language, Sizeof)
{
    Result script = run("ver2/test_language.gs", "test_Sizeof");
    EXPECT_EQ(script.Text(), "3:2:4");
}

TEST(Language, CastFormat)
{
    Result script = run("ver2/test_language.gs", "test_CastFormat");
    EXPECT_EQ(script.Text(), "07|005|ff|3|-3|42|1.50");
}

TEST(Language, CastFloat)
{
    Result script = run("ver2/test_language.gs", "test_CastFloat");
    EXPECT_FLOAT_EQ(script.Floating(), 2.0f);
}

TEST(Language, CastFloatStr)
{
    Result script = run("ver2/test_language.gs", "test_CastFloatStr");
    EXPECT_EQ(script.Text(), "2.000000");
}

TEST(Language, StringIndex)
{
    Result script = run("ver2/test_language.gs", "test_StringIndex");
    EXPECT_EQ(script.Text(), "65:Abz");
}

TEST(Language, Constructors)
{
    Result script = run("ver2/test_language.gs", "test_Constructors");
    EXPECT_EQ(script.Text(), "4:0:3:0:0:3:3:7");
}

TEST(Language, StringCmp)
{
    Result script = run("ver2/test_language.gs", "test_StringCmp");
    EXPECT_EQ(script.Integer(), 5);
}

TEST(Language, NullCmp)
{
    Result script = run("ver2/test_language.gs", "test_NullCmp");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(Language, Precedence)
{
    Result script = run("ver2/test_language.gs", "test_Precedence");
    EXPECT_EQ(script.Text(), "14:8:4:26");
}

TEST(Language, BitPattern)
{
    Result script = run("ver2/test_language.gs", "test_BitPattern");
    EXPECT_EQ(script.Text(), "1:3");
}

TEST(Language, DivMod)
{
    Result script = run("ver2/test_language.gs", "test_DivMod");
    EXPECT_EQ(script.Text(), "3:1:-3:-2:2147483644");
}

TEST(Language, RoomName)
{
    Result script = run("ver2/test_language.gs", "test_RoomName");
    EXPECT_EQ(script.Text(), "OpenRoom_3_12");
}

TEST(Language, Defines)
{
    Result script = run("ver2/test_language.gs", "test_Defines");
    EXPECT_EQ(script.Text(), "-1,42,90,1,ko,2,0,0,6,3,1");
    EXPECT_FALSE(script.engine->FindGlobal("LANG_EMPTY"));
    EXPECT_FALSE(script.engine->FindGlobal("LANG_NEG"));
}

TEST(Language, DefineFloat)
{
    Result script = run("ver2/test_language.gs", "test_DefineFloat");
    EXPECT_EQ(script.Integer(), 2);
}

TEST(Language, Ifdef)
{
    Result script = run("ver2/test_language.gs", "test_Ifdef");
    EXPECT_EQ(script.Text(), "11,12,0,0");
}

TEST(Language, TypedDecl)
{
    Result script = run("ver2/test_language.gs", "test_TypedDecl");
    EXPECT_EQ(script.Text(), "10,2,3,40");
}

TEST(Language, TypedAssign)
{
    Result script = run("ver2/test_language.gs", "test_TypedAssign");
    EXPECT_EQ(script.Text(), "4,1.500000,zz,9");
}

TEST(Language, DynVar)
{
    Result script = run("ver2/test_language.gs", "test_DynVar");
    EXPECT_EQ(script.Integer(), 9);
}

TEST(Language, RefGlobal)
{
    Result script = run("ver2/test_language.gs", "test_RefGlobal");
    EXPECT_EQ(script.Integer(), 9);
}

TEST(Language, PtrNull)
{
    Result script = run("ver2/test_language.gs", "test_PtrNull");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(Language, MultiDecl)
{
    Result script = run("ver2/test_language.gs", "test_MultiDecl");
    EXPECT_EQ(script.Integer(), 7);
}

TEST(Language, Forward)
{
    Result script = run("ver2/test_language.gs", "test_Forward");
    EXPECT_EQ(script.Integer(), 11);
}

TEST(Language, EmptyFn)
{
    Result script = run("ver2/test_language.gs", "test_EmptyFn");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(Language, EarlyReturn)
{
    Result script = run("ver2/test_language.gs", "test_EarlyReturn");
    EXPECT_EQ(script.Text(), "123");
}

TEST(Language, VoidReturn)
{
    Result script = run("ver2/test_language.gs", "test_VoidReturn");
    EXPECT_EQ(script.Text(), "1:5:3:1");
}

TEST(Language, Goto)
{
    Result script = run("ver2/test_language.gs", "test_Goto");
    EXPECT_EQ(script.Integer(), 11);
}

TEST(Language, DoWhile)
{
    Result script = run("ver2/test_language.gs", "test_DoWhile");
    EXPECT_EQ(script.Integer(), 31);
}

TEST(Language, WhileSkip)
{
    Result script = run("ver2/test_language.gs", "test_WhileSkip");
    EXPECT_EQ(script.Integer(), 5);
}

TEST(Language, WhileContinue)
{
    Result script = run("ver2/test_language.gs", "test_WhileContinue");
    EXPECT_EQ(script.Integer(), 4);
}

TEST(Language, NestedBreak)
{
    Result script = run("ver2/test_language.gs", "test_NestedBreak");
    EXPECT_EQ(script.Integer(), 4);
}

TEST(Language, IfNoBrace)
{
    Result script = run("ver2/test_language.gs", "test_IfNoBrace");
    EXPECT_EQ(script.Integer(), 4);
}

TEST(Language, BlockScope)
{
    Result script = run("ver2/test_language.gs", "test_BlockScope");
    EXPECT_EQ(script.Integer(), 45);
}

TEST(Language, LexerExtras)
{
    Result script = run("ver2/test_language.gs", "test_LexerExtras");
    EXPECT_EQ(script.Text(), "65:room:OpenRoom:3:8");
}

TEST(Language, Unary)
{
    Result script = run("ver2/test_language.gs", "test_Unary");
    EXPECT_EQ(script.Text(), "5:1:0:-1");
}

TEST(Language, NoShortCircuit)
{
    Result script = run("ver2/test_language.gs", "test_NoShortCircuit");
    EXPECT_EQ(script.Text(), "4:0:1");
}

TEST(Language, FloatInc)
{
    Result script = run("ver2/test_language.gs", "test_FloatInc");
    EXPECT_FLOAT_EQ(script.Floating(), 2.75f);
}

TEST(Language, Min)
{
    Result script = run("ver2/test_language.gs", "test_Min");
    EXPECT_EQ(script.Integer(), 32);
}

TEST(Language, HexColor)
{
    Result script = run("ver2/test_language.gs", "test_HexColor");
    EXPECT_EQ(script.Text(), "-1:-16711936");
}

TEST(Language, Scientific)
{
    Result script = run("ver2/test_language.gs", "test_Scientific");
    EXPECT_EQ(script.Text(), "150:100:-15");
}

TEST(Language, Breakpoint)
{
    Result script = run("ver2/test_language.gs", "test_Breakpoint");
    EXPECT_EQ(script.Integer(), 1);
}

TEST(Language, Include)
{
    Result script = run("ver2/test_language.gs", "test_LangInclude");
    EXPECT_EQ(script.Integer(), 7);
}

TEST(Language, BackslashPaths)
{
    std::string path = scriptPath("ver2/test_language.gs");
    std::replace(path.begin(), path.end(), '/', '\\');

    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
    engine.Load(path);
    EXPECT_EQ(std::get<int>(callValue(engine, "test_LangInclude")), 7);
}

TEST(Language, Abort)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.Load(scriptPath("ver2/test_language_abort.gs"));
    EXPECT_EQ(engine.FindFunction("abort_should_not_compile"), std::nullopt);
	EXPECT_FALSE(engine.FindGlobal("ABORT_SEEN"));
	EXPECT_EQ(callValue(engine, "abort_seen_value").ToInteger(), 1);
}

TEST(Language, Stop)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.Load(scriptPath("ver2/test_language.gs"));
    auto stoppedResult = engine.Call("test_Stop");
    ASSERT_FALSE(stoppedResult.error);
    EXPECT_TRUE(engine.IsStopped());
    EXPECT_EQ(globalInt(engine, "lang_stop_mark"), 17);
    auto resumed = engine.Resume();
    ASSERT_FALSE(resumed.error);
    sme::Value value = std::move(resumed);
    EXPECT_FALSE(engine.IsStopped());
    EXPECT_EQ(globalInt(engine, "lang_stop_mark"), 99);
    ASSERT_TRUE(value.IsInteger());
    EXPECT_EQ(std::get<int>(value), 0);
}

// Сьют регистрируется последним, поэтому эти тесты идут после остальных.
TEST(Performance, LargeLoop)
{
    Result script = run("ver2/test_performance.gs", "test_LargeLoop");
    EXPECT_EQ(script.Integer(), 100000);
}

TEST(Performance, StringOperations)
{
    Result script = run("ver2/test_performance.gs", "test_StringOperations");
    EXPECT_EQ(script.Text().size(), 1000);
}

TEST(Performance, TableOperations)
{
    Result script = run("ver2/test_performance.gs", "test_TableOperations");
    EXPECT_EQ(script.Integer(), 100);
}

TEST(Performance, FunctionCalls)
{
    Result script = run("ver2/test_performance.gs", "test_FunctionCalls");
    EXPECT_EQ(script.Integer(), 1000);
}

// 1000 смешанных итераций: арифметика, биты, строки, таблицы, ссылки, вызовы,
// нативы и объект хоста. В Debug около 90 мс на вызов. Потолок в 1 с
// оставляет запас и ловит сильное замедление.
TEST(Performance, WorkloadSpeed)
{
    sme::Engine engine;
    engine.SetFileSystem(sme::MakeStdFileSystem());
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.RegisterFactory("TStruct", makeTStruct);
    engine.Load(scriptPath("ver2/test_performance.gs"));

    constexpr int kRepeats = 5;
    constexpr int kIterations = 1000;
    std::array<double, kRepeats> samples{};
    int checksum = 0;
    for (int i = 0; i < kRepeats; ++i) {
        const auto start = std::chrono::steady_clock::now();
        sme::Value value = callValue(engine, "test_Workload");
        samples[i] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        ASSERT_TRUE(value.IsInteger());
        const int got = std::get<int>(value);
        if (i == 0) checksum = got;
        ASSERT_EQ(got, checksum);
    }
    EXPECT_EQ(checksum, 517233);

    const double minUs = *std::min_element(samples.begin(), samples.end());
    const double maxUs = *std::max_element(samples.begin(), samples.end());
    double sumUs = 0;
    for (double sample : samples) sumUs += sample;
    const double avgUs = sumUs / kRepeats;

    std::cout << std::fixed << std::setprecision(3)
              << "[perf] test_Workload, " << kIterations << " iterations, " << kRepeats << " calls, checksum " << checksum << "\n"
              << "[perf]   min " << minUs << " us  (" << (minUs / 1000.0) << " ms, "
              << (minUs / kIterations) << " us/iteration)\n"
              << "[perf]   avg " << avgUs << " us  (" << (avgUs / 1000.0) << " ms, "
              << (avgUs / kIterations) << " us/iteration)\n"
              << "[perf]   max " << maxUs << " us  (" << (maxUs / 1000.0) << " ms, "
              << (maxUs / kIterations) << " us/iteration)\n";
    RecordProperty("workload_avg_us", std::to_string(avgUs));
    EXPECT_LT(avgUs, 1000000.0);
}
