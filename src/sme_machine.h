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
    error::Result<Value> run(size_t function, std::span<const Value> args);
    // Продолжить верхний RunState, который стоит на stop.
    error::Result<Value> resume();

private:
    // nullopt значит «встали на stop», значение — нормальный return наружу.
    error::Result<std::optional<Value>> execute();
    int checkedIntAdd(int x, int y);
    int checkedIntSub(int x, int y);
    int checkedIntMul(int x, int y);
    Value intBinary(Op op, int x, int y);
    int narrowInt(double number);
    Value* referenced(const Value& value);
    Value materialize(const Chunk& chunk, size_t constant);
    Value binary(Op op, const Value& lhs, const Value& rhs);
    Value unary(Op op, const Value& value);
    bool equals(const Value& lhs, const Value& rhs);
    int compare(const Value& lhs, const Value& rhs);

    Engine& _engine;
    State& _state;
};

}  // пространство имён sme
