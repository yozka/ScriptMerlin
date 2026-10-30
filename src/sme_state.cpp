#include "sme_state.h"

#include "sme/sme_error.h"

namespace sme {

size_t State::freshFrameId() {
    size_t id = frameSerial++;
    if (frameSerial == 0) {
        frameSerial = 1;
    }
    return id;
}

uint32_t State::freshReferenceId() {
    if (referenceSerial == 0) throw RError("reference identity exhausted");
    return referenceSerial++;
}

void State::fail(const std::string& message, int line) {
    if (log) {
        log->message(log::Level::Error, message);
    }
    throw RError(message, line);
}

// Ссылка истинна сама по себе, независимо от значения, на которое указывает.
bool State::truthy(const Value& value) const {
    if (const auto* integer = std::get_if<int>(&value)) return *integer != 0;
    switch (value.TypeOf()) {
    case Type::Int: return std::get<int>(value) != 0;
    case Type::Float: return std::get<float>(value) != 0.f;
    case Type::Bool: return std::get<bool>(value);
    case Type::String: return !value.AsString().empty();
    case Type::Null:
    case Type::Pointer:
    case Type::Invalid:
        return false;
    case Type::Table:
    case Type::Ref:
    case Type::Object:
        return true;
    }
    return false;
}

std::string_view State::constantString(const Chunk& chunk, size_t index, int line) const {
    if (index >= chunk.constants.size()) {
        throw RError("internal constant fault", line);
    }
    const auto* text = std::get_if<std::string>(&chunk.constants[index]);
    if (!text) {
        throw RError("internal constant fault", line);
    }
    return *text;
}

}  // namespace sme
