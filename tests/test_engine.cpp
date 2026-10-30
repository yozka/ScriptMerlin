#include "sme/sme_engine.h"
#include "sme/sme_host.h"
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
#include <string>
#include <unordered_map>
#include <utility>

namespace {

std::string scriptPath(const char* relative) {
    return std::string(MLN_TESTS_DIR) + "/" + relative;
}

sme::Engine& cachedEngine(const char* relative)
{
    static std::unordered_map<std::string, std::unique_ptr<sme::Engine>> cache;
    auto& slot = cache[relative];
    if (!slot) {
        auto engine = std::make_unique<sme::Engine>();
        sme::library::InstallDefault(*engine);
		sme::library::InstallMath(*engine);
		sme::library::InstallGS(*engine);
        engine->load(scriptPath(relative));
        slot = std::move(engine);
    }
    return *slot;
}

struct Result {
    sme::Engine* engine = nullptr;
    sme::Value value;

    std::string text() const { return engine->toString(value); }
    int integer() const {
        if (const auto* number = std::get_if<int>(&value)) return *number;
        ADD_FAILURE() << "expected int, got " << engine->typeName(value) << " " << engine->toString(value);
        return 0;
    }
    float floating() const {
        if (const auto* number = std::get_if<float>(&value)) return *number;
        if (const auto* number = std::get_if<int>(&value)) return static_cast<float>(*number);
        ADD_FAILURE() << "expected float, got " << engine->typeName(value);
        return 0.f;
    }
    bool boolean() const {
        if (const auto* number = std::get_if<int>(&value)) return *number != 0;
        ADD_FAILURE() << "expected int, got " << engine->typeName(value);
        return false;
    }
};

Result run(const char* file, const char* function) {
    Result result;
    result.engine = &cachedEngine(file);
    result.value = result.engine->call(function);
    return result;
}

class TStruct : public sme::HostObject {
public:
    static int alive;
    static int released;

    TStruct() { ++alive; }
    ~TStruct() override { --alive; }
    void release() override { ++released; }

    sme::Value get(sme::Engine& engine, std::string_view name) override {
        if (name == "test_int") return sme::Value::integer(testInt_);
        if (name == "test_str") return sme::Value::string(testStr_);
        if (name == "test_out") return sme::Value::string(testOut_);
        return HostObject::get(engine, name);
    }

    void set(sme::Engine& engine, std::string_view name, const sme::Value& value) override {
        if (name == "test_int" && std::holds_alternative<int>(value)) {
            testInt_ = std::get<int>(value);
            return;
        }
        if ((name == "test_str" || name == "test_out") && engine.typeOf(value) == sme::Type::String) {
            std::string text = engine.stringOf(value);
            if (name == "test_str") testStr_ = std::move(text);
            else testOut_ = std::move(text);
            return;
        }
        HostObject::set(engine, name, value);
    }

    sme::Value call(sme::Engine& engine, std::string_view name, std::span<const sme::Value> args) override {
        if (name == "run") {
            testOut_ = testStr_ + std::to_string(testInt_);
            return sme::Value::null();
        }
        if (name == "join" && args.size() == 2 && std::holds_alternative<int>(args[0]) && engine.typeOf(args[1]) == sme::Type::String) {
            return sme::Value::string(std::to_string(std::get<int>(args[0])) + engine.stringOf(args[1]));
        }
        if (name == "add" && args.size() == 2 && std::holds_alternative<int>(args[0]) && std::holds_alternative<int>(args[1])) {
            return sme::Value::integer(std::get<int>(args[0]) + std::get<int>(args[1]));
        }
        return HostObject::call(engine, name, args);
    }

private:
    int testInt_ = 0;
    std::string testStr_;
    std::string testOut_;
};

int TStruct::alive = 0;
int TStruct::released = 0;

}  // анонимное пространство имён

TEST(Variables, String)
{
    Result script = run("ver2/test_script.gs", "test_VariableString");
    EXPECT_EQ(script.text(), "Test String");
}

TEST(Variables, Integer)
{
    Result script = run("ver2/test_script.gs", "test_VariableInteger");
    EXPECT_EQ(script.integer(), 42);
}

TEST(Variables, Float)
{
    Result script = run("ver2/test_script.gs", "test_VariableFloat");
    EXPECT_FLOAT_EQ(script.floating(), 76.57f);
}

TEST(Variables, Bool)
{
    Result script = run("ver2/test_script.gs", "test_VariableBool");
    EXPECT_EQ(script.boolean(), true);
}

TEST(Variables, Binary)
{
    Result script = run("ver2/test_script.gs", "test_VariableBinary");
    EXPECT_EQ(script.integer(), 0b01010110);
}

TEST(Variables, Hexadecimal)
{
    Result script = run("ver2/test_script.gs", "test_VariableHexadecimal");
    EXPECT_EQ(script.integer(), 0xFA73);
}

TEST(Logic, NOT)
{
    Result script = run("ver2/test_script.gs", "test_LogicNOT");
    EXPECT_EQ(script.boolean(), true);
}

TEST(Logic, AND)
{
    Result script = run("ver2/test_script.gs", "test_LogicAND");
    EXPECT_EQ(script.boolean(), true);
}

TEST(Logic, OR)
{
    Result script = run("ver2/test_script.gs", "test_LogicOR");
    EXPECT_EQ(script.boolean(), true);
}

TEST(ArithmeticOperator, SIGN_Float)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSIGN_Float");
    EXPECT_FLOAT_EQ(script.floating(), -68.14f);
}

TEST(ArithmeticOperator, SIGN_Int)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSIGN_Int");
    EXPECT_FLOAT_EQ(script.integer(), 67);
}

TEST(ArithmeticOperator, ADD_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_FloatFloat");
    EXPECT_FLOAT_EQ(script.floating(), 10123.36f);
}

TEST(ArithmeticOperator, ADD_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_FloatInt");
    EXPECT_FLOAT_EQ(script.floating(), 10125.12f);
}

TEST(ArithmeticOperator, ADD_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_IntFloat");
    EXPECT_FLOAT_EQ(script.floating(), 1129.24f);
}

TEST(ArithmeticOperator, ADD_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorADD_IntInt");
    EXPECT_FLOAT_EQ(script.integer(), 27);
}

TEST(ArithmeticOperator, SUB_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_FloatFloat");
    EXPECT_FLOAT_EQ(script.floating(), 33354.17f);
}

TEST(ArithmeticOperator, SUB_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_FloatInt");
    EXPECT_FLOAT_EQ(script.floating(), 39278.1f);
}

TEST(ArithmeticOperator, SUB_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_IntFloat");
    EXPECT_NEAR(script.floating(), 29.01f, 0.001f);
}

TEST(ArithmeticOperator, SUB_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorSUB_IntInt");
    EXPECT_FLOAT_EQ(script.integer(), 16);
}

TEST(ArithmeticOperator, MUL_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_FloatFloat");
    EXPECT_FLOAT_EQ(script.floating(), 2074.26f);
}

TEST(ArithmeticOperator, MUL_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_IntFloat");
    EXPECT_FLOAT_EQ(script.floating(), 1561.5f);
}

TEST(ArithmeticOperator, MUL_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_FloatInt");
    EXPECT_FLOAT_EQ(script.floating(), 1280.43f);
}

TEST(ArithmeticOperator, MUL_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMUL_IntInt");
    EXPECT_FLOAT_EQ(script.integer(), 126);
}

TEST(ArithmeticOperator, DIV_FloatFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_FloatFloat");
    EXPECT_NEAR(script.floating(), 7.59987f, 0.001f);
}

TEST(ArithmeticOperator, DIV_FloatInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_FloatInt");
    EXPECT_FLOAT_EQ(script.floating(), 7.6f);
}

TEST(ArithmeticOperator, DIV_IntFloat)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_IntFloat");
    EXPECT_NEAR(script.floating(), 3.03f, 0.001f);
}

TEST(ArithmeticOperator, DIV_IntInt)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorDIV_IntInt");
    EXPECT_FLOAT_EQ(script.integer(), 25);
}

TEST(ArithmeticOperator, MOD)
{
    Result script = run("ver2/test_script.gs", "test_ArithmeticOperatorMOD");
    EXPECT_FLOAT_EQ(script.floating(), 3.0f);
}

TEST(BitOperator, NOT)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorNOT");
    EXPECT_EQ(script.integer(), 0b01010110);
}

TEST(BitOperator, AND)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorAND");
    EXPECT_EQ(script.integer(), 0b10100111);
}

TEST(BitOperator, OR)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorOR");
    EXPECT_EQ(script.integer(), 0b11111101);
}

TEST(BitOperator, XOR)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorXOR");
    EXPECT_EQ(script.integer(), 0b11011000);
}

TEST(BitOperator, ShiftLeft)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorShiftLeft");
    EXPECT_EQ(script.integer(), 26);
}

TEST(BitOperator, ShiftRight)
{
    Result script = run("ver2/test_script.gs", "test_BitOperatorShiftRight");
    EXPECT_EQ(script.integer(), 3);
}

TEST(AssignmentOperator, Set)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSet");
    EXPECT_EQ(script.integer(), 41);
}

TEST(AssignmentOperator, SetADD)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetADD");
    EXPECT_EQ(script.integer(), 84);
}

TEST(AssignmentOperator, SetSUB)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetSUB");
    EXPECT_EQ(script.integer(), 74);
}

TEST(AssignmentOperator, SetMUL)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetMUL");
    EXPECT_EQ(script.integer(), 2048);
}

TEST(AssignmentOperator, SetDIV)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetDIV");
    EXPECT_EQ(script.integer(), 32);
}

TEST(AssignmentOperator, SetMOD)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetMOD");
    EXPECT_EQ(script.integer(), 84);
}

TEST(AssignmentOperator, SetBitAND)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetBitAND");
    EXPECT_EQ(script.integer(), 0b00010101);
}

TEST(AssignmentOperator, SetBitOR)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetBitOR");
    EXPECT_EQ(script.integer(), 0b11110111);
}

TEST(AssignmentOperator, SetBitXOR)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetBitXOR");
    EXPECT_EQ(script.integer(), 0b11100010);
}

TEST(AssignmentOperator, SetShiftLeft)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetShiftLeft");
    EXPECT_EQ(script.integer(), 52);
}

TEST(AssignmentOperator, SetShiftRight)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorSetShiftRight");
    EXPECT_EQ(script.integer(), 6);
}

TEST(AssignmentOperator, Increment)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorIncrement");
    EXPECT_EQ(script.integer(), 61);
}

TEST(AssignmentOperator, Decrement)
{
    Result script = run("ver2/test_script.gs", "test_AssignmentOperatorDecrement");
    EXPECT_EQ(script.integer(), 59);
}

TEST(RelationalOperator, Equal)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorEqual");
    EXPECT_EQ(script.boolean(), true);
}

TEST(RelationalOperator, NotEqual)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorNotEqual");
    EXPECT_EQ(script.boolean(), true);
}

TEST(RelationalOperator, Less)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorLess");
    EXPECT_EQ(script.boolean(), true);
}

TEST(RelationalOperator, LessOrEqual)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorLessOrEqual");
    EXPECT_EQ(script.boolean(), true);
}

TEST(RelationalOperator, Greater)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorGreater");
    EXPECT_EQ(script.boolean(), true);
}

TEST(RelationalOperator, GreaterOrEqual)
{
    Result script = run("ver2/test_script.gs", "test_RelationalOperatorGreaterOrEqual");
    EXPECT_EQ(script.boolean(), true);
}

TEST(TernaryOperator, True)
{
    Result script = run("ver2/test_script.gs", "test_TernaryOperatorTrue");
    EXPECT_EQ(script.integer(), 2);
}

TEST(TernaryOperator, False)
{
    Result script = run("ver2/test_script.gs", "test_TernaryOperatorFalse");
    EXPECT_EQ(script.integer(), 3);
}

TEST(IndexingOperator, String)
{
    Result script = run("ver2/test_script.gs", "test_IndexingOperatorString");
    EXPECT_EQ(script.text(), "not_implemented");
}

TEST(IndexingOperator, Table)
{
    Result script = run("ver2/test_script.gs", "test_IndexingOperatorTable");
    EXPECT_EQ(script.integer(), 99);
}

TEST(Branching, If)
{
    Result script = run("ver2/test_script.gs", "test_BranchingIf");
    EXPECT_EQ(script.integer(), 70);
}

TEST(Branching, Else)
{
    Result script = run("ver2/test_script.gs", "test_BranchingElse");
    EXPECT_EQ(script.integer(), 60);
}

TEST(Branching, Deeper)
{
    Result script = run("ver2/test_script.gs", "test_BranchingDeeper");
    EXPECT_EQ(script.integer(), 50);
}

TEST(Branching, Goto)
{
    Result script = run("ver2/test_script.gs", "test_BranchingGoto");
    EXPECT_EQ(script.integer(), 20);
}

TEST(Looping, While)
{
    Result script = run("ver2/test_script.gs", "test_LoopingWhile");
    EXPECT_EQ(script.integer(), 1000);
}

TEST(Looping, WhileBreak)
{
    Result script = run("ver2/test_script.gs", "test_LoopingWhileBreak");
    EXPECT_EQ(script.integer(), 1031);
}

TEST(Looping, WhileContinue)
{
    Result script = run("ver2/test_script.gs", "test_LoopingWhileContinue");
    EXPECT_EQ(script.integer(), 1032);
}

TEST(Looping, DoWhile)
{
    Result script = run("ver2/test_script.gs", "test_LoopingDoWhile");
    EXPECT_EQ(script.integer(), 1010);
}

TEST(Looping, For)
{
    Result script = run("ver2/test_script.gs", "test_LoopingFor");
    EXPECT_EQ(script.integer(), 1020);
}

TEST(Looping, ForBreak)
{
    Result script = run("ver2/test_script.gs", "test_LoopingForBreak");
    EXPECT_EQ(script.integer(), 1030);
}

TEST(Looping, ForContinue)
{
    Result script = run("ver2/test_script.gs", "test_LoopingForContinue");
    EXPECT_EQ(script.integer(), 1040);
}

TEST(Functions, Call)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsCall");
    EXPECT_EQ(script.integer(), 321);
}

TEST(Functions, CalllParameters)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsCallParameters");
    EXPECT_EQ(script.integer(), 50);
}

TEST(Functions, Return)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsReturn");
    EXPECT_EQ(script.integer(), 1060);
}

TEST(Functions, ReturnParametersZero)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsReturnParametersZero");
    EXPECT_EQ(script.integer(), 160);
}

TEST(Functions, ReturnBrackets)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsReturnBrackets");
    EXPECT_EQ(script.integer(), 161);
}

TEST(Functions, ChangesContent)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsChangesContent");
    EXPECT_EQ(script.text(), "n0t1,2,3-n1t1,2,3-");
}

TEST(Functions, DifferentTypes)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsDifferentTypes");
    EXPECT_EQ(script.text(), "first parameter is number: 1 second parameter: 2first parameter is string: 10 second parameter: 0");
}

TEST(Functions, Callback)
{
    Result script = run("ver2/test_script.gs", "test_FunctionsCallback");
    EXPECT_EQ(script.text(), "callback_1callback_2error");
}

TEST(Variables, Globals)
{
    Result script = run("ver2/test_script.gs", "test_VariablesGlobals");
    EXPECT_EQ(script.text(), "0,0,tab[0,0,0]1,test,tab[1,2,3]2,test,tab[1,2,3]");
}

TEST(Variables, Constants)
{
    Result script = run("ver2/test_script.gs", "test_VariablesConstants");
    EXPECT_EQ(script.text(), "10yozka");
}

TEST(Variables, Function)
{
    Result script = run("ver2/test_script.gs", "test_VariablesFunction");
    EXPECT_EQ(script.text(), "MyFunc1 10MyFunc2 10");
}

TEST(Variables, Acess)
{
    Result script = run("ver2/test_script.gs", "test_VariablesAcess");
    EXPECT_EQ(script.text(), "10;test_access ok");
}

TEST(Compile, Include)
{
    Result script = run("ver2/test_script.gs", "test_Include");
    EXPECT_EQ(script.text(), "Test Include Function");
}

TEST(Compile, Log)
{
    Result script = run("ver2/test_script.gs", "test_Include");
    EXPECT_EQ(script.text(), "Test Include Function");
}

TEST(Converting, FloatToString)
{
    Result script = run("ver2/test_script.gs", "test_FloatToString");
    EXPECT_EQ(script.text(), "0.50.6");
}

TEST(Converting, StringToFloat)
{
    Result script = run("ver2/test_script.gs", "test_StringToFloat");
    EXPECT_EQ(script.floating(), 1.1f);
}

TEST(MathLibrary, SgnPositive)
{
    Result script = run("ver2/test_math.gs", "test_SgnPositive");
    EXPECT_EQ(script.integer(), 1);
}

TEST(MathLibrary, SgnNegative)
{
    Result script = run("ver2/test_math.gs", "test_SgnNegative");
    EXPECT_EQ(script.integer(), -1);
}

TEST(MathLibrary, SgnZero)
{
    Result script = run("ver2/test_math.gs", "test_SgnZero");
    EXPECT_EQ(script.integer(), 0);
}

TEST(MathLibrary, AbsPositive)
{
    Result script = run("ver2/test_math.gs", "test_AbsPositive");
    EXPECT_EQ(script.integer(), 42);
}

TEST(MathLibrary, AbsNegative)
{
    Result script = run("ver2/test_math.gs", "test_AbsNegative");
    EXPECT_EQ(script.integer(), 42);
}

TEST(MathLibrary, Floor)
{
    Result script = run("ver2/test_math.gs", "test_Floor");
    EXPECT_FLOAT_EQ(script.floating(), 3.0f);
}

TEST(MathLibrary, Ceil)
{
    Result script = run("ver2/test_math.gs", "test_Ceil");
    EXPECT_FLOAT_EQ(script.floating(), 4.0f);
}

TEST(MathLibrary, Round)
{
    Result script = run("ver2/test_math.gs", "test_Round");
    EXPECT_FLOAT_EQ(script.floating(), 4.0f);
}

TEST(MathLibrary, RoundDown)
{
    Result script = run("ver2/test_math.gs", "test_RoundDown");
    EXPECT_FLOAT_EQ(script.floating(), 3.0f);
}

TEST(MathLibrary, Trunc)
{
    Result script = run("ver2/test_math.gs", "test_Trunc");
    EXPECT_FLOAT_EQ(script.floating(), 3.14f);
}

TEST(MathLibrary, Exp)
{
    Result script = run("ver2/test_math.gs", "test_Exp");
    EXPECT_NEAR(script.floating(), 2.718f, 0.001f);
}

TEST(MathLibrary, Log)
{
    Result script = run("ver2/test_math.gs", "test_Log");
    EXPECT_NEAR(script.floating(), 1.0f, 0.001f);
}

TEST(MathLibrary, Pow)
{
    Result script = run("ver2/test_math.gs", "test_Pow");
    EXPECT_FLOAT_EQ(script.floating(), 8.0f);
}

TEST(MathLibrary, Sqrt)
{
    Result script = run("ver2/test_math.gs", "test_Sqrt");
    EXPECT_FLOAT_EQ(script.floating(), 4.0f);
}

TEST(MathLibrary, Sin)
{
    Result script = run("ver2/test_math.gs", "test_Sin");
    EXPECT_NEAR(script.floating(), 0.0f, 0.001f);
}

TEST(MathLibrary, Cos)
{
    Result script = run("ver2/test_math.gs", "test_Cos");
    EXPECT_NEAR(script.floating(), 1.0f, 0.001f);
}

TEST(MathLibrary, Tan)
{
    Result script = run("ver2/test_math.gs", "test_Tan");
    EXPECT_NEAR(script.floating(), 0.0f, 0.001f);
}

TEST(MathLibrary, Asin)
{
    Result script = run("ver2/test_math.gs", "test_Asin");
    EXPECT_NEAR(script.floating(), 1.5708f, 0.001f);
}

TEST(MathLibrary, Acos)
{
    Result script = run("ver2/test_math.gs", "test_Acos");
    EXPECT_NEAR(script.floating(), 0.0f, 0.001f);
}

TEST(MathLibrary, Atan)
{
    Result script = run("ver2/test_math.gs", "test_Atan");
    EXPECT_NEAR(script.floating(), 0.7854f, 0.001f);
}

TEST(AdvancedFeatures, NestedFunctions)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_NestedFunctions");
    EXPECT_EQ(script.integer(), 30);
}

TEST(AdvancedFeatures, RecursiveFunction)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_RecursiveFunction");
    EXPECT_EQ(script.integer(), 120);
}

TEST(AdvancedFeatures, FunctionPointers)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_FunctionPointers");
    EXPECT_EQ(script.integer(), 8);
}

TEST(AdvancedFeatures, DynamicFunctionCall)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_DynamicFunctionCall");
    EXPECT_EQ(script.text(), "Dynamic call successful");
}

TEST(AdvancedFeatures, VariableArguments)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_VariableArguments");
    EXPECT_EQ(script.integer(), 6);
}

TEST(AdvancedFeatures, DefaultParameters)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_DefaultParameters");
    EXPECT_EQ(script.integer(), 15);
}

TEST(AdvancedFeatures, MultipleReturns)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_MultipleReturns");
    EXPECT_EQ(script.integer(), 42);
}

TEST(AdvancedFeatures, EarlyReturn)
{
    Result script = run("ver2/test_advanced_simple.gs", "test_EarlyReturn");
    EXPECT_EQ(script.integer(), 10);
}

TEST(TableOperations, TableCreation)
{
    Result script = run("ver2/test_tables.gs", "test_TableCreation");
    EXPECT_EQ(script.text(), "Table created successfully");
}

TEST(TableOperations, TableAccess)
{
    Result script = run("ver2/test_tables.gs", "test_TableAccess");
    EXPECT_EQ(script.integer(), 42);
}

TEST(TableOperations, TableModification)
{
    Result script = run("ver2/test_tables.gs", "test_TableModification");
    EXPECT_EQ(script.integer(), 100);
}

TEST(TableOperations, TableIteration)
{
    Result script = run("ver2/test_tables.gs", "test_TableIteration");
    EXPECT_EQ(script.integer(), 15);
}

TEST(TableOperations, TableSize)
{
    Result script = run("ver2/test_tables.gs", "test_TableSize");
    EXPECT_EQ(script.integer(), 5);
}

TEST(TableOperations, TableCopy)
{
    Result script = run("ver2/test_tables.gs", "test_TableCopy");
    EXPECT_EQ(script.integer(), 42);
}

TEST(TableOperations, NestedTables)
{
    Result script = run("ver2/test_tables.gs", "test_NestedTables");
    EXPECT_EQ(script.integer(), 4);
}

TEST(TableOperations, TableAsParameter)
{
    Result script = run("ver2/test_tables.gs", "test_TableAsParameter");
    EXPECT_EQ(script.integer(), 6);
}

TEST(StringOperations, StringConcatenation)
{
    Result script = run("ver2/test_strings.gs", "test_StringConcatenation");
    EXPECT_EQ(script.text(), "Hello World");
}

TEST(StringOperations, StringLength)
{
    Result script = run("ver2/test_strings.gs", "test_StringLength");
    EXPECT_EQ(script.integer(), 11);
}

TEST(StringOperations, StringSubstring)
{
    Result script = run("ver2/test_strings.gs", "test_StringSubstring");
    EXPECT_EQ(script.text(), "World");
}

TEST(StringOperations, StringSubstringEdgeCases)
{
    Result script = run("ver2/test_strings.gs", "test_StringSubstringEdgeCases");
    EXPECT_EQ(script.text(), "Hello||World");
}

TEST(StringOperations, StringFind)
{
    Result script = run("ver2/test_strings.gs", "test_StringFind");
    EXPECT_EQ(script.integer(), 6);
}

TEST(StringOperations, StringReplace)
{
    Result script = run("ver2/test_strings.gs", "test_StringReplace");
    EXPECT_EQ(script.text(), "Hello Universe");
}

TEST(StringOperations, StringSplit)
{
    Result script = run("ver2/test_strings.gs", "test_StringSplit");
    EXPECT_EQ(script.integer(), 3);
}

TEST(StringOperations, StringToUpper)
{
    Result script = run("ver2/test_strings.gs", "test_StringToUpper");
    EXPECT_EQ(script.text(), "HELLO WORLD");
}

TEST(StringOperations, StringToLower)
{
    Result script = run("ver2/test_strings.gs", "test_StringToLower");
    EXPECT_EQ(script.text(), "hello world");
}

TEST(StringOperations, StringTrim)
{
    Result script = run("ver2/test_strings.gs", "test_StringTrim");
    EXPECT_EQ(script.text(), "Hello World");
}

TEST(ErrorHandling, DivisionByZero)
{
    Result script = run("ver2/test_errors_simple.gs", "test_DivisionByZero");
    EXPECT_EQ(script.integer(), 0);
}

TEST(ErrorHandling, InvalidArrayAccess)
{
    Result script = run("ver2/test_errors_simple.gs", "test_InvalidArrayAccess");
    EXPECT_EQ(script.integer(), 0);
}

TEST(ErrorHandling, UndefinedFunction)
{
    Result script = run("ver2/test_errors_simple.gs", "test_UndefinedFunction");
    EXPECT_EQ(script.integer(), 0);
}

TEST(ErrorHandling, TypeMismatch)
{
    Result script = run("ver2/test_errors_simple.gs", "test_TypeMismatch");
    EXPECT_EQ(script.text(), "Type conversion successful");
}

TEST(ErrorHandling, StackOverflow)
{
    Result script = run("ver2/test_errors_simple.gs", "test_StackOverflow");
    EXPECT_EQ(script.integer(), 10);
}

TEST(MemoryManagement, LargeStringAllocation)
{
    Result script = run("ver2/test_memory.gs", "test_LargeStringAllocation");
    EXPECT_EQ(script.text().size(), 10000);
}

TEST(MemoryManagement, LargeTableAllocation)
{
    Result script = run("ver2/test_memory.gs", "test_LargeTableAllocation");
    EXPECT_EQ(script.integer(), 100);
}

TEST(MemoryManagement, MemoryLeakPrevention)
{
    Result script = run("ver2/test_memory.gs", "test_MemoryLeakPrevention");
    EXPECT_EQ(script.integer(), 42);
}

TEST(GameSpecific, PlayerMovement)
{
    Result script = run("ver2/test_game.gs", "test_PlayerMovement");
    EXPECT_EQ(script.integer(), 1);
}

TEST(GameSpecific, CollisionDetection)
{
    Result script = run("ver2/test_game.gs", "test_CollisionDetection");
    EXPECT_EQ(script.integer(), 1);
}

TEST(GameSpecific, InventorySystem)
{
    Result script = run("ver2/test_game.gs", "test_InventorySystem");
    EXPECT_EQ(script.integer(), 3);
}

TEST(GameSpecific, DialogSystem)
{
    Result script = run("ver2/test_game.gs", "test_DialogSystem");
    EXPECT_EQ(script.text(), "Dialog displayed");
}

TEST(GameSpecific, SoundSystem)
{
    Result script = run("ver2/test_game.gs", "test_SoundSystem");
    EXPECT_EQ(script.integer(), 1);
}

TEST(GameSpecific, InputHandling)
{
    Result script = run("ver2/test_game.gs", "test_InputHandling");
    EXPECT_EQ(script.integer(), 1);
}

TEST(GameSpecific, SaveLoadSystem)
{
    Result script = run("ver2/test_game.gs", "test_SaveLoadSystem");
    EXPECT_EQ(script.integer(), 1);
}

TEST(Integration, CompleteGameFlow)
{
    Result script = run("ver2/test_integration.gs", "test_CompleteGameFlow");
    EXPECT_EQ(script.integer(), 1);
}

TEST(Integration, MultipleSystems)
{
    Result script = run("ver2/test_integration.gs", "test_MultipleSystems");
    EXPECT_EQ(script.integer(), 1);
}

TEST(Integration, StressTest)
{
    Result script = run("ver2/test_integration.gs", "test_StressTest");
    EXPECT_EQ(script.integer(), 1);
}

TEST(EdgeCases, EmptyString)
{
    Result script = run("ver2/test_edge_cases.gs", "test_EmptyString");
    EXPECT_EQ(script.text(), "");
}

TEST(EdgeCases, VeryLargeNumbers)
{
    Result script = run("ver2/test_edge_cases.gs", "test_VeryLargeNumbers");
    EXPECT_EQ(script.integer(), 2147483647);
}

TEST(EdgeCases, VerySmallNumbers)
{
    Result script = run("ver2/test_edge_cases.gs", "test_VerySmallNumbers");
    EXPECT_EQ(script.integer(), -2147483648);
}

TEST(EdgeCases, FloatingPointPrecision)
{
    Result script = run("ver2/test_edge_cases.gs", "test_FloatingPointPrecision");
    EXPECT_NEAR(script.floating(), 3.14159f, 0.00001f);
}

TEST(EdgeCases, UnicodeStrings)
{
    Result script = run("ver2/test_edge_cases.gs", "test_UnicodeStrings");
    EXPECT_EQ(script.text(), "Привет мир");
}

TEST(Regression, PreviousBug1)
{
    Result script = run("ver2/test_regression.gs", "test_PreviousBug1");
    EXPECT_EQ(script.integer(), 42);
}

TEST(Regression, PreviousBug2)
{
    Result script = run("ver2/test_regression.gs", "test_PreviousBug2");
    EXPECT_EQ(script.text(), "Fixed");
}

TEST(Regression, PreviousBug3)
{
    Result script = run("ver2/test_regression.gs", "test_PreviousBug3");
    EXPECT_FLOAT_EQ(script.floating(), 3.14f);
}

TEST(ScriptObject, test_Prop)
{
    TStruct::alive = 0;
    TStruct::released = 0;
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.addFactory("TStruct", [] { return std::make_shared<TStruct>(); });
    engine.load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = engine.call("test_Prop");
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(engine.toString(value), "hello world42");
}

TEST(ScriptObject, test_PropCall)
{
    TStruct::alive = 0;
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.addFactory("TStruct", [] { return std::make_shared<TStruct>(); });
    engine.load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = engine.call("test_PropCall");
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(engine.toString(value), "7 items");
}

TEST(ScriptObject, test_PropAdd)
{
    TStruct::alive = 0;
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.addFactory("TStruct", [] { return std::make_shared<TStruct>(); });
    engine.load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = engine.call("test_PropAdd");
    ASSERT_TRUE(std::holds_alternative<int>(value));
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(std::get<int>(value), 43);
}

TEST(ScriptObject, test_PropRelease)
{
    TStruct::alive = 0;
    TStruct::released = 0;
    std::shared_ptr<TStruct> kept;
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.addFactory("TStruct", [&kept] {
        kept = std::make_shared<TStruct>();
        return kept;
    });
    engine.load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = engine.call("test_PropRelease");
    ASSERT_TRUE(std::holds_alternative<int>(value));
    EXPECT_EQ(std::get<int>(value), 0);
    EXPECT_EQ(TStruct::released, 0);
    EXPECT_EQ(TStruct::alive, 1);
    kept.reset();
    EXPECT_EQ(TStruct::alive, 0);
}

TEST(ValueOwnership, ValuesOutliveEngineAndCalls)
{
    sme::Value value;
    sme::Value table;
    {
        sme::Engine engine;
        engine.loadSource(R"(
            fn text() { return "a string that exceeds inline storage"; }
            fn table() { return [1, 2, 3]; }
            fn other() { return 42; }
            fn copy_string() {
                var original = "long enough";
                var copy = original;
                copy[[0]] = 'X';
                return original + ":" + copy;
            }
            fn reference_string() {
                var text = "original string";
                var ref = &text;
                *ref = "updated string";
                return text;
            }
        )");
        value = engine.call("text");
        table = engine.call("table");
        EXPECT_TRUE(std::holds_alternative<std::string>(value));
        for (int i = 0; i < 100; ++i) {
            EXPECT_EQ(std::get<int>(engine.call("other")), 42);
        }
        EXPECT_EQ(engine.toString(value), "a string that exceeds inline storage");
        EXPECT_EQ(engine.toString(engine.call("copy_string")), "long enough:Xong enough");
        sme::Value referenceString = engine.call("reference_string");
        EXPECT_TRUE(std::holds_alternative<sme::SharedString>(referenceString));
        EXPECT_EQ(engine.toString(referenceString), "updated string");
    }

    sme::Engine reader;
    EXPECT_EQ(reader.toString(value), "a string that exceeds inline storage");
    EXPECT_EQ(reader.toString(table), "[1, 2, 3]");
}

TEST(ValueOwnership, CyclicTableOwnershipIsRejected)
{
    sme::Engine engine;
    engine.loadSource(R"(
        fn cycle() {
            var table = [0];
            table[0] = table;
            return table;
        }
    )");

    EXPECT_THROW(engine.call("cycle"), sme::RError);
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

    sme::Value linked = sme::Value::shared(sme::Value::integer(7));
    sme::Value linkedCopy = linked;
    const auto* sharedValue = std::get_if<sme::SharedValue>(&linked);
    ASSERT_NE(sharedValue, nullptr);
    ASSERT_TRUE(*sharedValue);
    EXPECT_EQ(std::get<int>(**sharedValue), 7);
    **sharedValue = sme::Value::integer(11);
    EXPECT_EQ(std::get<int>(*std::get<sme::SharedValue>(linkedCopy)), 11);
}

TEST(ScriptObject, test_PropCompound)
{
    TStruct::alive = 0;
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.addFactory("TStruct", [] { return std::make_shared<TStruct>(); });
    engine.load(scriptPath("ver2/test_prop.gs"));
    sme::Value value = engine.call("test_PropCompound");
    EXPECT_EQ(TStruct::alive, 0);
    EXPECT_EQ(engine.toString(value), "16abc");
}

int globalInt(sme::Engine& engine, const char* name) {
    const std::optional<size_t> index = engine.findGlobal(name);
    EXPECT_NE(index, std::nullopt);
    const sme::Value value = engine.globalAt(index.value_or(0));
    if (const auto* number = std::get_if<int>(&value)) return *number;
    ADD_FAILURE() << name;
    return 0;
}

TEST(Language, OutParams)
{
    Result script = run("ver2/test_language.gs", "test_OutParams");
    EXPECT_EQ(script.text(), "1:1:1");
}

TEST(Language, RefInc)
{
    Result script = run("ver2/test_language.gs", "test_RefInc");
    EXPECT_EQ(script.integer(), 7);
}

TEST(Language, TableRef)
{
    Result script = run("ver2/test_language.gs", "test_TableRef");
    EXPECT_EQ(script.text(), "11,7,30");
}

TEST(Language, RefAlias)
{
    Result script = run("ver2/test_language.gs", "test_RefAlias");
    EXPECT_EQ(script.integer(), 8);
}

TEST(Language, RefIdentity)
{
    Result script = run("ver2/test_language.gs", "test_RefIdentity");
    EXPECT_EQ(script.integer(), 10);
}

TEST(Language, DefaultParams)
{
    Result script = run("ver2/test_language.gs", "test_DefaultParams");
    EXPECT_EQ(script.text(), "127,327,345,27");
}

TEST(Language, Typeof)
{
    Result script = run("ver2/test_language.gs", "test_Typeof");
    EXPECT_EQ(script.text(), "132460");
}

TEST(Language, Sizeof)
{
    Result script = run("ver2/test_language.gs", "test_Sizeof");
    EXPECT_EQ(script.text(), "3:2:4");
}

TEST(Language, CastFormat)
{
    Result script = run("ver2/test_language.gs", "test_CastFormat");
    EXPECT_EQ(script.text(), "07|005|ff|3|-3|42|1.50");
}

TEST(Language, CastFloat)
{
    Result script = run("ver2/test_language.gs", "test_CastFloat");
    EXPECT_FLOAT_EQ(script.floating(), 2.0f);
}

TEST(Language, CastFloatStr)
{
    Result script = run("ver2/test_language.gs", "test_CastFloatStr");
    EXPECT_EQ(script.text(), "2.000000");
}

TEST(Language, StringIndex)
{
    Result script = run("ver2/test_language.gs", "test_StringIndex");
    EXPECT_EQ(script.text(), "65:Abz");
}

TEST(Language, Constructors)
{
    Result script = run("ver2/test_language.gs", "test_Constructors");
    EXPECT_EQ(script.text(), "4:0:3:0:0:3:3:7");
}

TEST(Language, StringCmp)
{
    Result script = run("ver2/test_language.gs", "test_StringCmp");
    EXPECT_EQ(script.integer(), 5);
}

TEST(Language, NullCmp)
{
    Result script = run("ver2/test_language.gs", "test_NullCmp");
    EXPECT_EQ(script.integer(), 1);
}

TEST(Language, Precedence)
{
    Result script = run("ver2/test_language.gs", "test_Precedence");
    EXPECT_EQ(script.text(), "14:8:4:26");
}

TEST(Language, BitPattern)
{
    Result script = run("ver2/test_language.gs", "test_BitPattern");
    EXPECT_EQ(script.text(), "1:3");
}

TEST(Language, DivMod)
{
    Result script = run("ver2/test_language.gs", "test_DivMod");
    EXPECT_EQ(script.text(), "3:1:-3:-2:2147483644");
}

TEST(Language, RoomName)
{
    Result script = run("ver2/test_language.gs", "test_RoomName");
    EXPECT_EQ(script.text(), "OpenRoom_3_12");
}

TEST(Language, Defines)
{
    Result script = run("ver2/test_language.gs", "test_Defines");
    EXPECT_EQ(script.text(), "-1,42,90,1,ko,2,0,0,6,3,1");
}

TEST(Language, DefineFloat)
{
    Result script = run("ver2/test_language.gs", "test_DefineFloat");
    EXPECT_EQ(script.integer(), 2);
}

TEST(Language, Ifdef)
{
    Result script = run("ver2/test_language.gs", "test_Ifdef");
    EXPECT_EQ(script.text(), "11,12,0,0");
}

TEST(Language, TypedDecl)
{
    Result script = run("ver2/test_language.gs", "test_TypedDecl");
    EXPECT_EQ(script.text(), "10,2,3,40");
}

TEST(Language, TypedAssign)
{
    Result script = run("ver2/test_language.gs", "test_TypedAssign");
    EXPECT_EQ(script.text(), "4,1.500000,zz,9");
}

TEST(Language, DynVar)
{
    Result script = run("ver2/test_language.gs", "test_DynVar");
    EXPECT_EQ(script.integer(), 9);
}

TEST(Language, RefGlobal)
{
    Result script = run("ver2/test_language.gs", "test_RefGlobal");
    EXPECT_EQ(script.integer(), 9);
}

TEST(Language, PtrNull)
{
    Result script = run("ver2/test_language.gs", "test_PtrNull");
    EXPECT_EQ(script.integer(), 1);
}

TEST(Language, MultiDecl)
{
    Result script = run("ver2/test_language.gs", "test_MultiDecl");
    EXPECT_EQ(script.integer(), 7);
}

TEST(Language, Forward)
{
    Result script = run("ver2/test_language.gs", "test_Forward");
    EXPECT_EQ(script.integer(), 11);
}

TEST(Language, EmptyFn)
{
    Result script = run("ver2/test_language.gs", "test_EmptyFn");
    EXPECT_EQ(script.integer(), 1);
}

TEST(Language, EarlyReturn)
{
    Result script = run("ver2/test_language.gs", "test_EarlyReturn");
    EXPECT_EQ(script.text(), "123");
}

TEST(Language, VoidReturn)
{
    Result script = run("ver2/test_language.gs", "test_VoidReturn");
    EXPECT_EQ(script.text(), "1:5:3:1");
}

TEST(Language, Goto)
{
    Result script = run("ver2/test_language.gs", "test_Goto");
    EXPECT_EQ(script.integer(), 11);
}

TEST(Language, DoWhile)
{
    Result script = run("ver2/test_language.gs", "test_DoWhile");
    EXPECT_EQ(script.integer(), 31);
}

TEST(Language, WhileSkip)
{
    Result script = run("ver2/test_language.gs", "test_WhileSkip");
    EXPECT_EQ(script.integer(), 5);
}

TEST(Language, WhileContinue)
{
    Result script = run("ver2/test_language.gs", "test_WhileContinue");
    EXPECT_EQ(script.integer(), 4);
}

TEST(Language, NestedBreak)
{
    Result script = run("ver2/test_language.gs", "test_NestedBreak");
    EXPECT_EQ(script.integer(), 4);
}

TEST(Language, IfNoBrace)
{
    Result script = run("ver2/test_language.gs", "test_IfNoBrace");
    EXPECT_EQ(script.integer(), 4);
}

TEST(Language, BlockScope)
{
    Result script = run("ver2/test_language.gs", "test_BlockScope");
    EXPECT_EQ(script.integer(), 45);
}

TEST(Language, LexerExtras)
{
    Result script = run("ver2/test_language.gs", "test_LexerExtras");
    EXPECT_EQ(script.text(), "65:room:OpenRoom:3:8");
}

TEST(Language, Unary)
{
    Result script = run("ver2/test_language.gs", "test_Unary");
    EXPECT_EQ(script.text(), "5:1:0:-1");
}

TEST(Language, NoShortCircuit)
{
    Result script = run("ver2/test_language.gs", "test_NoShortCircuit");
    EXPECT_EQ(script.text(), "4:0:1");
}

TEST(Language, FloatInc)
{
    Result script = run("ver2/test_language.gs", "test_FloatInc");
    EXPECT_FLOAT_EQ(script.floating(), 2.75f);
}

TEST(Language, Min)
{
    Result script = run("ver2/test_language.gs", "test_Min");
    EXPECT_EQ(script.integer(), 32);
}

TEST(Language, HexColor)
{
    Result script = run("ver2/test_language.gs", "test_HexColor");
    EXPECT_EQ(script.text(), "-1:-16711936");
}

TEST(Language, Scientific)
{
    Result script = run("ver2/test_language.gs", "test_Scientific");
    EXPECT_EQ(script.text(), "150:100:-15");
}

TEST(Language, Breakpoint)
{
    Result script = run("ver2/test_language.gs", "test_Breakpoint");
    EXPECT_EQ(script.integer(), 1);
}

TEST(Language, Include)
{
    Result script = run("ver2/test_language.gs", "test_LangInclude");
    EXPECT_EQ(script.integer(), 7);
}

TEST(Language, Scope)
{
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.load(scriptPath("ver2/test_language.gs"));
    EXPECT_EQ(engine.scope(), 41);
    EXPECT_EQ(engine.functionScope("test_LangInclude"), std::optional<size_t>{41});
    EXPECT_EQ(engine.functionScope("lang_included"), std::optional<size_t>{41});
}

TEST(Language, Abort)
{
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.load(scriptPath("ver2/test_language_abort.gs"));
    EXPECT_EQ(engine.findFunction("abort_should_not_compile"), std::nullopt);
    EXPECT_EQ(globalInt(engine, "ABORT_SEEN"), 1);
}

TEST(Language, Stop)
{
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.load(scriptPath("ver2/test_language.gs"));
    engine.call("test_Stop");
    EXPECT_TRUE(engine.stopped());
    EXPECT_EQ(globalInt(engine, "lang_stop_mark"), 17);
    sme::Value value = engine.resume();
    EXPECT_FALSE(engine.stopped());
    EXPECT_EQ(globalInt(engine, "lang_stop_mark"), 99);
    ASSERT_TRUE(std::holds_alternative<int>(value));
    EXPECT_EQ(std::get<int>(value), 0);
}

// Сьют регистрируется последним, поэтому эти тесты идут после остальных.
TEST(Performance, LargeLoop)
{
    Result script = run("ver2/test_performance.gs", "test_LargeLoop");
    EXPECT_EQ(script.integer(), 100000);
}

TEST(Performance, StringOperations)
{
    Result script = run("ver2/test_performance.gs", "test_StringOperations");
    EXPECT_EQ(script.text().size(), 1000);
}

TEST(Performance, TableOperations)
{
    Result script = run("ver2/test_performance.gs", "test_TableOperations");
    EXPECT_EQ(script.integer(), 100);
}

TEST(Performance, FunctionCalls)
{
    Result script = run("ver2/test_performance.gs", "test_FunctionCalls");
    EXPECT_EQ(script.integer(), 1000);
}

// 1000 смешанных итераций: арифметика, биты, строки, таблицы, ссылки, вызовы,
// нативы и объект хоста. В Debug около 90 мс на вызов. Потолок в 1 с
// оставляет запас и ловит сильное замедление.
TEST(Performance, WorkloadSpeed)
{
    sme::Engine engine;
	sme::library::InstallDefault(engine);
	sme::library::InstallMath(engine);
	sme::library::InstallGS(engine);
    engine.addFactory("TStruct", [] { return std::make_shared<TStruct>(); });
    engine.load(scriptPath("ver2/test_performance.gs"));

    constexpr int kRepeats = 5;
    constexpr int kIterations = 1000;
    std::array<double, kRepeats> samples{};
    int checksum = 0;
    for (int i = 0; i < kRepeats; ++i) {
        const auto start = std::chrono::steady_clock::now();
        sme::Value value = engine.call("test_Workload");
        samples[i] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        ASSERT_TRUE(std::holds_alternative<int>(value));
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
