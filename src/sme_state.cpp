#include "sme_state.h"

#include "sme/sme_error.h"
#include "sme_assert.h"

namespace sme {

size_t State::freshFrameId() {
    size_t id = frameSerial++;
    if (frameSerial == 0) {
        frameSerial = 1;
    }
    return id;
}

size_t State::freshReferenceId() {
    if (referenceSerial == 0) {
        RecordRuntimeError(error::Code::VM_REFERENCE_IDENTITY_EXHAUSTED, "reference identity exhausted");
        return 0;
    }
    return referenceSerial++;
}

error::Error State::MakeRuntimeError(error::Code code, std::string text) const {
    error::Location location;
    location.source = sourceName;

    std::vector<error::StackFrame> stack;
    for (const RunState& run : runStack) {
        for (const Frame& frame : run.frames) {
            if (frame.function >= functions.size()) {
                continue;
            }
            const Chunk& chunk = functions[frame.function];
            error::StackFrame item;
            item.source = sourceName;
            item.function = chunk.name;
            if (frame.currentInstruction < chunk.code.size()) {
                item.instruction = frame.currentInstruction;
                item.line = frame.currentLine;
            }
            stack.push_back(item);
            if (!runStack.empty() && &run == &runStack.back() && !run.frames.empty() &&
                &frame == &run.frames.back()) {
                location.function = chunk.name;
                location.instruction = item.instruction;
                location.line = item.line;
            }
        }
    }
    return error::Error(code, std::move(text), std::move(location), std::move(stack));
}

void State::RecordRuntimeError(error::Code code, const std::string& text) {
    if (runStack.empty()) {
        SME_ASSERT_FAIL("runtime error raised outside an active script call");
        return;
    }
    if (!hasRuntimeError) {
        runtimeError = MakeRuntimeError(code, text);
        hasRuntimeError = true;
    }
}

void State::ClearRuntimeError() {
    runtimeError.reset();
    hasRuntimeError = false;
}

const error::Error& State::GetRuntimeError() const {
    SME_ASSERT(hasRuntimeError && runtimeError.has_value());
    return *runtimeError;
}

void State::RecordRuntimeError(const error::Error& error) {
    if (runStack.empty()) {
        SME_ASSERT_FAIL("runtime error raised outside an active script call");
        return;
    }
    if (!hasRuntimeError) {
        runtimeError = error;
        hasRuntimeError = true;
    }
}


// Ссылка истинна сама по себе, независимо от значения, на которое указывает.
bool State::truthy(const Value& value) const {
    if (const auto* integer = std::get_if<int>(&value)) return *integer != 0;
    switch (value.TypeOf()) {
    case Type::Int: return std::get<int>(value) != 0;
    case Type::Float: return std::get<float>(value) != 0.f;
    case Type::Bool: return std::get<bool>(value);
    case Type::String: return !value.ToString().empty();
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

std::string_view State::constantString(const Chunk& chunk, size_t index) {
    if (index >= chunk.constants.size()) {
        RecordRuntimeError(error::Code::VM_INTERNAL_CONSTANT_FAULT, "internal constant fault");
        return {};
    }
    const auto* text = std::get_if<std::string>(&chunk.constants[index]);
    if (!text) {
        RecordRuntimeError(error::Code::VM_INTERNAL_CONSTANT_FAULT, "internal constant fault");
        return {};
    }
    return *text;
}

}  // namespace sme
