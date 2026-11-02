#include "sme_machine.h"

#include "sme_assert.h"
#include "sme/sme_error.h"

// Интерпретатор. execute крутит верхний кадр верхнего RunState, пока
// не встретит return наружу, stop или типизированную runtime-ошибку.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace sme {
namespace {

inline Value& fastReg(Frame& frame, size_t index) {
    SME_ASSERT(index < frame.regs.size());
    return frame.regs[index];
}

}  // анонимное пространство имён

Machine::Machine(Engine& engine, State& state) : _engine(engine), _state(state) {}

int Machine::CheckedIntAdd(int x, int y) {
    if ((y > 0 && x > std::numeric_limits<int>::max() - y) ||
        (y < 0 && x < std::numeric_limits<int>::min() - y)) {
        _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
        return 0;
    }
    return x + y;
}

int Machine::CheckedIntSub(int x, int y) {
    if ((y < 0 && x > std::numeric_limits<int>::max() + y) ||
        (y > 0 && x < std::numeric_limits<int>::min() + y)) {
        _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
        return 0;
    }
    return x - y;
}

int Machine::CheckedIntMul(int x, int y) {
    if (x == 0 || y == 0) return 0;
    if ((x == -1 && y == std::numeric_limits<int>::min()) ||
        (y == -1 && x == std::numeric_limits<int>::min())) {
        _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
        return 0;
    }
    if ((x > 0 && y > 0 && x > std::numeric_limits<int>::max() / y) ||
        (x > 0 && y < 0 && y < std::numeric_limits<int>::min() / x) ||
        (x < 0 && y > 0 && x < std::numeric_limits<int>::min() / y) ||
        (x < 0 && y < 0 && x < std::numeric_limits<int>::max() / y)) {
        _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
        return 0;
    }
    return x * y;
}

Value Machine::IntBinary(Op op, int x, int y) {
    switch (op) {
    case Op::Add: return Value::Integer(CheckedIntAdd(x, y));
    case Op::Sub: return Value::Integer(CheckedIntSub(x, y));
    case Op::Mul: return Value::Integer(CheckedIntMul(x, y));
    case Op::Div:
        if (y == 0) {
            _state.RecordRuntimeError(error::Code::VM_DIVISION_BY_ZERO, "division by zero");
            return Value::Null();
        }
        if (x == std::numeric_limits<int>::min() && y == -1) {
            _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
            return Value::Null();
        }
        return Value::Integer(x / y);
    case Op::Mod:
        if (y == 0) {
            _state.RecordRuntimeError(error::Code::VM_DIVISION_BY_ZERO, "division by zero");
            return Value::Null();
        }
        if (x == std::numeric_limits<int>::min() && y == -1) {
            _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
            return Value::Null();
        }
        return Value::Integer(x % y);
    case Op::BitAnd: return Value::Integer(x & y);
    case Op::BitOr: return Value::Integer(x | y);
    case Op::BitXor: return Value::Integer(x ^ y);
    case Op::Shl:
    case Op::Shr:
        if (y < 0 || y >= 31) {
            _state.RecordRuntimeError(error::Code::VM_SHIFT_OUT_OF_RANGE, "shift out of range");
            return Value::Null();
        }
        if (op == Op::Shl) return Value::Integer(static_cast<int>(static_cast<uint32_t>(x) << y));
        return Value::Integer(static_cast<int>(static_cast<uint32_t>(x) >> static_cast<uint32_t>(y)));
    case Op::CmpEq: return Value::Integer(x == y ? 1 : 0);
    case Op::CmpNe: return Value::Integer(x != y ? 1 : 0);
    case Op::CmpLt: return Value::Integer(x < y ? 1 : 0);
    case Op::CmpLe: return Value::Integer(x <= y ? 1 : 0);
    case Op::CmpGt: return Value::Integer(x > y ? 1 : 0);
    case Op::CmpGe: return Value::Integer(x >= y ? 1 : 0);
    case Op::LogicAnd: return Value::Integer((x != 0 && y != 0) ? 1 : 0);
    case Op::LogicOr: return Value::Integer((x != 0 || y != 0) ? 1 : 0);
    default:
        _state.RecordRuntimeError(error::Code::VM_INTERNAL_OPERATOR_FAULT, "internal operator fault");
        return Value::Null();
    }
}

int Machine::NarrowInt(double number) {
    if (!std::isfinite(number) ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number >= static_cast<double>(std::numeric_limits<int>::max()) + 1.0) {
        _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
        return 0;
    }
    return static_cast<int>(number);
}

Value Machine::Materialize(const Chunk& chunk, size_t constant) {
    if (constant >= chunk.constants.size()) {
        _state.RecordRuntimeError(error::Code::VM_INTERNAL_CONSTANT_FAULT, "internal constant fault");
        return Value::Null();
    }
    const Constant& value = chunk.constants[constant];
    if (std::holds_alternative<std::monostate>(value)) return Value::Null();
    if (const auto* integer = std::get_if<int>(&value)) return sme::Value::Integer(*integer);
    if (const auto* real = std::get_if<float>(&value)) return sme::Value::Float(*real);
    if (const auto* flag = std::get_if<bool>(&value)) return Value::Boolean(*flag);
    if (const auto* text = std::get_if<std::string>(&value)) return Value::String(*text);
    if (std::holds_alternative<std::nullptr_t>(value)) return Value::Pointer();
    _state.RecordRuntimeError(error::Code::VM_INTERNAL_CONSTANT_FAULT, "internal constant fault");
    return Value::Null();
}

bool Machine::Equals(const Value& lhs, const Value& rhs) {
    if (auto left = lhs.AsNumber()) {
        if (auto right = rhs.AsNumber()) {
            return *left == *right;
        }
    }
    auto readText = [&](const Value& value, std::string& out) {
        if (const auto* text = std::get_if<std::string>(&value)) {
            out = *text;
            return true;
        }
        if (const auto* text = std::get_if<SharedString>(&value); text && *text) {
            out = **text;
            return true;
        }
        return false;
    };
    std::string leftText;
    std::string rightText;
    const bool leftIsText = readText(lhs, leftText);
    const bool rightIsText = readText(rhs, rightText);
    if (leftIsText || rightIsText) {
        return leftIsText && rightIsText && leftText == rightText;
    }
    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (lhs.IsNull() || lhs.IsPointer()) return true;
    if (const auto* flag = std::get_if<bool>(&lhs)) return *flag == std::get<bool>(rhs);
    if (const auto* handle = std::get_if<Handle>(&lhs)) {
        const Handle& other = std::get<Handle>(rhs);
        if (!*handle || !other) return false;
        return *handle == other;
    }
    if (const auto* table = std::get_if<TableHandle>(&lhs)) {
        const TableHandle& other = std::get<TableHandle>(rhs);
        return *table && other && *table == other;
    }
    if (const auto* link = std::get_if<SharedValue>(&lhs)) {
        return *link && *link == std::get<SharedValue>(rhs);
    }
    if (const auto* ref = std::get_if<LocalReference>(&lhs)) {
        return ref->identity == std::get<LocalReference>(rhs).identity;
    }
    if (const auto* ref = std::get_if<GlobalReference>(&lhs)) {
        return ref->identity == std::get<GlobalReference>(rhs).identity;
    }
    if (const auto* ref = std::get_if<IndexedReference>(&lhs)) {
        return ref->identity == std::get<IndexedReference>(rhs).identity;
    }
    return false;
}

// NaN ни меньше, ни больше, ни равен. == идёт через equals и для NaN уже ложен;
// <= и >= без этой проверки считали бы «не меньше и не больше» равенством.
int Machine::Compare(const Value& lhs, const Value& rhs) {
    auto left = lhs.AsNumber();
    auto right = rhs.AsNumber();
    if (!left || !right) {
        _state.RecordRuntimeError(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
        return 0;
    }
    if (*left < *right) return -1;
    if (*left > *right) return 1;
    return 0;
}

Value Machine::Unary(Op op, const Value& value) {
    if (op == Op::Not) {
        return Value::Integer(_state.Truthy(value) ? 0 : 1);
    }
    if (op == Op::Neg) {
        if (const auto* integer = std::get_if<int>(&value)) {
            if (*integer == std::numeric_limits<int>::min()) {
                _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
                return Value::Null();
            }
            return sme::Value::Integer(-*integer);
        }
        if (const auto* real = std::get_if<float>(&value)) {
            return sme::Value::Float(-*real);
        }
        _state.RecordRuntimeError(error::Code::VM_NEGATION_EXPECTS_NUMBER, "negation expects a number");
        return Value::Null();
    }
    if (const auto* integer = std::get_if<int>(&value)) {
        return sme::Value::Integer(~*integer);
    }
    _state.RecordRuntimeError(error::Code::VM_BITWISE_NOT_EXPECTS_INT, "bitwise not expects an int");
    return Value::Null();
}

Value Machine::Binary(Op op, const Value& lhs, const Value& rhs) {
    if (lhs.index() == VT_INT && rhs.index() == VT_INT) {
        return IntBinary(op, std::get<int>(lhs), std::get<int>(rhs));
    }
    if (op == Op::LogicAnd || op == Op::LogicOr) {
        bool left = _state.Truthy(lhs);
        bool right = _state.Truthy(rhs);
        bool result = op == Op::LogicAnd ? (left && right) : (left || right);
        return Value::Integer(result ? 1 : 0);
    }
    if (op == Op::Add && lhs.IsTable() && rhs.IsTable()) {
        const TableHandle& left = std::get<TableHandle>(lhs);
        const TableHandle& right = std::get<TableHandle>(rhs);
        auto joined = std::make_shared<Table>(*left);
        joined->insert(joined->end(), right->begin(), right->end());
        return Value(std::in_place_type<TableHandle>, std::move(joined));
    }
    if (op == Op::Add) {
        const std::string* leftText = lhs.AsStringData();
        const std::string* rightText = rhs.AsStringData();
        if (leftText || rightText) {
            std::string leftStorage;
            std::string rightStorage;
            const std::string& left = leftText ? *leftText : (leftStorage = lhs.CastString());
            const std::string& right = rightText ? *rightText : (rightStorage = rhs.CastString());
            std::string result;
            result.reserve(left.size() + right.size());
            result.append(left);
            result.append(right);
            return Value::String(std::move(result));
        }
    }
    if (op == Op::CmpEq) return Value::Integer(Equals(lhs, rhs) ? 1 : 0);
    if (op == Op::CmpNe) return Value::Integer(Equals(lhs, rhs) ? 0 : 1);
    if (op == Op::CmpLt || op == Op::CmpLe || op == Op::CmpGt || op == Op::CmpGe) {
        auto left = lhs.AsNumber();
        auto right = rhs.AsNumber();
        if (!left || !right) {
            _state.RecordRuntimeError(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
            return Value::Null();
        }
        if (std::isnan(*left) || std::isnan(*right)) {
            return Value::Integer(0);
        }
        int order = Compare(lhs, rhs);
        bool result = false;
        switch (op) {
        case Op::CmpLt: result = order < 0; break;
        case Op::CmpLe: result = order <= 0; break;
        case Op::CmpGt: result = order > 0; break;
        case Op::CmpGe: result = order >= 0; break;
        default: break;
        }
        return Value::Integer(result ? 1 : 0);
    }

    auto left = lhs.AsNumber();
    auto right = rhs.AsNumber();
    if (!left || !right) {
        _state.RecordRuntimeError(error::Code::VM_OPERATOR_EXPECTS_NUMBERS, "operator expects numbers");
        return Value::Null();
    }
    const bool floats = lhs.IsFloat() || rhs.IsFloat();
    const double a = *left;
    const double b = *right;

    auto bothInts = [&]() -> std::optional<std::pair<int, int>> {
        if (floats) return std::nullopt;
        auto x = lhs.AsInteger();
        auto y = rhs.AsInteger();
        if (!x || !y) return std::nullopt;
        return std::pair{*x, *y};
    };

    if (!floats && (op == Op::Add || op == Op::Sub || op == Op::Mul)) {
        auto pair = bothInts();
        if (!pair) {
            _state.RecordRuntimeError(error::Code::VM_OPERATOR_EXPECTS_NUMBERS, "operator expects numbers");
            return Value::Null();
        }
        const int64_t x = pair->first;
        const int64_t y = pair->second;
        int64_t z = 0;
        if (op == Op::Add) z = x + y;
        else if (op == Op::Sub) z = x - y;
        else z = x * y;
        if (z > std::numeric_limits<int>::max() || z < std::numeric_limits<int>::min()) {
            _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
            return Value::Null();
        }
        return Value::Integer(static_cast<int>(z));
    }

    switch (op) {
    case Op::Add: return Value::Float(static_cast<float>(a + b));
    case Op::Sub: return Value::Float(static_cast<float>(a - b));
    case Op::Mul: return Value::Float(static_cast<float>(a * b));
    case Op::Div: {
        // INT_MIN / -1 непредставимо в int32. То же проверяется у остатка.
        if (b == 0) {
            _state.RecordRuntimeError(error::Code::VM_DIVISION_BY_ZERO, "division by zero");
            return Value::Null();
        }
        if (!floats) {
            auto pair = *bothInts();
            if (pair.first == std::numeric_limits<int>::min() && pair.second == -1) {
                _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
                return Value::Null();
            }
            return Value::Integer(pair.first / pair.second);
        }
        return Value::Float(static_cast<float>(a / b));
    }
    case Op::Mod: {
        if (b == 0) {
            _state.RecordRuntimeError(error::Code::VM_DIVISION_BY_ZERO, "division by zero");
            return Value::Null();
        }
        if (!floats) {
            auto pair = *bothInts();
            if (pair.first == std::numeric_limits<int>::min() && pair.second == -1) {
                _state.RecordRuntimeError(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
                return Value::Null();
            }
            return Value::Integer(pair.first % pair.second);
        }
        return Value::Float(static_cast<float>(std::fmod(a, b)));
    }
    case Op::BitAnd:
    case Op::BitOr:
    case Op::BitXor:
    case Op::Shl:
    case Op::Shr: {
        auto pair = bothInts();
        if (!pair) {
            _state.RecordRuntimeError(error::Code::VM_BITWISE_OPERATOR_EXPECTS_INTS, "bitwise operator expects ints");
            return Value::Null();
        }
        int x = pair->first;
        int y = pair->second;
        switch (op) {
        case Op::BitAnd: return Value::Integer(x & y);
        case Op::BitOr: return Value::Integer(x | y);
        case Op::BitXor: return Value::Integer(x ^ y);
        case Op::Shl:
        case Op::Shr:
            // Сдвиг логический, через uint32. Бит 31 поставить нельзя: счётчик только 0..30.
            // Поэтому -8 >> 1 — это 2147483644, а не арифметические -4.
            if (y < 0 || y >= 31) {
                _state.RecordRuntimeError(error::Code::VM_SHIFT_OUT_OF_RANGE, "shift out of range");
                return Value::Null();
            }
            if (op == Op::Shl) {
                return Value::Integer(static_cast<int>(static_cast<uint32_t>(x) << y));
            }
            return Value::Integer(static_cast<int>(static_cast<uint32_t>(x) >> static_cast<uint32_t>(y)));
        default: break;
        }
        break;
    }
    default:
        break;
    }
    _state.RecordRuntimeError(error::Code::VM_INTERNAL_OPERATOR_FAULT, "internal operator fault");
    return Value::Null();
}

error::Result<Value> Machine::Run(size_t function, std::span<const Value> args) {
    if (_state.hasRuntimeError) {
        return {Value::Null(), _state.GetRuntimeError()};
    }
    if (function >= _state.functions.size()) {
        return {Value::Null(), _state.MakeRuntimeError(error::Code::VM_UNKNOWN_FUNCTION, "unknown function")};
    }
    const Chunk& chunk = _state.functions[function];
    if (_state.runStack.size() >= SME_MAX_CALL_DEPTH) {
        return {Value::Null(), _state.MakeRuntimeError(error::Code::VM_CALL_DEPTH_EXCEEDED, "script call nested too deeply")};
    }

    Frame frame;
    frame.function = function;
    frame.generation = _state.FreshFrameId();
    frame.regs.resize(chunk.registerCount);
    const size_t supplied = std::min(args.size(), static_cast<size_t>(chunk.parameterCount));
    frame.supplied = static_cast<uint16_t>(supplied);
    for (size_t i = 0; i < supplied; ++i) {
        frame.regs[i] = args[i];
    }
    RunState fresh;
    fresh.frames.push_back(std::move(frame));
    _state.runStack.push_back(std::move(fresh));
    // Снимает этот RunState при return/ошибке; stop кадр оставляет.
    struct Guard {
        State& state;
        bool active = true;
        ~Guard() {
            if (active && !state.runStack.empty()) {
                state.runStack.pop_back();
            }
        }
        void Keep() { active = false; }
    } guard{_state};
    auto result = Execute();
    if (result.error) {
        return {Value::Null(), std::move(result.error)};
    }
    if (!result.has_value()) {
        guard.Keep();
        return Value::Null();
    }
    return std::move(*result);
}

error::Result<Value> Machine::Resume() {
    if (_state.runStack.empty()) {
        return {Value::Null(), _state.MakeRuntimeError(error::Code::VM_NOTHING_TO_RESUME, "nothing to resume")};
    }
    // Тот же страж, что у run, но кадр уже лежит на стеке: его положил остановившийся call.
    struct Guard {
        State& state;
        bool active = true;
        ~Guard() {
            if (active && !state.runStack.empty()) {
                state.runStack.pop_back();
            }
        }
        void Keep() { active = false; }
    } guard{_state};
    auto result = Execute();
    if (result.error) {
        return {Value::Null(), std::move(result.error)};
    }
    if (!result.has_value()) {
        guard.Keep();
        return Value::Null();
    }
    return std::move(*result);
}

namespace {

void promoteString(Value& value) {
    if (auto* text = std::get_if<std::string>(&value)) {
        value = std::make_shared<std::string>(std::move(*text));
    }
}

std::string* mutableStringData(Value& value) {
    if (auto* text = std::get_if<std::string>(&value)) return text;
    if (auto* text = std::get_if<SharedString>(&value); text && *text) return text->get();
    return nullptr;
}

bool reachesTable(const Value& value, const Table* target, std::vector<const Table*>& visited,
    std::vector<const Value*>& visitedLinks) {
    if (const auto* ref = std::get_if<IndexedReference>(&value)) {
        if (!ref->container) return false;
        if (ref->container.get() == target) return true;
        return reachesTable(Value{ref->container}, target, visited, visitedLinks);
    }
    if (const auto* link = std::get_if<SharedValue>(&value)) {
        if (!*link || std::find(visitedLinks.begin(), visitedLinks.end(), link->get()) != visitedLinks.end()) {
            return false;
        }
        visitedLinks.push_back(link->get());
        return reachesTable(**link, target, visited, visitedLinks);
    }
    const auto* table = std::get_if<TableHandle>(&value);
    if (!table || !*table) return false;
    if (table->get() == target) return true;
    if (std::find(visited.begin(), visited.end(), table->get()) != visited.end()) return false;
    visited.push_back(table->get());
    for (const Value& item : **table) {
        if (reachesTable(item, target, visited, visitedLinks)) return true;
    }
    return false;
}

bool createsCycle(const Value& value, const TableHandle& destination) {
    if (!destination) return false;
    std::vector<const Table*> visited;
    std::vector<const Value*> visitedLinks;
    return reachesTable(value, destination.get(), visited, visitedLinks);
}

}  // анонимное пространство имён

Value* Machine::Referenced(const Value& value) {
    if (!value.IsReference()) {
        _state.RecordRuntimeError(error::Code::VM_VALUE_NOT_REFERENCE, "value is not a reference");
        return nullptr;
    }
    if (const auto* link = std::get_if<SharedValue>(&value)) {
        if (!*link) {
            _state.RecordRuntimeError(error::Code::VM_DANGLING_REFERENCE, "dangling reference");
            return nullptr;
        }
        return link->get();
    }
    if (const auto* indexed = std::get_if<IndexedReference>(&value)) {
        if (!indexed->container) {
            _state.RecordRuntimeError(error::Code::VM_DANGLING_REFERENCE, "dangling reference");
            return nullptr;
        }
        if (indexed->index >= indexed->container->size()) {
            _state.RecordRuntimeError(error::Code::VM_DANGLING_REFERENCE, "dangling reference");
            return nullptr;
        }
        return &(*indexed->container)[indexed->index];
    }
    if (const auto* global = std::get_if<GlobalReference>(&value)) {
        if (global->slot >= _state.globals.size()) {
            _state.RecordRuntimeError(error::Code::VM_DANGLING_REFERENCE, "dangling reference");
            return nullptr;
        }
        return &_state.globals[global->slot].value;
    }
    const LocalReference& local = std::get<LocalReference>(value);
    if (_state.runStack.empty() || local.frame >= _state.runStack.back().frames.size()) {
        _state.RecordRuntimeError(error::Code::VM_DANGLING_REFERENCE, "dangling reference");
        return nullptr;
    }
    Frame& owner = _state.runStack.back().frames[local.frame];
    if (owner.generation != local.frameGeneration || local.slot >= owner.regs.size()) {
        _state.RecordRuntimeError(error::Code::VM_DANGLING_REFERENCE, "dangling reference");
        return nullptr;
    }
    return &owner.regs[local.slot];
}

namespace {

// Как gso_cast в GS9: формат — строка printf, буфер 127 символов.
std::string castIntToString(int value, const std::string& format) {
    char text[128];
    text[127] = 0;
    std::snprintf(text, 127, format.empty() ? "%i" : format.c_str(), value);
    return text;
}

std::string castFloatToString(float value, const std::string& format) {
    char text[128];
    text[127] = 0;
    std::snprintf(text, 127, format.empty() ? "%f" : format.c_str(), value);
    return text;
}

int castStringToInt(const std::string& text, const std::string& format) {
    int value = 0;
    std::sscanf(text.c_str(), format.empty() ? "%i" : format.c_str(), &value);
    return value;
}

float castStringToFloat(const std::string& text, const std::string& format) {
    float value = 0.f;
    std::sscanf(text.c_str(), format.empty() ? "%f" : format.c_str(), &value);
    return value;
}

}  // анонимное пространство имён

// Один шаг — одна инструкция верхнего кадра. bump двигает ip того кадра,
// который сейчас сверху: после Call сверху уже другой кадр, поэтому ip
// вызывающего увеличивают до push. write пишет в текущий верхний кадр.
error::Result<std::optional<Value>> Machine::Execute() {
    auto pendingError = [&]() -> error::Result<std::optional<Value>> {
        return {std::nullopt, _state.GetRuntimeError()};
    };
    auto failure = [&](error::Code code, std::string message) {
        _state.RecordRuntimeError(code, message);
        return pendingError();
    };
    while (true) {
        RunState& active = _state.runStack.back();
        if (active.frames.empty()) {
            return failure(error::Code::VM_CALL_STACK_UNDERFLOW, "call stack underflow");
        }
        Frame& frame = active.frames.back();
        const Chunk& chunk = _state.functions[frame.function];
        if (frame.ip >= chunk.code.size()) {
            return failure(error::Code::VM_FUNCTION_RAN_OUT_OF_INSTRUCTIONS,
                "function '" + chunk.name + "' ran out of instructions");
        }
        const Instruction& ins = chunk.code[frame.ip];
        frame.currentInstruction = frame.ip;
        frame.currentLine = ins.line;

        auto bump = [&] { frame.ip++; };
        auto write = [&](uint16_t slot, Value value) { frame.regs[slot] = std::move(value); };

        switch (ins.op) {
        case Op::LoadConst: {
            const Constant& constant = chunk.constants[ins.b];
            if (const auto* number = std::get_if<int>(&constant)) frame.regs[ins.a] = Value::Integer(*number);
            else frame.regs[ins.a] = Materialize(chunk, ins.b);
            frame.ip++;
            break;
        }
        case Op::Move: {
            // null не затирает регистр. Так a = maybe(0) оставляет прежнее a,
            // если maybe вернул null.
            Value incoming = fastReg(frame, ins.b);
            if (!incoming.IsNull()) {
                write(ins.a, std::move(incoming));
            }
            bump();
            break;
        }
        case Op::Copy: {
            write(ins.a, fastReg(frame, ins.b));
            bump();
            break;
        }
        case Op::LoadGlobal:
            if (ins.b >= _state.globals.size()) return failure(error::Code::VM_INTERNAL_GLOBAL_FAULT, "internal global fault");
            write(ins.a, _state.globals[ins.b].value);
            bump();
            break;
        case Op::StoreGlobal: {
            if (ins.a >= _state.globals.size()) return failure(error::Code::VM_INTERNAL_GLOBAL_FAULT, "internal global fault");
            Value incoming = fastReg(frame, ins.b);
            if (!incoming.IsNull()) {
                _state.globals[ins.a].value = std::move(incoming);
            }
            bump();
            break;
        }
        case Op::Stop:
            if (_state.hasRuntimeError) return pendingError();
            // ip уже за stop, поэтому stopped() узнаёт паузу по предыдущей инструкции.
            bump();
            return {std::nullopt};
        case Op::Typeof:
            write(ins.a, Value::Integer(fastReg(frame, ins.b).TypeCode()));
            bump();
            break;
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
        case Op::LogicAnd: case Op::LogicOr:
        case Op::BitAnd: case Op::BitOr: case Op::BitXor: case Op::Shl: case Op::Shr:
        case Op::CmpEq: case Op::CmpNe: case Op::CmpLt: case Op::CmpLe: case Op::CmpGt: case Op::CmpGe: {
            const Value& lhs = frame.regs[ins.b];
            const Value& rhs = frame.regs[ins.c];
            if (lhs.index() == VT_INT && rhs.index() == VT_INT) {
                frame.regs[ins.a] = IntBinary(ins.op, std::get<int>(lhs), std::get<int>(rhs));
            } else {
                frame.regs[ins.a] = Binary(ins.op, lhs, rhs);
            }
            frame.ip++;
            break;
        }
        case Op::BinaryImm: {
            const int imm = static_cast<int16_t>(ins.d);
            const Value& lhs = frame.regs[ins.b];
            const Op op = static_cast<Op>(ins.c);
            if (lhs.index() == VT_INT) {
                frame.regs[ins.a] = IntBinary(op, std::get<int>(lhs), imm);
            } else {
                frame.regs[ins.a] = Binary(op, lhs, Value::Integer(imm));
            }
            frame.ip++;
            break;
        }
        case Op::Neg: case Op::Not: case Op::BitNot: {
            Value value = fastReg(frame, ins.b);
            write(ins.a, Unary(ins.op, value));
            bump();
            break;
        }
        case Op::Jump:
            if (_state.hasRuntimeError) return pendingError();
            frame.ip = ins.a;
            break;
        case Op::JumpIfFalse:
        case Op::JumpIfTrue: {
            if (_state.hasRuntimeError) return pendingError();
            bool flag = _state.Truthy(fastReg(frame, ins.a));
            if ((ins.op == Op::JumpIfFalse && !flag) || (ins.op == Op::JumpIfTrue && flag)) {
                frame.ip = ins.b;
            } else {
                bump();
            }
            break;
        }
        case Op::JumpIfCmpFalse: {
            if (_state.hasRuntimeError) return pendingError();
            const Value& lhs = fastReg(frame, ins.a);
            const Value& rhs = fastReg(frame, ins.b);
            const Op op = static_cast<Op>(ins.c);
            bool result = false;
            if (const auto* left = std::get_if<int>(&lhs); left) {
                if (const auto* right = std::get_if<int>(&rhs); right) {
                    if (op == Op::CmpEq) result = *left == *right;
                    else if (op == Op::CmpNe) result = *left != *right;
                    else if (op == Op::CmpLt) result = *left < *right;
                    else if (op == Op::CmpLe) result = *left <= *right;
                    else if (op == Op::CmpGt) result = *left > *right;
                    else if (op == Op::CmpGe) result = *left >= *right;
                    else return failure(error::Code::VM_INTERNAL_OPERATOR_FAULT, "internal operator fault");
                } else if (op == Op::CmpEq) {
                    result = Equals(lhs, rhs);
                } else if (op == Op::CmpNe) {
                    result = !Equals(lhs, rhs);
                } else {
                    const auto leftNumber = lhs.AsNumber();
                    const auto rightNumber = rhs.AsNumber();
                    if (!leftNumber || !rightNumber) return failure(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
                    if (!std::isnan(*leftNumber) && !std::isnan(*rightNumber)) {
                        if (op == Op::CmpLt) result = *leftNumber < *rightNumber;
                        else if (op == Op::CmpLe) result = *leftNumber <= *rightNumber;
                        else if (op == Op::CmpGt) result = *leftNumber > *rightNumber;
                        else if (op == Op::CmpGe) result = *leftNumber >= *rightNumber;
                        else return failure(error::Code::VM_INTERNAL_OPERATOR_FAULT, "internal operator fault");
                    }
                }
            } else if (op == Op::CmpEq) {
                result = Equals(lhs, rhs);
            } else if (op == Op::CmpNe) {
                result = !Equals(lhs, rhs);
            } else {
                const auto left = lhs.AsNumber();
                const auto right = rhs.AsNumber();
                if (!left || !right) return failure(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
                if (!std::isnan(*left) && !std::isnan(*right)) {
                    if (op == Op::CmpLt) result = *left < *right;
                    else if (op == Op::CmpLe) result = *left <= *right;
                    else if (op == Op::CmpGt) result = *left > *right;
                    else if (op == Op::CmpGe) result = *left >= *right;
                    else return failure(error::Code::VM_INTERNAL_OPERATOR_FAULT, "internal operator fault");
                }
            }
            if (!result) frame.ip = ins.e;
            else bump();
            break;
        }
        case Op::JumpIfCmpImmFalse: {
            if (_state.hasRuntimeError) return pendingError();
            const Value& lhs = fastReg(frame, ins.a);
            const Op op = static_cast<Op>(ins.c);
            const int immediate = static_cast<int16_t>(ins.d);
            bool result = false;
            if (const auto* integer = std::get_if<int>(&lhs)) {
                if (op == Op::CmpEq) result = *integer == immediate;
                else if (op == Op::CmpNe) result = *integer != immediate;
                else if (op == Op::CmpLt) result = *integer < immediate;
                else if (op == Op::CmpLe) result = *integer <= immediate;
                else if (op == Op::CmpGt) result = *integer > immediate;
                else if (op == Op::CmpGe) result = *integer >= immediate;
                else return failure(error::Code::VM_INTERNAL_OPERATOR_FAULT, "internal operator fault");
            } else if (op == Op::CmpEq || op == Op::CmpNe) {
                bool equal = false;
                if (const auto number = lhs.AsNumber()) {
                    equal = *number == static_cast<float>(immediate);
                }
                result = op == Op::CmpEq ? equal : !equal;
            } else {
                const auto left = lhs.AsNumber();
                if (!left) return failure(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
                const float right = static_cast<float>(immediate);
                if (!std::isnan(*left)) {
                    if (op == Op::CmpLt) result = *left < right;
                    else if (op == Op::CmpLe) result = *left <= right;
                    else if (op == Op::CmpGt) result = *left > right;
                    else if (op == Op::CmpGe) result = *left >= right;
                    else return failure(error::Code::VM_INTERNAL_OPERATOR_FAULT, "internal operator fault");
                }
            }
            if (!result) frame.ip = ins.e;
            else bump();
            break;
        }
        case Op::JumpIfNotLe: {
            if (_state.hasRuntimeError) return pendingError();
            const Value& lhs = fastReg(frame, ins.a);
            const Value& rhs = fastReg(frame, ins.b);
            if (const auto* left = std::get_if<int>(&lhs); left) {
                if (const auto* right = std::get_if<int>(&rhs); right) {
                    if (*left > *right) frame.ip = ins.c;
                    else bump();
                    break;
                }
            }
            const auto left = lhs.AsNumber();
            const auto right = rhs.AsNumber();
            if (!left || !right) return failure(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
            if (!(*left <= *right)) frame.ip = ins.c;
            else bump();
            break;
        }
        case Op::IncJumpLe: {
            if (_state.hasRuntimeError) return pendingError();
            const Value& limit = fastReg(frame, ins.b);
            Value& counter = frame.regs[ins.a];
            if (const auto* left = std::get_if<int>(&counter); left) {
                if (*left == std::numeric_limits<int>::max()) return failure(error::Code::VM_INTEGER_OVERFLOW, "integer overflow");
                const int incremented = *left + 1;
                counter = incremented;
                if (const auto* right = std::get_if<int>(&limit)) {
                    if (incremented <= *right) frame.ip = ins.c;
                    else bump();
                    break;
                }
                const auto right = limit.AsNumber();
                if (!right) return failure(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
                if (static_cast<double>(incremented) <= *right) frame.ip = ins.c;
                else bump();
                break;
            }
            counter = Binary(Op::Add, counter, Value::Integer(1));
            if (const auto* left = std::get_if<int>(&counter); left) {
                if (const auto* right = std::get_if<int>(&limit); right) {
                    if (*left <= *right) frame.ip = ins.c;
                    else bump();
                    break;
                }
            }
            const auto left = counter.AsNumber();
            const auto right = limit.AsNumber();
            if (!left || !right) return failure(error::Code::VM_COMPARISON_EXPECTS_NUMBERS, "comparison expects numbers");
            if (*left <= *right) frame.ip = ins.c;
            else bump();
            break;
        }
        case Op::Call:
        case Op::CallNative: {
            if (_state.hasRuntimeError) return pendingError();
            if (ins.d > 0 && static_cast<size_t>(ins.c) + ins.d > frame.regs.size()) {
                return failure(error::Code::VM_INTERNAL_ARGUMENT_FAULT, "internal argument fault");
            }
            const std::span<const Value> args(frame.regs.data() + ins.c, ins.d);
            bump();
            if (ins.op == Op::CallNative) {
                if (ins.b >= _state.natives.size()) return failure(error::Code::VM_INTERNAL_NATIVE_FAULT, "internal native fault");
                Value result = _state.natives[ins.b](_engine, args);
                if (_state.hasRuntimeError) {
                    return pendingError();
                }
                _state.runStack.back().frames.back().regs[ins.a] = std::move(result);
                break;
            }
            if (ins.b >= _state.functions.size()) return failure(error::Code::VM_INTERNAL_FUNCTION_FAULT, "internal function fault");
            const Chunk& callee = _state.functions[ins.b];
            Frame next;
            next.function = ins.b;
            next.generation = _state.FreshFrameId();
            next.writesParent = true;
            next.parentDest = ins.a;
            next.regs.resize(callee.registerCount);
            const uint16_t count = std::min(ins.d, callee.parameterCount);
            next.supplied = count;
            for (uint16_t i = 0; i < count && i < next.regs.size(); ++i) {
                next.regs[i] = args[i];
            }
            _state.runStack.back().frames.push_back(std::move(next));
            if (_state.runStack.back().frames.size() > SME_MAX_RECURSION_DEPTH) {
                return failure(error::Code::VM_RECURSION_DEPTH_EXCEEDED, "script recursion is too deep");
            }
            break;
        }
        case Op::Return: {
            if (_state.hasRuntimeError) return pendingError();
            Value result = fastReg(frame, ins.a);
            bool writeParent = frame.writesParent;
            uint16_t dest = frame.parentDest;
            _state.runStack.back().frames.pop_back();
            if (_state.runStack.back().frames.empty()) {
                return {std::optional<Value>(std::move(result))};
            }
            if (writeParent) {
                _state.runStack.back().frames.back().regs[dest] = std::move(result);
            }
            break;
        }
        case Op::MakeTable: {
            std::vector<Value> items;
            items.reserve(ins.c);
            for (uint16_t i = 0; i < ins.c; ++i) {
                items.push_back(fastReg(frame, static_cast<size_t>(ins.b) + i));
            }
            auto table = std::make_shared<Table>();
            table->assign(std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
            write(ins.a, Value(std::in_place_type<TableHandle>, std::move(table)));
            bump();
            break;
        }
        case Op::GetIndex: {
            Value object = fastReg(frame, ins.b);
            Value index = fastReg(frame, ins.c);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) return failure(error::Code::VM_INDEX_MUST_BE_NON_NEGATIVE_INT, "index must be a non-negative int");
            if (const auto* text = object.AsStringData()) {
                if (static_cast<size_t>(*slot) >= text->size()) return failure(error::Code::VM_STRING_INDEX_OUT_OF_RANGE, "string index out of range");
                write(ins.a, Value::String(std::string(1, (*text)[static_cast<size_t>(*slot)])));
                bump();
                break;
            }
            if (!object.IsTable()) {
                return failure(error::Code::VM_VALUE_NOT_INDEXABLE, "value cannot be indexed");
            }
            const TableHandle& table = std::get<TableHandle>(object);
            if (static_cast<size_t>(*slot) >= table->size()) return failure(error::Code::VM_TABLE_INDEX_OUT_OF_RANGE, "table index out of range");
            write(ins.a, (*table)[static_cast<size_t>(*slot)]);
            bump();
            break;
        }
        case Op::SetIndex: {
            Value index = fastReg(frame, ins.b);
            Value value = fastReg(frame, ins.c);
            Value object = fastReg(frame, ins.a);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) return failure(error::Code::VM_INDEX_MUST_BE_NON_NEGATIVE_INT, "index must be a non-negative int");
            if (!object.IsTable()) return failure(error::Code::VM_VALUE_NOT_INDEXABLE, "value cannot be indexed");
            TableHandle container = std::get<TableHandle>(object);
            if (static_cast<size_t>(*slot) >= container->size()) return failure(error::Code::VM_TABLE_INDEX_OUT_OF_RANGE, "table index out of range");
            if (createsCycle(value, container)) return failure(error::Code::VM_CYCLIC_TABLE_OWNERSHIP, "cyclic table ownership is not supported");
            (*container)[static_cast<size_t>(*slot)] = std::move(value);
            bump();
            break;
        }
        case Op::GetProp:
        case Op::SetProp:
        case Op::CallMethod: {
            if (_state.hasRuntimeError) return pendingError();
            const std::string_view name = _state.ConstantString(chunk, ins.op == Op::SetProp ? ins.b : ins.c);
            if (_state.hasRuntimeError) return pendingError();
            Value object = fastReg(frame, ins.op == Op::SetProp ? ins.a : ins.b);
            if (!object.IsObject()) return failure(error::Code::VM_VALUE_HAS_NO_PROPERTIES, "value has no properties");
            IScriptObject* target = std::get<Handle>(object).get();
            if (ins.op == Op::GetProp) {
                bump();
                Value result = target->GetProperty(_engine, name);
                if (_state.hasRuntimeError) {
                    return pendingError();
                }
                _state.runStack.back().frames.back().regs[ins.a] = std::move(result);
                break;
            }
            if (ins.op == Op::SetProp) {
                Value incoming = fastReg(frame, ins.c);
                bump();
                target->SetProperty(_engine, name, incoming);
                if (_state.hasRuntimeError) {
                    return pendingError();
                }
                break;
            }
            const std::span<const Value> args(frame.regs.data() + ins.d, ins.e);
            bump();
            Value result = target->Call(_engine, name, args);
            if (_state.hasRuntimeError) {
                return pendingError();
            }
            _state.runStack.back().frames.back().regs[ins.a] = std::move(result);
            break;
        }
        case Op::MakeRefLocal: {
            // Ссылочный строковый слот получает разделяемое стабильное хранилище.
            auto& frames = _state.runStack.back().frames;
            promoteString(frames.back().regs[ins.b]);
            const size_t referenceId = _state.FreshReferenceId();
            if (_state.hasRuntimeError) return pendingError();
            write(ins.a, Value(std::in_place_type<LocalReference>,
                LocalReference{static_cast<uint32_t>(frames.size() - 1), frames.back().generation,
                    ins.b, referenceId}));
            bump();
            break;
        }
        case Op::MakeRefIndex: {
            const Value& object = fastReg(frame, ins.b);
            const Value& index = fastReg(frame, ins.c);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || !object.IsTable()) {
                return failure(error::Code::VM_ADDRESS_EXPECTS_TABLE_ELEMENT, "address expects a table element");
            }
            const TableHandle& container = std::get<TableHandle>(object);
            const uint32_t tableIndex = static_cast<uint32_t>(*slot);
            if (tableIndex < container->size()) {
                promoteString((*container)[tableIndex]);
            }
            const size_t referenceId = _state.FreshReferenceId();
            if (_state.hasRuntimeError) return pendingError();
            write(ins.a, Value(std::in_place_type<IndexedReference>,
                IndexedReference{container, tableIndex, referenceId}));
            bump();
            break;
        }
        case Op::MakeRefGlobal: {
            const size_t slot = ins.b;
            if (slot < _state.globals.size()) promoteString(_state.globals[slot].value);
            const size_t referenceId = _state.FreshReferenceId();
            if (_state.hasRuntimeError) return pendingError();
            write(ins.a, Value(std::in_place_type<GlobalReference>,
                GlobalReference{static_cast<uint32_t>(slot), referenceId}));
            bump();
            break;
        }
        case Op::LoadRef: {
            Value ref = fastReg(frame, ins.b);
            Value* target = Referenced(ref);
            if (!target) return pendingError();
            promoteString(*target);
            write(ins.a, *target);
            bump();
            break;
        }
        case Op::StoreRef: {
            Value ref = fastReg(frame, ins.a);
            Value incoming = fastReg(frame, ins.b);
            if (!incoming.IsNull()) {
                if (ref.IsReference()) {
                    const auto* target = std::get_if<IndexedReference>(&ref);
                    if (target && createsCycle(incoming, target->container)) {
                        return failure(error::Code::VM_CYCLIC_TABLE_OWNERSHIP, "cyclic table ownership is not supported");
                    }
                }
                Value* target = Referenced(ref);
                if (!target) return pendingError();
                *target = std::move(incoming);
                promoteString(*target);
            }
            bump();
            break;
        }
        case Op::RefGlobal: {
            Value index = fastReg(frame, ins.b);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || static_cast<size_t>(*slot) >= _state.globals.size()) {
                return failure(error::Code::VM_GLOBAL_INDEX_OUT_OF_RANGE, "global index out of range");
            }
            const uint32_t globalSlot = static_cast<uint32_t>(*slot);
            promoteString(_state.globals[globalSlot].value);
            const size_t referenceId = _state.FreshReferenceId();
            if (_state.hasRuntimeError) return pendingError();
            write(ins.a, Value(std::in_place_type<GlobalReference>,
                GlobalReference{globalSlot, referenceId}));
            bump();
            break;
        }
        case Op::GetChar: {
            Value object = fastReg(frame, ins.b);
            Value index = fastReg(frame, ins.c);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || !object.IsString()) {
                return failure(error::Code::VM_CHARACTER_INDEX_EXPECTS_STRING, "character index expects a string");
            }
            const std::string text = object.ToString();
            if (static_cast<size_t>(*slot) >= text.size()) return failure(error::Code::VM_STRING_INDEX_OUT_OF_RANGE, "string index out of range");
            write(ins.a, Value::Integer(static_cast<unsigned char>(text[static_cast<size_t>(*slot)])));
            bump();
            break;
        }
        case Op::SetChar: {
            Value& object = fastReg(frame, ins.a);
            Value index = fastReg(frame, ins.b);
            Value incoming = fastReg(frame, ins.c);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) return failure(error::Code::VM_CHARACTER_INDEX_EXPECTS_STRING, "character index expects a string");
            char byte = 0;
            if (const auto* number = std::get_if<int>(&incoming)) byte = static_cast<char>(*number);
            else if (const auto* text = incoming.AsStringData()) {
                if (!text->empty()) byte = (*text)[0];
            } else if (incoming.IsString() && !incoming.ToString().empty()) byte = incoming.ToString()[0];
            auto* stored = mutableStringData(object);
            if (!stored) {
                return failure(error::Code::VM_CHARACTER_INDEX_EXPECTS_STRING, "character index expects a string");
            }
            if (static_cast<size_t>(*slot) >= stored->size()) return failure(error::Code::VM_STRING_INDEX_OUT_OF_RANGE, "string index out of range");
            (*stored)[static_cast<size_t>(*slot)] = byte;
            bump();
            break;
        }
        case Op::NewObj: {
            const std::string_view type = _state.ConstantString(chunk, ins.b);
            if (_state.hasRuntimeError) return pendingError();
            auto factory = _state.factories.find(std::string(type));
            if (factory == _state.factories.end() || !factory->second) {
                return failure(error::Code::VM_UNKNOWN_TYPE, "unknown type '" + std::string(type) + "'");
            }
            std::shared_ptr<IScriptObject> object = factory->second();
            if (!object) {
                return failure(error::Code::VM_UNKNOWN_TYPE, "unknown type '" + std::string(type) + "'");
            }
            write(ins.a, Value(std::move(object)));
            bump();
            break;
        }
        case Op::Cast: {
            Value value = fastReg(frame, ins.b);
            std::string format;
            if (ins.d != 65535) {
                format = std::string(_state.ConstantString(chunk, ins.d));
                if (_state.hasRuntimeError) return pendingError();
            }
            if (ins.c == 2 && value.IsString()) {
                write(ins.a, value);
                bump();
                break;
            }
            if (ins.c == 2) {
                if (const auto* integerValue = std::get_if<int>(&value)) {
                    write(ins.a, Value::String(castIntToString(*integerValue, format)));
                } else if (const auto* realValue = std::get_if<float>(&value)) {
                    write(ins.a, Value::String(castFloatToString(*realValue, format)));
                } else if (value.IsNull()) {
                    write(ins.a, Value::String(castIntToString(0, format)));
                } else {
                    write(ins.a, Value::String(value.CastString()));
                }
                bump();
                break;
            }
            if (ins.c == 0) {
                if (const auto* integerValue = std::get_if<int>(&value)) write(ins.a, Value::Integer(*integerValue));
                else if (const auto* realValue = std::get_if<float>(&value)) {
                    write(ins.a, Value::Integer(NarrowInt(*realValue)));
                }
                else if (const auto* flag = std::get_if<bool>(&value)) write(ins.a, Value::Integer(*flag ? 1 : 0));
                else if (value.IsString()) {
                    write(ins.a, Value::Integer(castStringToInt(value.ToString(), format)));
                } else write(ins.a, Value::Integer(0));
                bump();
                break;
            }
            if (const auto* realValue = std::get_if<float>(&value)) write(ins.a, Value::Float(*realValue));
            else if (const auto* integerValue = std::get_if<int>(&value)) write(ins.a, Value::Float(static_cast<float>(*integerValue)));
            else if (value.IsString()) {
                write(ins.a, Value::Float(castStringToFloat(value.ToString(), format)));
            } else write(ins.a, Value::Float(0.f));
            bump();
            break;
        }
        case Op::CallName: {
            if (_state.hasRuntimeError) return pendingError();
            // Имя ищется линейно: сначала скриптовые функции, потом нативы.
            // Нет ни там, ни там — в регистр пишется int 0, не null и не ошибка.
            const std::string_view name = _state.ConstantString(chunk, ins.b);
            if (_state.hasRuntimeError) return pendingError();
            if (ins.d > 0 && static_cast<size_t>(ins.c) + ins.d > frame.regs.size()) {
                return failure(error::Code::VM_INTERNAL_ARGUMENT_FAULT, "internal argument fault");
            }
            const std::span<const Value> args(frame.regs.data() + ins.c, ins.d);
            bump();
            auto function = std::find_if(_state.functions.begin(), _state.functions.end(), [&](const Chunk& item) {
                return item.name == name;
            });
            if (function != _state.functions.end()) {
                const auto index = static_cast<size_t>(std::distance(_state.functions.begin(), function));
                Frame next;
                next.function = index;
                next.generation = _state.FreshFrameId();
                next.writesParent = true;
                next.parentDest = ins.a;
                next.regs.resize(function->registerCount);
                const uint16_t count = std::min<uint16_t>(ins.d, function->parameterCount);
                next.supplied = count;
                for (uint16_t i = 0; i < count && i < next.regs.size(); ++i) {
                    next.regs[i] = args[i];
                }
                _state.runStack.back().frames.push_back(std::move(next));
                if (_state.runStack.back().frames.size() > SME_MAX_RECURSION_DEPTH) {
                    return failure(error::Code::VM_RECURSION_DEPTH_EXCEEDED, "script recursion is too deep");
                }
                break;
            }
            auto native = std::find(_state.nativeNames.begin(), _state.nativeNames.end(), name);
            if (native != _state.nativeNames.end()) {
                const auto index = static_cast<size_t>(std::distance(_state.nativeNames.begin(), native));
                Value result = _state.natives[index](_engine, args);
                if (_state.hasRuntimeError) {
                    return pendingError();
                }
                _state.runStack.back().frames.back().regs[ins.a] = std::move(result);
                break;
            }
            write(ins.a, Value::Integer(0));
            break;
        }
        default:
            return failure(error::Code::VM_UNKNOWN_INSTRUCTION, "unknown instruction");
        }
    }
}

}  // пространство имён sme
