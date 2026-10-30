#include "sme_machine.h"

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

struct Number {
    bool isFloat = false;
    double value = 0;
};

std::optional<Number> asNumber(const Value& value) {
    if (const auto* integer = std::get_if<int>(&value)) {
        return Number{false, static_cast<double>(*integer)};
    }
    if (const auto* real = std::get_if<float>(&value)) {
        return Number{true, static_cast<double>(*real)};
    }
    if (const auto* flag = std::get_if<bool>(&value)) {
        return Number{false, *flag ? 1.0 : 0.0};
    }
    return std::nullopt;
}

Value intBinary(Op op, int x, int y, int line) {
    auto overflow = [&](int64_t z) {
        if (z > std::numeric_limits<int>::max() || z < std::numeric_limits<int>::min()) {
            throw RError("integer overflow", line);
        }
        return integer(static_cast<int>(z));
    };
    switch (op) {
    case Op::Add: return overflow(static_cast<int64_t>(x) + y);
    case Op::Sub: return overflow(static_cast<int64_t>(x) - y);
    case Op::Mul: return overflow(static_cast<int64_t>(x) * y);
    case Op::Div:
        if (y == 0) throw RError("division by zero", line);
        if (x == std::numeric_limits<int>::min() && y == -1) throw RError("integer overflow", line);
        return integer(x / y);
    case Op::Mod:
        if (y == 0) throw RError("division by zero", line);
        if (x == std::numeric_limits<int>::min() && y == -1) throw RError("integer overflow", line);
        return integer(x % y);
    case Op::BitAnd: return integer(x & y);
    case Op::BitOr: return integer(x | y);
    case Op::BitXor: return integer(x ^ y);
    case Op::Shl:
    case Op::Shr:
        if (y < 0 || y >= 31) throw RError("shift out of range", line);
        if (op == Op::Shl) return integer(static_cast<int>(static_cast<uint32_t>(x) << y));
        return integer(static_cast<int>(static_cast<uint32_t>(x) >> static_cast<uint32_t>(y)));
    case Op::CmpEq: return integer(x == y ? 1 : 0);
    case Op::CmpNe: return integer(x != y ? 1 : 0);
    case Op::CmpLt: return integer(x < y ? 1 : 0);
    case Op::CmpLe: return integer(x <= y ? 1 : 0);
    case Op::CmpGt: return integer(x > y ? 1 : 0);
    case Op::CmpGe: return integer(x >= y ? 1 : 0);
    case Op::LogicAnd: return integer((x != 0 && y != 0) ? 1 : 0);
    case Op::LogicOr: return integer((x != 0 || y != 0) ? 1 : 0);
    default:
        throw RError("internal operator fault", line);
    }
}

std::vector<Value> takeRegs(State& state, size_t count) {
    std::vector<Value> regs;
    if (!state.regPool.empty()) {
        regs = std::move(state.regPool.back());
        state.regPool.pop_back();
    }
    regs.assign(count, Value{});
    return regs;
}

void giveRegs(State& state, std::vector<Value> regs) {
    if (state.regPool.size() < 64) {
        state.regPool.push_back(std::move(regs));
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

}  // анонимное пространство имён

Machine::Machine(Engine& engine, State& state) : engine_(engine), state_(state) {}

Value& Machine::reg(Frame& frame, size_t index, int line) {
    if (index >= frame.regs.size()) {
        throw RError("internal register fault", line);
    }
    return frame.regs[index];
}

Value Machine::materialize(const Chunk& chunk, size_t constant, int line) {
    if (constant >= chunk.constants.size()) {
        throw RError("internal constant fault", line);
    }
    const Constant& value = chunk.constants[constant];
    if (std::holds_alternative<std::monostate>(value)) return null();
    if (const auto* integer = std::get_if<int>(&value)) return sme::integer(*integer);
    if (const auto* real = std::get_if<float>(&value)) return sme::real(*real);
    if (const auto* flag = std::get_if<bool>(&value)) return boolean(*flag);
    if (const auto* text = std::get_if<std::string>(&value)) {
        if (chunk.interned.size() != chunk.constants.size()) {
            chunk.interned.resize(chunk.constants.size());
        }
        Value& cached = chunk.interned[constant];
        if (!std::holds_alternative<std::monostate>(cached)) {
            if (const auto* handle = std::get_if<Handle>(&cached); !handle || state_.heap.check(*handle)) {
                return cached;
            }
        }
        cached = state_.makeString(*text);
        return cached;
    }
    if (std::holds_alternative<Pointer>(value)) return pointer();
    throw RError("internal constant fault", line);
}

bool Machine::equals(const Value& lhs, const Value& rhs) {
    if (auto left = asNumber(lhs)) {
        if (auto right = asNumber(rhs)) {
            return left->value == right->value;
        }
    }
    auto readText = [&](const Value& value, std::string& out) {
        if (const auto* text = std::get_if<ShortStr>(&value)) {
            out.assign(text->data, text->len);
            return true;
        }
        const auto* handle = std::get_if<Handle>(&value);
        if (!handle || !state_.heap.check(*handle)) return false;
        const auto* stored = std::get_if<std::string>(&state_.heap.at(*handle));
        if (!stored) return false;
        out = *stored;
        return true;
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
    if (std::holds_alternative<std::monostate>(lhs)) return true;
    if (std::holds_alternative<Pointer>(lhs)) return true;
    if (const auto* flag = std::get_if<bool>(&lhs)) return *flag == std::get<bool>(rhs);
    if (const auto* handle = std::get_if<Handle>(&lhs)) {
        const Handle& other = std::get<Handle>(rhs);
        if (!state_.heap.check(*handle) || !state_.heap.check(other)) return false;
        const Object& left = state_.heap.at(*handle);
        const Object& right = state_.heap.at(other);
        if (const auto* text = std::get_if<std::string>(&left)) {
            const auto* otherText = std::get_if<std::string>(&right);
            return otherText && *text == *otherText;
        }
        return *handle == other;
    }
    return false;
}

// NaN ни меньше, ни больше, ни равен. == идёт через equals и для NaN уже ложен;
// <= и >= без этой проверки считали бы «не меньше и не больше» равенством.
int Machine::compare(const Value& lhs, const Value& rhs, int line) {
    auto left = asNumber(lhs);
    auto right = asNumber(rhs);
    if (!left || !right) {
        throw RError("comparison expects numbers", line);
    }
    if (left->value < right->value) return -1;
    if (left->value > right->value) return 1;
    return 0;
}

Value Machine::unary(Op op, const Value& value, int line) {
    if (op == Op::Not) {
        return integer(state_.truthy(value) ? 0 : 1);
    }
    if (op == Op::Neg) {
        if (const auto* integer = std::get_if<int>(&value)) {
            if (*integer == std::numeric_limits<int>::min()) {
                throw RError("integer overflow", line);
            }
            return sme::integer(-*integer);
        }
        if (const auto* real = std::get_if<float>(&value)) {
            return sme::real(-*real);
        }
        throw RError("negation expects a number", line);
    }
    if (const auto* integer = std::get_if<int>(&value)) {
        return sme::integer(~*integer);
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
        return integer(result ? 1 : 0);
    }
    if (op == Op::Add && state_.typeOf(lhs) == Type::Table && state_.typeOf(rhs) == Type::Table) {
        Table joined = std::get<Table>(state_.heap.at(std::get<Handle>(lhs)));
        const Table& extra = std::get<Table>(state_.heap.at(std::get<Handle>(rhs)));
        joined.items.insert(joined.items.end(), extra.items.begin(), extra.items.end());
        return Value{state_.heap.allocate(std::move(joined))};
    }
    if (op == Op::Add) {
        auto textOf = [&](const Value& value) -> std::optional<std::string> {
            if (const auto* text = std::get_if<ShortStr>(&value)) {
                return std::string(text->data, text->len);
            }
            const auto* handle = std::get_if<Handle>(&value);
            if (!handle || !state_.heap.check(*handle)) return std::nullopt;
            const auto* stored = std::get_if<std::string>(&state_.heap.at(*handle));
            if (!stored) return std::nullopt;
            return *stored;
        };
        auto leftText = textOf(lhs);
        auto rightText = textOf(rhs);
        if (leftText || rightText) {
            const std::string left = leftText ? *leftText : state_.toString(lhs);
            const std::string right = rightText ? *rightText : state_.toString(rhs);
            return state_.makeString(left + right);
        }
    }
    if (op == Op::CmpEq) return integer(equals(lhs, rhs) ? 1 : 0);
    if (op == Op::CmpNe) return integer(equals(lhs, rhs) ? 0 : 1);
    if (op == Op::CmpLt || op == Op::CmpLe || op == Op::CmpGt || op == Op::CmpGe) {
        auto left = asNumber(lhs);
        auto right = asNumber(rhs);
        if (!left || !right) {
            throw RError("comparison expects numbers", line);
        }
        if (std::isnan(left->value) || std::isnan(right->value)) {
            return integer(0);
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
        return integer(result ? 1 : 0);
    }

    auto left = asNumber(lhs);
    auto right = asNumber(rhs);
    if (!left || !right) {
        throw RError("operator expects numbers", line);
    }
    const bool floats = left->isFloat || right->isFloat;
    const double a = left->value;
    const double b = right->value;

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
        return integer(static_cast<int>(z));
    }

    switch (op) {
    case Op::Add: return real(static_cast<float>(a + b));
    case Op::Sub: return real(static_cast<float>(a - b));
    case Op::Mul: return real(static_cast<float>(a * b));
    case Op::Div: {
        // INT_MIN / -1 непредставимо в int32. То же проверяется у остатка.
        if (b == 0) throw RError("division by zero", line);
        if (!floats) {
            auto pair = *bothInts();
            if (pair.first == std::numeric_limits<int>::min() && pair.second == -1) {
                throw RError("integer overflow", line);
            }
            return integer(pair.first / pair.second);
        }
        return real(static_cast<float>(a / b));
    }
    case Op::Mod: {
        if (b == 0) throw RError("division by zero", line);
        if (!floats) {
            auto pair = *bothInts();
            if (pair.first == std::numeric_limits<int>::min() && pair.second == -1) {
                throw RError("integer overflow", line);
            }
            return integer(pair.first % pair.second);
        }
        return real(static_cast<float>(std::fmod(a, b)));
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
        case Op::BitAnd: return integer(x & y);
        case Op::BitOr: return integer(x | y);
        case Op::BitXor: return integer(x ^ y);
        case Op::Shl:
        case Op::Shr:
            // Сдвиг логический, через uint32. Бит 31 поставить нельзя: счётчик только 0..30.
            // Поэтому -8 >> 1 — это 2147483644, а не арифметические -4.
            if (y < 0 || y >= 31) throw RError("shift out of range", line);
            if (op == Op::Shl) {
                return integer(static_cast<int>(static_cast<uint32_t>(x) << y));
            }
            return integer(static_cast<int>(static_cast<uint32_t>(x) >> static_cast<uint32_t>(y)));
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
    if (state_.runStack.size() >= 64) {
        throw RError("script call nested too deeply");
    }

    Frame frame;
    frame.function = function;
    frame.generation = state_.freshFrameId();
    frame.regs = takeRegs(state_, chunk.registerCount);
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
        return null();
    }
    return *result;
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
        return null();
    }
    return *result;
}

namespace {

bool holdsRef(State& state, const Value& value) {
    const auto* handle = std::get_if<Handle>(&value);
    if (!handle || !state.heap.check(*handle)) {
        return false;
    }
    return std::holds_alternative<Reference>(state.heap.at(*handle));
}

Value& referenced(State& state, const Value& value, int line) {
    if (!holdsRef(state, value)) {
        throw RError("value is not a reference", line);
    }
    const Reference ref = std::get<Reference>(state.heap.at(std::get<Handle>(value)));
    if (ref.indexed) {
        if (!state.heap.check(ref.container)) {
            throw RError("dangling reference", line);
        }
        Object& object = state.heap.at(ref.container);
        auto* table = std::get_if<Table>(&object);
        if (!table || ref.index >= table->items.size()) {
            throw RError("dangling reference", line);
        }
        return table->items[ref.index];
    }
    if (ref.global) {
        if (ref.slot >= state.globals.size()) {
            throw RError("dangling reference", line);
        }
        return state.globals[ref.slot].value;
    }
    if (state.runStack.empty() || ref.frame >= state.runStack.back().frames.size()) {
        throw RError("dangling reference", line);
    }
    Frame& owner = state.runStack.back().frames[ref.frame];
    if (owner.generation != ref.frameGeneration || ref.slot >= owner.regs.size()) {
        throw RError("dangling reference", line);
    }
    return owner.regs[ref.slot];
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
        if (active.frames.size() > 256) {
            throw RError("script recursion is too deep");
        }
        Frame& frame = active.frames.back();
        const Chunk& chunk = state_.functions[frame.function];
        if (frame.ip >= chunk.code.size()) {
            throw RError("function '" + chunk.name + "' ran out of instructions");
        }
        const Instruction ins = chunk.code[frame.ip];

        auto bump = [&] { frame.ip++; };
        auto write = [&](uint16_t slot, Value value) { frame.regs[slot] = std::move(value); };

        switch (ins.op) {
        case Op::LoadConst: {
            const Constant& constant = chunk.constants[ins.b];
            if (const auto* number = std::get_if<int>(&constant)) frame.regs[ins.a] = integer(*number);
            else frame.regs[ins.a] = materialize(chunk, ins.b, ins.line);
            frame.ip++;
            break;
        }
        case Op::Move: {
            // null не затирает регистр. Так a = maybe(0) оставляет прежнее a,
            // если maybe вернул null.
            Value incoming = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            if (!std::holds_alternative<std::monostate>(incoming)) {
                write(ins.a, std::move(incoming));
            }
            bump();
            break;
        }
        case Op::Copy: {
            write(ins.a, reg(state_.runStack.back().frames.back(), ins.b, ins.line));
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
            Value incoming = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            if (!std::holds_alternative<std::monostate>(incoming)) {
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
            write(ins.a, integer(engine_.typeCode(reg(frame, ins.b, ins.line))));
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
                frame.regs[ins.a] = binary(op, lhs, integer(imm), ins.line);
            }
            frame.ip++;
            break;
        }
        case Op::Neg: case Op::Not: case Op::BitNot: {
            Value value = reg(frame, ins.b, ins.line);
            write(ins.a, unary(ins.op, value, ins.line));
            bump();
            break;
        }
        case Op::Jump:
            frame.ip = ins.a;
            break;
        case Op::JumpIfFalse:
        case Op::JumpIfTrue: {
            bool flag = state_.truthy(reg(frame, ins.a, ins.line));
            if ((ins.op == Op::JumpIfFalse && !flag) || (ins.op == Op::JumpIfTrue && flag)) {
                frame.ip = ins.b;
            } else {
                bump();
            }
            break;
        }
        case Op::Call:
        case Op::CallNative: {
            if (ins.d > 0 && static_cast<size_t>(ins.c) + ins.d > frame.regs.size()) {
                throw RError("internal argument fault", ins.line);
            }
            Value smallArgs[8];
            std::vector<Value> heapArgs;
            std::span<const Value> args;
            if (ins.d <= 8) {
                for (uint16_t i = 0; i < ins.d; ++i) {
                    smallArgs[i] = frame.regs[ins.c + i];
                }
                args = std::span<const Value>(smallArgs, ins.d);
            } else {
                heapArgs.reserve(ins.d);
                for (uint16_t i = 0; i < ins.d; ++i) {
                    heapArgs.push_back(frame.regs[ins.c + i]);
                }
                args = heapArgs;
            }
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
            next.regs = takeRegs(state_, callee.registerCount);
            const uint16_t count = std::min(ins.d, callee.parameterCount);
            next.supplied = count;
            for (uint16_t i = 0; i < count && i < next.regs.size(); ++i) {
                next.regs[i] = args[i];
            }
            state_.runStack.back().frames.push_back(std::move(next));
            break;
        }
        case Op::Return: {
            Value result = reg(frame, ins.a, ins.line);
            bool writeParent = frame.writesParent;
            uint16_t dest = frame.parentDest;
            giveRegs(state_, std::move(state_.runStack.back().frames.back().regs));
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
                items.push_back(reg(frame, static_cast<uint16_t>(ins.b + i), ins.line));
            }
            Handle handle = state_.heap.allocate(Table{std::move(items)});
            write(ins.a, Value{handle});
            bump();
            break;
        }
        case Op::GetIndex: {
            Value object = reg(frame, ins.b, ins.line);
            Value index = reg(frame, ins.c, ins.line);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) throw RError("index must be a non-negative int", ins.line);
            if (const auto* text = std::get_if<ShortStr>(&object)) {
                if (static_cast<size_t>(*slot) >= text->len) throw RError("string index out of range", ins.line);
                write(ins.a, shortString(text->data + *slot, 1));
                bump();
                break;
            }
            if (!std::holds_alternative<Handle>(object) || !state_.heap.check(std::get<Handle>(object))) {
                throw RError("value cannot be indexed", ins.line);
            }
            const Object& stored = state_.heap.at(std::get<Handle>(object));
            if (const auto* text = std::get_if<std::string>(&stored)) {
                if (static_cast<size_t>(*slot) >= text->size()) throw RError("string index out of range", ins.line);
                write(ins.a, state_.makeString(std::string(1, (*text)[static_cast<size_t>(*slot)])));
            } else if (const auto* table = std::get_if<Table>(&stored)) {
                if (static_cast<size_t>(*slot) >= table->items.size()) throw RError("table index out of range", ins.line);
                write(ins.a, table->items[static_cast<size_t>(*slot)]);
            } else {
                throw RError("value cannot be indexed", ins.line);
            }
            bump();
            break;
        }
        case Op::SetIndex: {
            Value index = reg(frame, ins.b, ins.line);
            Value value = reg(frame, ins.c, ins.line);
            Value object = reg(frame, ins.a, ins.line);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) throw RError("index must be a non-negative int", ins.line);
            if (!std::holds_alternative<Handle>(object)) throw RError("value cannot be indexed", ins.line);
            Object& stored = state_.heap.at(std::get<Handle>(object));
            auto* table = std::get_if<Table>(&stored);
            if (!table) throw RError("only tables can be assigned by index", ins.line);
            if (static_cast<size_t>(*slot) >= table->items.size()) throw RError("table index out of range", ins.line);
            table->items[static_cast<size_t>(*slot)] = std::move(value);
            bump();
            break;
        }
        case Op::GetProp:
        case Op::SetProp:
        case Op::CallMethod: {
            const std::string_view name = state_.constantString(chunk, ins.op == Op::SetProp ? ins.b : ins.c, ins.line);
            Value object = reg(frame, ins.op == Op::SetProp ? ins.a : ins.b, ins.line);
            if (!std::holds_alternative<Handle>(object)) throw RError("value has no properties", ins.line);
            Object& stored = state_.heap.at(std::get<Handle>(object));
            auto* host = std::get_if<HostBox>(&stored);
            if (!host || !host->object) throw RError("value has no properties", ins.line);
            HostObject* target = host->object.get();
            if (ins.op == Op::GetProp) {
                Value result = target->get(engine_, name);
                write(ins.a, std::move(result));
                bump();
                break;
            }
            if (ins.op == Op::SetProp) {
                Value incoming = reg(state_.runStack.back().frames.back(), ins.c, ins.line);
                target->set(engine_, name, incoming);
                bump();
                break;
            }
            Value smallArgs[8];
            std::vector<Value> heapArgs;
            std::span<const Value> args;
            if (ins.e <= 8) {
                for (uint16_t i = 0; i < ins.e; ++i) smallArgs[i] = frame.regs[ins.d + i];
                args = std::span<const Value>(smallArgs, ins.e);
            } else {
                heapArgs.reserve(ins.e);
                for (uint16_t i = 0; i < ins.e; ++i) heapArgs.push_back(frame.regs[ins.d + i]);
                args = heapArgs;
            }
            bump();
            Value result = target->call(engine_, name, args);
            state_.runStack.back().frames.back().regs[ins.a] = std::move(result);
            break;
        }
        case Op::MakeRefLocal: {
            // Поколение кадра запоминается вместе с индексом. После return
            // новый вызов может занять тот же индекс, но поколение уже другое.
            Reference ref;
            const auto& frames = state_.runStack.back().frames;
            ref.frame = frames.size() - 1;
            ref.frameGeneration = frames.back().generation;
            ref.slot = ins.b;
            write(ins.a, Value{state_.heap.allocate(ref)});
            bump();
            break;
        }
        case Op::MakeRefIndex: {
            Value object = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            Value index = reg(state_.runStack.back().frames.back(), ins.c, ins.line);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || !std::holds_alternative<Handle>(object)) {
                throw RError("address expects a table element", ins.line);
            }
            Reference ref;
            ref.indexed = true;
            ref.container = std::get<Handle>(object);
            ref.index = static_cast<size_t>(*slot);
            write(ins.a, Value{state_.heap.allocate(ref)});
            bump();
            break;
        }
        case Op::MakeRefGlobal: {
            Reference ref;
            ref.global = true;
            ref.slot = ins.b;
            write(ins.a, Value{state_.heap.allocate(ref)});
            bump();
            break;
        }
        case Op::LoadRef: {
            Value ref = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            write(ins.a, referenced(state_, ref, ins.line));
            bump();
            break;
        }
        case Op::StoreRef: {
            Value ref = reg(state_.runStack.back().frames.back(), ins.a, ins.line);
            Value incoming = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            if (!std::holds_alternative<std::monostate>(incoming)) {
                referenced(state_, ref, ins.line) = std::move(incoming);
            }
            bump();
            break;
        }
        case Op::RefGlobal: {
            Value index = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || static_cast<size_t>(*slot) >= state_.globals.size()) {
                throw RError("global index out of range", ins.line);
            }
            Reference ref;
            ref.global = true;
            ref.slot = static_cast<size_t>(*slot);
            write(ins.a, Value{state_.heap.allocate(ref)});
            bump();
            break;
        }
        case Op::GetChar: {
            Value object = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            Value index = reg(state_.runStack.back().frames.back(), ins.c, ins.line);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0 || state_.typeOf(object) != Type::String) {
                throw RError("character index expects a string", ins.line);
            }
            const std::string& text = state_.stringOf(object);
            if (static_cast<size_t>(*slot) >= text.size()) throw RError("string index out of range", ins.line);
            write(ins.a, integer(static_cast<unsigned char>(text[static_cast<size_t>(*slot)])));
            bump();
            break;
        }
        case Op::SetChar: {
            Value& object = reg(state_.runStack.back().frames.back(), ins.a, ins.line);
            Value index = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            Value incoming = reg(state_.runStack.back().frames.back(), ins.c, ins.line);
            const auto* slot = std::get_if<int>(&index);
            if (!slot || *slot < 0) throw RError("character index expects a string", ins.line);
            char byte = 0;
            if (const auto* number = std::get_if<int>(&incoming)) byte = static_cast<char>(*number);
            else if (const auto* text = std::get_if<ShortStr>(&incoming)) {
                if (text->len != 0) byte = text->data[0];
            } else if (state_.typeOf(incoming) == Type::String && !state_.stringOf(incoming).empty()) byte = state_.stringOf(incoming)[0];
            if (auto* text = std::get_if<ShortStr>(&object)) {
                if (static_cast<size_t>(*slot) >= text->len) throw RError("string index out of range", ins.line);
                text->data[static_cast<size_t>(*slot)] = byte;
                bump();
                break;
            }
            if (!std::holds_alternative<Handle>(object) || state_.typeOf(object) != Type::String) {
                throw RError("character index expects a string", ins.line);
            }
            std::string& stored = std::get<std::string>(state_.heap.at(std::get<Handle>(object)));
            if (static_cast<size_t>(*slot) >= stored.size()) throw RError("string index out of range", ins.line);
            stored[static_cast<size_t>(*slot)] = byte;
            bump();
            break;
        }
        case Op::NewObj: {
            const std::string_view type = state_.constantString(chunk, ins.b, ins.line);
            auto factory = state_.factories.find(std::string(type));
            if (factory == state_.factories.end() || !factory->second) {
                throw RError("unknown type '" + std::string(type) + "'", ins.line);
            }
            std::shared_ptr<HostObject> object = factory->second();
            if (!object) {
                throw RError("unknown type '" + std::string(type) + "'", ins.line);
            }
            write(ins.a, Value{state_.heap.allocate(HostBox{std::move(object)})});
            bump();
            break;
        }
        case Op::Cast: {
            Value value = reg(state_.runStack.back().frames.back(), ins.b, ins.line);
            std::string format;
            if (ins.d != 65535) {
                format = std::string(state_.constantString(chunk, ins.d, ins.line));
            }
            if (ins.c == 2 && state_.typeOf(value) == Type::String) {
                write(ins.a, value);
                bump();
                break;
            }
            if (ins.c == 2) {
                if (const auto* integerValue = std::get_if<int>(&value)) {
                    write(ins.a, state_.makeString(castIntToString(*integerValue, format)));
                } else if (const auto* realValue = std::get_if<float>(&value)) {
                    write(ins.a, state_.makeString(castFloatToString(*realValue, format)));
                } else if (std::holds_alternative<std::monostate>(value)) {
                    write(ins.a, state_.makeString(castIntToString(0, format)));
                } else {
                    write(ins.a, state_.makeString(state_.toString(value)));
                }
                bump();
                break;
            }
            if (ins.c == 0) {
                if (const auto* integerValue = std::get_if<int>(&value)) write(ins.a, integer(*integerValue));
                else if (const auto* realValue = std::get_if<float>(&value)) write(ins.a, integer(narrowInt(*realValue, ins.line)));
                else if (const auto* flag = std::get_if<bool>(&value)) write(ins.a, integer(*flag ? 1 : 0));
                else if (state_.typeOf(value) == Type::String) {
                    write(ins.a, integer(castStringToInt(state_.stringOf(value), format)));
                } else write(ins.a, integer(0));
                bump();
                break;
            }
            if (const auto* realValue = std::get_if<float>(&value)) write(ins.a, real(*realValue));
            else if (const auto* integerValue = std::get_if<int>(&value)) write(ins.a, real(static_cast<float>(*integerValue)));
            else if (state_.typeOf(value) == Type::String) {
                write(ins.a, real(castStringToFloat(state_.stringOf(value), format)));
            } else write(ins.a, real(0.f));
            bump();
            break;
        }
        case Op::CallName: {
            // Имя ищется линейно: сначала скриптовые функции, потом нативы.
            // Нет ни там, ни там — в регистр пишется int 0, не null и не ошибка.
            const std::string_view name = state_.constantString(chunk, ins.b, ins.line);
            std::vector<Value> args;
            args.reserve(ins.d);
            for (uint16_t i = 0; i < ins.d; ++i) {
                args.push_back(frame.regs[ins.c + i]);
            }
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
                next.regs = takeRegs(state_, function->registerCount);
                const uint16_t count = std::min<uint16_t>(ins.d, function->parameterCount);
                next.supplied = count;
                for (uint16_t i = 0; i < count && i < next.regs.size(); ++i) {
                    next.regs[i] = std::move(args[i]);
                }
                state_.runStack.back().frames.push_back(std::move(next));
                break;
            }
            auto native = std::find(state_.nativeNames.begin(), state_.nativeNames.end(), name);
            if (native != state_.nativeNames.end()) {
                const auto index = static_cast<size_t>(std::distance(state_.nativeNames.begin(), native));
                write(ins.a, state_.natives[index](engine_, args));
                break;
            }
            write(ins.a, integer(0));
            break;
        }
        default:
            throw RError("unknown instruction", ins.line);
        }
    }
}

}  // пространство имён sme
