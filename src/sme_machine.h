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
    // Исключение снимает ровно этот RunState и не трогает паузу ниже по стеку.
    Value run(size_t function, std::span<const Value> args);
    // Продолжить верхний RunState, который стоит на stop.
    Value resume();

private:
    // nullopt значит «встали на stop», значение — нормальный return наружу.
    std::optional<Value> execute();
    Value& reg(Frame& frame, size_t index, int line);
    Value materialize(const Chunk& chunk, size_t constant, int line);
    Value binary(Op op, const Value& lhs, const Value& rhs, int line);
    Value unary(Op op, const Value& value, int line);
    bool equals(const Value& lhs, const Value& rhs);
    int compare(const Value& lhs, const Value& rhs, int line);

    Engine& engine_;
    State& state_;
};

}  // пространство имён sme
