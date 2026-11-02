#pragma once

#include "sme_state.h"

#include <optional>
#include <span>

namespace sme {

// Интерпретатор байткода. Один объект на вызов: состояние живёт в Engine.
class Machine {
public:
    Machine(Engine& engine, State& state);

    // Новый RunState. Недостающие аргументы остаются null, лишние игнорируются.
    // Завершение/ошибка снимает только этот RunState; stop оставляет его на стеке.
    error::Result<Value> Run(size_t function, std::span<const Value> args);
    // Продолжить верхний RunState, который стоит на stop.
    error::Result<Value> Resume();

private:
    // nullopt значит «встали на stop», значение — нормальный return наружу.
    error::Result<std::optional<Value>> Execute();
    int CheckedIntAdd(int x, int y);
    int CheckedIntSub(int x, int y);
    int CheckedIntMul(int x, int y);
    Value IntBinary(Op op, int x, int y);
    int NarrowInt(double number);
    Value* Referenced(const Value& value);
    Value Materialize(const Chunk& chunk, size_t constant);
    Value Binary(Op op, const Value& lhs, const Value& rhs);
    Value Unary(Op op, const Value& value);
    bool Equals(const Value& lhs, const Value& rhs);
    int Compare(const Value& lhs, const Value& rhs);

    Engine& _engine;
    State& _state;
};

}  // пространство имён sme
