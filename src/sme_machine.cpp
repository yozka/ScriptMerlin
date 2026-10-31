#include "sme_machine.h"

#include "sme_assert.h"
#include "sme/sme_error.h"

// Интерпретатор. execute крутит верхний кадр верхнего RunState, пока
// не встретит return наружу или stop. Исключение из опкода не ловится здесь:
// RunGuard в run/resume снимает тот RunState, который этот вход положил.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sme {
namespace {

inline Value& fastReg(Frame& frame, size_t index) {
    SME_ASSERT(index < frame.regs.size());
    return frame.regs[index];
}

int checkedIntAdd(int x, int y, int line) {
    if ((y > 0 && x > std::numeric_limits<int>::max() - y) ||
        (y < 0 && x < std::numeric_limits<int>::min() - y)) {
        throw RError("integer overflow", line);
    }
    return x + y;
}

int checkedIntSub(int x, int y, int line) {
    if ((y < 0 && x > std::numeric_limits<int>::max() + y) ||
        (y > 0 && x < std::numeric_limits<int>::min() + y)) {
        throw RError("integer overflow", line);
    }
    return x - y;
}

int checkedIntMul(int x, int y, int line) {
    if (x == 0 || y == 0) return 0;
    if ((x == -1 && y == std::numeric_limits<int>::min()) ||
        (y == -1 && x == std::numeric_limits<int>::min())) {
        throw RError("integer overflow", line);
    }
    if ((x > 0 && y > 0 && x > std::numeric_limits<int>::max() / y) ||
        (x > 0 && y < 0 && y < std::numeric_limits<int>::min() / x) ||
        (x < 0 && y > 0 && x < std::numeric_limits<int>::min() / y) ||
        (x < 0 && y < 0 && x < std::numeric_limits<int>::max() / y)) {
        throw RError("integer overflow", line);
    }
    return x * y;
}

Value intBinary(Op op, int x, int y, int line) {
    switch (op) {
    case Op::Add: return Value::Integer(checkedIntAdd(x, y, line));
    case Op::Sub: return Value::Integer(checkedIntSub(x, y, line));
    case Op::Mul: return Value::Integer(checkedIntMul(x, y, line));
    case Op::Div:
        if (y == 0) throw RError("division by zero", line);
        if (x == std::numeric_limits<int>::min() && y == -1) throw RError("integer overflow", line);
        return Value::Integer(x / y);
    case Op::Mod:
        if (y == 0) throw RError("division by zero", line);
        if (x == std::numeric_limits<int>::min() && y == -1) throw RError("integer overflow", line);
        return Value::Integer(x % y);
    case Op::BitAnd: return Value::Integer(x & y);
    case Op::BitOr: return Value::Integer(x | y);
    case Op::BitXor: return Value::Integer(x ^ y);
    case Op::Shl:
    case Op::Shr:
        if (y < 0 || y >= 31) throw RError("shift out of range", line);
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
        throw RError("internal operator fault", line);
    }
}

int narrowInt(double number, int line) {
    if (!std::isfinite(number) ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number >= static_cast<double>(std::numeric_limits<int>::max()) + 1.0) {
        throw RError("integer overflow", line);
    }
    return static_cast<int>(number);
}

std::optional<int> asInt(const Value& value) {
    if (const auto* integer = std::get_if<int>(&value)) return *integer;
    if (const auto* flag = std::get_if<bool>(&value)) return *flag ? 1 : 0;
    return std::nullopt;
}

const std::string* stringData(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value)) return text;
    if (const auto* text = std::get_if<SharedString>(&value); text && *text) return text->get();
    return nullptr;
}

}  // анонимное пространство имён

Machine::Machine(Engine& engine, State& state) : engine_(engine), state_(state) {}

Value Machine::materialize(const Chunk& chunk, size_t constant, int line) {
    if (constant >= chunk.constants.size()) {
        throw RError("internal constant fault", line);
    }
    const Constant& value = chunk.constants[constant];
    if (std::holds_alternative<std::monostate>(value)) return Value::Null();
    if (const auto* integer = std::get_if<int>(&value)) return sme::Value::Integer(*integer);
    if (const auto* real = std::get_if<float>(&value)) return sme::Value::Float(*real);
    if (const auto* flag = std::get_if<bool>(&value)) return Value::Boolean(*flag);
    if (const auto* text = std::get_if<std::string>(&value)) return Value::String(*text);
    if (std::holds_alternative<std::nullptr_t>(value)) return Value::Pointer();
    throw RError("internal constant fault", line);
}

bool Machine::equals(const Value& lhs, const Value& rhs) {
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
int Machine::compare(const Value& lhs, const Value& rhs, int line) {
    auto left = lhs.AsNumber();
    auto right = rhs.AsNumber();
    if (!left || !right) {
        throw RError("comparison expects numbers", line);
    }
    if (*left < *right) return -1;
    if (*left > *right) return 1;
    return 0;
}

Value Machine::unary(Op op, const Value& value, int line) {
    if (op == Op::Not) {
        return Value::Integer(state_.truthy(value) ? 0 : 1);
    }
    if (op == Op::Neg) {
        if (const auto* integer = std::get_if<int>(&value)) {
            if (*integer == std::numeric_limits<int>::min()) {
                throw RError("integer overflow", line);
            }
            return sme::Value::Integer(-*integer);
        }
        if (const auto* real = std::get_if<float>(&value)) {
            return sme::Value::Float(-*real);
        }
        throw RError("negation expects a number", line);
    }
    if (const auto* integer = std::get_if<int>(&value)) {
        return sme::Value::Integer(~*integer);
    }
    throw RError("bitwise not expects an int", line);
}

Value Machine::binary(Op op, const Value& lhs, const Value& rhs, int line) {
    if (lhs.index() == VT_INT && rhs.index() == VT_INT) {
        return intBinary(op, std::get<int>(lhs), std::get<int>(rhs), line);
    }
    if (op == Op::LogicAnd || op == Op::LogicOr) {
        bool left = state_.truthy(lhs);
        bool right = state_.truthy(rhs);
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
        const std::string* leftText = stringData(lhs);
        const std::string* rightText = stringData(rhs);
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
    if (op == Op::CmpEq) return Value::Integer(equals(lhs, rhs) ? 1 : 0);
    if (op == Op::CmpNe) return Value::Integer(equals(lhs, rhs) ? 0 : 1);
    if (op == Op::CmpLt || op == Op::CmpLe || op == Op::CmpGt || op == Op::CmpGe) {
        auto left = lhs.AsNumber();
        auto right = rhs.AsNumber();
        if (!left || !right) {
            throw RError("comparison expects numbers", line);
        }
        if (std::isnan(*left) || std::isnan(*right)) {
            return Value::Integer(0);
        }
        int order = compare(lhs, rhs, line);
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
        throw RError("operator expects numbers", line);
    }
    const bool floats = lhs.IsFloat() || rhs.IsFloat();
    const double a = *left;
    const double b = *right;

    auto bothInts = [&]() -> std::optional<std::pair<int, int>> {
        if (floats) return std::nullopt;
        auto x = asInt(lhs);
        auto y = asInt(rhs);
        if (!x || !y) return std::nullopt;
        return std::pair{*x, *y};
    };

    if (!floats && (op == Op::Add || op == Op::Sub || op == Op::Mul)) {
        auto pair = bothInts();
        if (!pair) {
            throw RError("operator expects numbers", line);
        }
        const int64_t x = pair->first;
        const int64_t y = pair->second;
        int64_t z = 0;
        if (op == Op::Add) z = x + y;
        else if (op == Op::Sub) z = x - y;
        else z = x * y;
        if (z > std::numeric_limits<int>::max() || z < std::numeric_limits<int>::min()) {
            throw RError("integer overflow", line);
        }
        return Value::Integer(static_cast<int>(z));
    }

    switch (op) {
    case Op::Add: return Value::Float(static_cast<float>(a + b));
    case Op::Sub: return Value::Float(static_cast<float>(a - b));
    case Op::Mul: return Value::Float(static_cast<float>(a * b));
    case Op::Div: {
        // INT_MIN / -1 непредставимо в int32. То же проверяется у остатка.
        if (b == 0) throw RError("division by zero", line);
        if (!floats) {
            auto pair = *bothInts();
            if (pair.first == std::numeric_limits<int>::min() && pair.second == -1) {
                throw RError("integer overflow", line);
            }
            return Value::Integer(pair.first / pair.second);
        }
        return Value::Float(static_cast<float>(a / b));
    }
    case Op::Mod: {
        if (b == 0) throw RError("division by zero", line);
        if (!floats) {
            auto pair = *bothInts();
            if (pair.first == std::numeric_limits<int>::min() && pair.second == -1) {
                throw RError("integer overflow", line);
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
        if (!pair) throw RError("bitwise operator expects ints", line);
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
            if (y < 0 || y >= 31) throw RError("shift out of range", line);
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
    throw RError("internal operator fault", line);
}

Value Machine::run(size_t function, std::span<const Value> args) {
    if (function >= state_.functions.size()) {
        throw RError("unknown function");
    }
    const Chunk& chunk = state_.functions[function];
    if (state_.runStack.size() >= SME_MAX_CALL_DEPTH) {
        throw RError("script call nested too deeply");
    }

    Frame frame;
    frame.function = function;
    frame.generation = state_.freshFrameId();
    frame.regs.resize(chunk.registerCount);
    const size_t supplied = std::min(args.size(), static_cast<size_t>(chunk.parameterCount));
    frame.supplied = static_cast<uint16_t>(supplied);
    for (size_t i = 0; i < supplied; ++i) {
        frame.regs[i] = args[i];
    }
    RunState fresh;
    fresh.frames.push_back(std::move(frame));
    state_.runStack.push_back(std::move(fresh));
    // Снимает этот RunState при return и при исключении. stop кадр оставляет.
    struct Guard {
        State& state;
        bool active = true;
        ~Guard() {
            if (active && !state.runStack.empty()) {
                state.runStack.pop_back();
            }
        }
        void keep() { active = false; }
    } guard{state_};
    std::optional<Value> result = execute();
    if (!result) {
        guard.keep();
        return Value::Null();
    }
    return result.value();
}

Value Machine::resume() {
    if (state_.runStack.empty()) {
        throw RError("nothing to resume");
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
        void keep() { active = false; }
    } guard{state_};
    std::optional<Value> result = execute();
    if (!result) {
        guard.keep();
        return Value::Null();
    }
    return result.value();
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

Value& referenced(State& state, const Value& value, int line) {
    if (!value.IsReference()) {
        throw RError("value is not a reference", line);
    }
    if (const auto* link = std::get_if<SharedValue>(&value)) {
        if (!*link) throw RError("dangling reference", line);
        return **link;
    }
    if (const auto* indexed = std::get_if<IndexedReference>(&value)) {
        if (!indexed->container) {
            throw RError("dangling reference", line);
        }
        if (indexed->index >= indexed->container->size()) {
            throw RError("dangling reference", line);
        }
        return (*indexed->container)[indexed->index];
    }
    if (const auto* global = std::get_if<GlobalReference>(&value)) {
        if (global->slot >= state.globals.size()) {
            throw RError("dangling reference", line);
        }
        return state.globals[global->slot].value;
    }
    const LocalReference& local = std::get<LocalReference>(value);
    if (state.runStack.empty() || local.frame >= state.runStack.back().frames.size()) {
        throw RError("dangling reference", line);
    }
    Frame& owner = state.runStack.back().frames[local.frame];
    if (owner.generation != local.frameGeneration || local.slot >= owner.regs.size()) {
        throw RError("dangling reference", line);
    }
    return owner.regs[local.slot];
}

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
std::optional<Value> Machine::execute() {
    while (true) {
        RunState& active = state_.runStack.back();
        if (active.frames.empty()) {
            throw RError("call stack underflow");
        }
        Frame& frame = active.frames.back();
        const Chunk& chunk = state_.functions[frame.function];
        if (frame.ip >= chunk.code.size()) {
            throw RError("function '" + chunk.name + "' ran out of instructions");
        }
        const Instruction& ins = chunk.code[frame.ip];

        auto bump = [&] { frame.ip++; };
        auto write = [&](uint16_t slot, Value value) { frame.regs[slot] = std::move(value); };

        switch (ins.op) {
        case Op::LoadConst: {
            const Constant& constant = chunk.constants[ins.b];
            if (const auto* number = std::get_if<int>(&constant)) frame.regs[ins.a] = Value::Integer(*number);
            else frame.regs[ins.a] = materialize(chunk, ins.b, ins.line);
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
            if (ins.b >= state_.globals.size()) throw RError("internal global fault", ins.line);
            write(ins.a, state_.globals[ins.b].value);
            bump();
            break;
        case Op::StoreGlobal: {
            if (ins.a >= state_.globals.size()) throw RError("internal global fault", ins.line);
            Value incoming = fastReg(frame, ins.b);
            if (!incoming.IsNull()) {
                state_.globals[ins.a].value = std::move(incoming);
            }
            bump();
            break;
        }
        case Op::Stop:
            // ip уже за stop, поэтому stopped() узнаёт паузу по предыдущей инструкции.
            bump();
            return std::nullopt;
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
                frame.regs[ins.a] = intBinary(ins.op, std::get<int>(lhs), std::get<int>(rhs), ins.line);
            } else {
                frame.regs[ins.a] = binary(ins.op, lhs, rhs, ins.line);
            }
            frame.ip++;
            break;
        }
        case Op::BinaryImm: {
            const int imm = static_cast<int16_t>(ins.d);
            const Value& lhs = frame.regs[ins.b];
            const Op op = static_cast<Op>(ins.c);
            if (lhs.index() == VT_INT) {
                frame.regs[ins.a] = intBinary(op, std::get<int>(lhs), imm, ins.line);
            } else {
                frame.regs[ins.a] = binary(op, lhs, Value::Integer(imm), ins.line);
            }
            frame.ip++;
            break;
        }
        case Op::Neg: case Op::Not: case Op::BitNot: {
            Value value = fastReg(frame, ins.b);
            write(ins.a, unary(ins.op, value, ins.line));
            bump();
            break;
        }
        case Op::Jump:
            frame.ip = ins.a;
            break;
        case Op::JumpIfFalse:
        case Op::JumpIfTrue: {
            bool flag = state_.truthy(fastReg(frame, ins.a));
            if ((ins.op == Op::JumpIfFalse && !flag) || (ins.op == Op::JumpIfTrue && flag)) {
                frame.ip = ins.b;
            } else {
                bump();
            }
            break;
        }
        case Op::JumpIfCmpFalse: {
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
                    else throw RError("internal operator fault", ins.line);
                } else if (op == Op::CmpEq) {
                    result = equals(lhs, rhs);
                } else if (op == Op::CmpNe) {
                    result = !equals(lhs, rhs);
                } else {
                    const auto leftNumber = lhs.AsNumber();
                    const auto rightNumber = rhs.AsNumber();
                    if (!leftNumber || !rightNumber) throw RError("comparison expects numbers", ins.line);
                    if (!std::isnan(*leftNumber) && !std::isnan(*rightNumber)) {
                        if (op == Op::CmpLt) result = *leftNumber < *rightNumber;
                        else if (op == Op::CmpLe) result = *leftNumber <= *rightNumber;
                        else if (op == Op::CmpGt) result = *leftNumber > *rightNumber;
                        else if (op == Op::CmpGe) result = *leftNumber >= *rightNumber;
                        else throw RError("internal operator fault", ins.line);
                    }
                }
            } else if (op == Op::CmpEq) {
                result = equals(lhs, rhs);
            } else if (op == Op::CmpNe) {
                result = !equals(lhs, rhs);
            } else {
                const auto left = lhs.AsNumber();
                const auto right = rhs.AsNumber();
                if (!left || !right) throw RError("comparison expects numbers", ins.line);
                if (!std::isnan(*left) && !std::isnan(*right)) {
                    if (op == Op::CmpLt) result = *left < *right;
                    else if (op == Op::CmpLe) result = *left <= *right;
                    else if (op == Op::CmpGt) result = *left > *right;
                    else if (op == Op::CmpGe) result = *left >= *right;
                    else throw RError("internal operator fault", ins.line);
                }
            }
            if (!result) frame.ip = ins.e;
            else bump();
            break;
        }
        case Op::JumpIfCmpImmFalse: {
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
                else throw RError("internal operator fault", ins.line);
            } else if (op == Op::CmpEq || op == Op::CmpNe) {
                bool equal = false;
                if (const auto number = lhs.AsNumber()) {
                    equal = *number == static_cast<float>(immediate);
                }
                result = op == Op::CmpEq ? equal : !equal;
            } else {
                const auto left = lhs.AsNumber();
                if (!left) throw RError("comparison expects numbers", ins.line);
                const float right = static_cast<float>(immediate);
                if (!std::isnan(*left)) {
                    if (op == Op::CmpLt) result = *left < right;
                    else if (op == Op::CmpLe) result = *left <= right;
                    else if (op == Op::CmpGt) result = *left > right;
                    else if (op == Op::CmpGe) result = *left >= right;
                    else throw RError("internal operator fault", ins.line);
                }
            }
            if (!result) frame.ip = ins.e;
            else bump();
            break;
        }
        case Op::JumpIfNotLe: {
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
            if (!left || !right) throw RError("comparison expects numbers", ins.line);
            if (!(*left <= *right)) frame.ip = ins.c;
            else bump();
            break;
        }
        case Op::IncJumpLe: {
            const Value& limit = fastReg(frame, ins.b);
            Value& counter = frame.regs[ins.a];
            if (const auto* left = std::get_if<int>(&counter); left) {
                if (*left == std::numeric_limits<int>::max()) throw RError("integer overflow", ins.line);
                const int incremented = *left + 1;
                counter = incremented;
                if (const auto* right = std::get_if<int>(&limit)) {
                    if (incremented <= *right) frame.ip = ins.c;
                    else bump();
                    break;
                }
                const auto right = limit.AsNumber();
                if (!right) throw RError("comparison expects numbers", ins.line);
                if (static_cast<double>(incremented) <= *right) frame.ip = ins.c;
                else bump();
                break;
            }
            Value next = binary(Op::Add, counter, Value::Integer(1), ins.line);
            counter = std::move(next);
            if (const auto* left = std::get_if<int>(&counter); left) {
                if (const auto* right = std::get_if<int>(&limit); right) {
                    if (*left <= *right) frame.ip = ins.c;
                    else bump();
                    break;
                }
            }
            const auto left = counter.AsNumber();
            const auto right = limit.AsNumber();
            if (!left || !right) throw RError("comparison expects numbers", ins.line);
            if (*left <= *right) frame.ip = ins.c;
            else bump();
            break;
        }
        case Op::Call:
        case Op::CallNative: {
            if (ins.d > 0 && static_cast<size_t>(ins.c) + ins.d > frame.regs.size()) {
                throw RError("internal argument fault", ins.line);
            }
            const std::span<const Value> args(frame.regs.data() + ins.c, ins.d);
            bump();
            if (ins.op == Op::CallNative) {
                if (ins.b >= state_.natives.size()) throw RError("internal native fault", ins.line);
                Value result = state_.natives[ins.b](engine_, args);
                state_.runStack.back().frames.back().regs[ins.a] = std::move(result);
                break;
            }
            if (ins.b >= state_.functions.size()) throw RError("internal function fault", ins.line);
            const Chunk& callee = state_.functions[ins.b];
            Frame next;
            next.function = ins.b;
            next.generation = state_.freshFrameId();
            next.writesParent = true;
            next.parentDest = ins.a;
            next.regs.resize(callee.registerCount);
            const uint16_t count = std::min(ins.d, callee.parameterCount);
            next.supplied = count;
            for (uint16_t i = 0; i < count && i < next.regs.size(); ++i) {
                next.regs[i] = args[i];
            }
            state_.runStack.back().frames.push_back(std::move(next));
            if (state_.runStack.back().frames.size() > SME_MAX_RECURSION_DEPTH) {
                throw RError("script recursion is too deep");
            }
            break;
        }
        case Op::Return: {
            Value result = fastReg(frame, ins.a);
            bool writeParent = frame.writesParent;
            uint16_t dest = frame.parentDest;
            state_.runStack.back().frames.pop_back();
            if (state_.runStack.back().frames.empty()) {
                return result;
            }
            if (writeParent) {
                state_.runStack.back().frames.back().regs[dest] = std::move(result);
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
            if (!slot || *slot < 0) throw RError("index must be a non-negative int", ins.line);
            if (const auto* text = stringData(object)) {
                if (static_cast<size_t>(*slot) >= text->size()) throw RError("string index out of range", ins.line);
                write(ins.a, Value::String(std::string(1, (*text)[static_cast<size_t>(*slot)])));
                bump();
                break;
            }
            if (!object.IsTable()) {
                throw RError("value cannot be indexed", ins.line);
            }
            const TableHandle& table = std::get<TableHandle>(object);
            if (static_cast<size_t>(*slot) >= table->size()) throw RError("table index out of range", ins.line);
            write(ins.a, (*table)[static_cast<size_t>(*slot)]);
            bump();
            break;
        }
        case Op::SetIndex: {
            Value index = fastReg(frame, ins.b);
            Value value = fastReg(frame, ins.c);
            Value object = fastReg(frame, ins.a);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) throw RError("index must be a non-negative int", ins.line);
            if (!object.IsTable()) throw RError("value cannot be indexed", ins.line);
            TableHandle container = std::get<TableHandle>(object);
            if (static_cast<size_t>(*slot) >= container->size()) throw RError("table index out of range", ins.line);
            if (createsCycle(value, container)) throw RError("cyclic table ownership is not supported", ins.line);
            (*container)[static_cast<size_t>(*slot)] = std::move(value);
            bump();
            break;
        }
        case Op::GetProp:
        case Op::SetProp:
        case Op::CallMethod: {
            const std::string_view name = state_.constantString(chunk, ins.op == Op::SetProp ? ins.b : ins.c, ins.line);
            Value object = fastReg(frame, ins.op == Op::SetProp ? ins.a : ins.b);
            if (!object.IsObject()) throw RError("value has no properties", ins.line);
            IScriptObject* target = std::get<Handle>(object).get();
            if (ins.op == Op::GetProp) {
                Value result = target->GetProperty(engine_, name);
                write(ins.a, std::move(result));
                bump();
                break;
            }
            if (ins.op == Op::SetProp) {
                Value incoming = fastReg(frame, ins.c);
                target->SetProperty(engine_, name, incoming);
                bump();
                break;
            }
            const std::span<const Value> args(frame.regs.data() + ins.d, ins.e);
            bump();
            Value result = target->Call(engine_, name, args);
            state_.runStack.back().frames.back().regs[ins.a] = std::move(result);
            break;
        }
        case Op::MakeRefLocal: {
            // Ссылочный строковый слот получает разделяемое стабильное хранилище.
            auto& frames = state_.runStack.back().frames;
            promoteString(frames.back().regs[ins.b]);
            write(ins.a, Value(std::in_place_type<LocalReference>,
                LocalReference{static_cast<uint32_t>(frames.size() - 1), frames.back().generation,
                    ins.b, state_.freshReferenceId()}));
            bump();
            break;
        }
        case Op::MakeRefIndex: {
            const Value& object = fastReg(frame, ins.b);
            const Value& index = fastReg(frame, ins.c);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || !object.IsTable()) {
                throw RError("address expects a table element", ins.line);
            }
            const TableHandle& container = std::get<TableHandle>(object);
            const uint32_t tableIndex = static_cast<uint32_t>(*slot);
            if (tableIndex < container->size()) {
                promoteString((*container)[tableIndex]);
            }
            write(ins.a, Value(std::in_place_type<IndexedReference>,
                IndexedReference{container, tableIndex, state_.freshReferenceId()}));
            bump();
            break;
        }
        case Op::MakeRefGlobal: {
            const size_t slot = ins.b;
            if (slot < state_.globals.size()) promoteString(state_.globals[slot].value);
            write(ins.a, Value(std::in_place_type<GlobalReference>,
                GlobalReference{static_cast<uint32_t>(slot), state_.freshReferenceId()}));
            bump();
            break;
        }
        case Op::LoadRef: {
            Value ref = fastReg(frame, ins.b);
            Value& target = referenced(state_, ref, ins.line);
            promoteString(target);
            write(ins.a, target);
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
                        throw RError("cyclic table ownership is not supported", ins.line);
                    }
                }
                Value& target = referenced(state_, ref, ins.line);
                target = std::move(incoming);
                promoteString(target);
            }
            bump();
            break;
        }
        case Op::RefGlobal: {
            Value index = fastReg(frame, ins.b);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || static_cast<size_t>(*slot) >= state_.globals.size()) {
                throw RError("global index out of range", ins.line);
            }
            const uint32_t globalSlot = static_cast<uint32_t>(*slot);
            promoteString(state_.globals[globalSlot].value);
            write(ins.a, Value(std::in_place_type<GlobalReference>,
                GlobalReference{globalSlot, state_.freshReferenceId()}));
            bump();
            break;
        }
        case Op::GetChar: {
            Value object = fastReg(frame, ins.b);
            Value index = fastReg(frame, ins.c);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || !object.IsString()) {
                throw RError("character index expects a string", ins.line);
            }
            const std::string text = object.ToString();
            if (static_cast<size_t>(*slot) >= text.size()) throw RError("string index out of range", ins.line);
            write(ins.a, Value::Integer(static_cast<unsigned char>(text[static_cast<size_t>(*slot)])));
            bump();
            break;
        }
        case Op::SetChar: {
            Value& object = fastReg(frame, ins.a);
            Value index = fastReg(frame, ins.b);
            Value incoming = fastReg(frame, ins.c);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) throw RError("character index expects a string", ins.line);
            char byte = 0;
            if (const auto* number = std::get_if<int>(&incoming)) byte = static_cast<char>(*number);
            else if (const auto* text = stringData(incoming)) {
                if (!text->empty()) byte = (*text)[0];
            } else if (incoming.IsString() && !incoming.ToString().empty()) byte = incoming.ToString()[0];
            auto* stored = mutableStringData(object);
            if (!stored) {
                throw RError("character index expects a string", ins.line);
            }
            if (static_cast<size_t>(*slot) >= stored->size()) throw RError("string index out of range", ins.line);
            (*stored)[static_cast<size_t>(*slot)] = byte;
            bump();
            break;
        }
        case Op::NewObj: {
            const std::string_view type = state_.constantString(chunk, ins.b, ins.line);
            auto factory = state_.factories.find(std::string(type));
            if (factory == state_.factories.end() || !factory->second) {
                throw RError("unknown type '" + std::string(type) + "'", ins.line);
            }
            std::shared_ptr<IScriptObject> object = factory->second();
            if (!object) {
                throw RError("unknown type '" + std::string(type) + "'", ins.line);
            }
            write(ins.a, Value(std::move(object)));
            bump();
            break;
        }
        case Op::Cast: {
            Value value = fastReg(frame, ins.b);
            std::string format;
            if (ins.d != 65535) {
                format = std::string(state_.constantString(chunk, ins.d, ins.line));
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
                else if (const auto* realValue = std::get_if<float>(&value)) write(ins.a, Value::Integer(narrowInt(*realValue, ins.line)));
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
            // Имя ищется линейно: сначала скриптовые функции, потом нативы.
            // Нет ни там, ни там — в регистр пишется int 0, не null и не ошибка.
            const std::string_view name = state_.constantString(chunk, ins.b, ins.line);
            if (ins.d > 0 && static_cast<size_t>(ins.c) + ins.d > frame.regs.size()) {
                throw RError("internal argument fault", ins.line);
            }
            const std::span<const Value> args(frame.regs.data() + ins.c, ins.d);
            bump();
            auto function = std::find_if(state_.functions.begin(), state_.functions.end(), [&](const Chunk& item) {
                return item.name == name;
            });
            if (function != state_.functions.end()) {
                const auto index = static_cast<size_t>(std::distance(state_.functions.begin(), function));
                Frame next;
                next.function = index;
                next.generation = state_.freshFrameId();
                next.writesParent = true;
                next.parentDest = ins.a;
                next.regs.resize(function->registerCount);
                const uint16_t count = std::min<uint16_t>(ins.d, function->parameterCount);
                next.supplied = count;
                for (uint16_t i = 0; i < count && i < next.regs.size(); ++i) {
                    next.regs[i] = args[i];
                }
                state_.runStack.back().frames.push_back(std::move(next));
                if (state_.runStack.back().frames.size() > SME_MAX_RECURSION_DEPTH) {
                    throw RError("script recursion is too deep");
                }
                break;
            }
            auto native = std::find(state_.nativeNames.begin(), state_.nativeNames.end(), name);
            if (native != state_.nativeNames.end()) {
                const auto index = static_cast<size_t>(std::distance(state_.nativeNames.begin(), native));
                write(ins.a, state_.natives[index](engine_, args));
                break;
            }
            write(ins.a, Value::Integer(0));
            break;
        }
        default:
            throw RError("unknown instruction", ins.line);
        }
    }
}

}  // пространство имён sme
