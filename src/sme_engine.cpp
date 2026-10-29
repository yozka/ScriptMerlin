#include "sme_state.h"

#include "sme_compiler.h"
#include "sme_machine.h"
#include "sme_parser.h"
#include "sme_preprocess.h"
#include "smelib_filesystem.h"

// Движок: связывает препроцессор, компиляцию и вызовы VM.
// Сборка мусора вокруг call. Нативы подключает хост.

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>

namespace sme {
namespace {

std::string render(const State& state, const Value& value, std::vector<Handle>& seen);

}  // анонимное пространство имён

// Корни сборки: глобалы, регистры всех кадров и пины. Значение на стеке C++
// само по себе корнем не является — его нужно положить в pins или result.
void State::gatherRoots(std::vector<Value>& out) const {
    for (const Global& global : globals) {
        out.push_back(global.value);
    }
    for (const RunState& active : runStack) {
        for (const Frame& frame : active.frames) {
            out.insert(out.end(), frame.regs.begin(), frame.regs.end());
        }
    }
    out.insert(out.end(), pins.begin(), pins.end());
    out.push_back(result);
    for (const Chunk& chunk : functions) {
        out.insert(out.end(), chunk.interned.begin(), chunk.interned.end());
    }
}

size_t State::freshFrameId() {
    size_t id = frameSerial++;
    if (frameSerial == 0) {
        frameSerial = 1;
    }
    return id;
}

void State::fail(const std::string& message, int line) {
    if (log) {
        log->message(log::Level::RError, message);
    }
    throw RError(message, line);
}

Value State::makeString(std::string text) {
    if (text.size() <= 7) {
        return shortString(text.data(), text.size());
    }
    return Value(heap.allocate(std::move(text)));
}

Type State::typeOf(const Value& value) const {
    // Сначала частые типы: два сравнения и прямой переход быстрее таблицы switch.
    const auto kind = value.index();
    if (kind == VT_INT) return Type::Int;
    if (kind == VT_FLOAT) return Type::Float;
    if (kind == VT_SSTR) return Type::String;
    if (kind == VT_NULL) return Type::Null;
    if (kind == VT_HANDLE) {
        const Handle* handle = std::get_if<Handle>(&value);
        if (handle == nullptr || !heap.check(*handle)) return Type::Invalid;
        const auto stored = heap.at(*handle).index();
        if (stored == OT_STR) return Type::String;
        if (stored == OT_TAB) return Type::Table;
        if (stored == OT_OBJ) return Type::Object;
        if (stored == OT_REF) return Type::Ref;
        return Type::Invalid;
    }
    if (kind == VT_BOOL) return Type::Bool;
    if (kind == VT_PTR) return Type::Pointer;
    return Type::Invalid;
}

// Имя для type() и отладки. Код typeof — отдельно, в Engine::typeCode.
// Мёртвый хендл не называется null: иначе нельзя отличить «скрипт вернул null»
// от «значение уже собрано».
std::string_view State::typeName(const Value& value) const {
    switch (typeOf(value)) {
    case Type::Null: return "null";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::Bool: return "bool";
    case Type::String: return "string";
    case Type::Table: return "table";
    case Type::Ref: return "ref";
    case Type::Object: return "object";
    case Type::Pointer: return "pointer";
    case Type::Invalid: return "invalid";
    }
    return "invalid";
}

std::string State::stringOf(const Value& value) const {
    if (const auto* text = std::get_if<ShortStr>(&value)) {
        return std::string(text->data, text->len);
    }
    if (typeOf(value) != Type::String) {
        throw RError("value is not a string");
    }
    return std::get<std::string>(heap.at(std::get<Handle>(value)));
}

size_t State::lengthOf(const Value& value) const {
    if (const auto* text = std::get_if<ShortStr>(&value)) {
        return text->len;
    }
    switch (typeOf(value)) {
    case Type::String:
        return std::get<std::string>(heap.at(std::get<Handle>(value))).size();
    case Type::Table:
        return std::get<Table>(heap.at(std::get<Handle>(value))).items.size();
    default:
        throw RError("len expects a string or a table");
    }
}

// Что считается истиной в if, && и !. Пустая строка ложна, пустая таблица истинна.
// Ссылка истинна, даже если за ней 0 или null: смотрим на сам хендл.
// NULL и null ложны. Числа — по значению, NaN как float != 0 истинен.
bool State::truthy(const Value& value) const {
    if (const auto* integer = std::get_if<int>(&value)) return *integer != 0;
    switch (typeOf(value)) {
    case Type::Int: return std::get<int>(value) != 0;
    case Type::Float: return std::get<float>(value) != 0.f;
    case Type::Bool: return std::get<bool>(value);
    case Type::String: return !stringOf(value).empty();
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

namespace {

std::string render(const State& state, const Value& value, std::vector<Handle>& seen) {
    switch (state.typeOf(value)) {
    case Type::Null:
    case Type::Pointer:
        return "null";
    case Type::Invalid:
        return "invalid";
    case Type::Int:
        return std::to_string(std::get<int>(value));
    case Type::Float: {
        std::ostringstream out;
        out << std::get<float>(value);
        return out.str();
    }
    case Type::Bool:
        return std::get<bool>(value) ? "true" : "false";
    case Type::String:
        if (const auto* text = std::get_if<ShortStr>(&value)) {
            return std::string(text->data, text->len);
        }
        return std::get<std::string>(state.heap.at(std::get<Handle>(value)));
    case Type::Ref:
        return "ref";
    case Type::Object:
        return "object";
    case Type::Table:
        break;
    }

    Handle handle = std::get<Handle>(value);
    if (std::find(seen.begin(), seen.end(), handle) != seen.end()) return "[...]";

    seen.push_back(handle);
    const Table& table = std::get<Table>(state.heap.at(handle));
    std::string out = "[";
    for (size_t i = 0; i < table.items.size(); ++i) {
        if (i != 0) out += ", ";
        out += render(state, table.items[i], seen);
    }
    out += "]";
    seen.pop_back();
    return out;
}

}  // анонимное пространство имён

std::string State::toString(const Value& value) const {
    std::vector<Handle> seen;
    return render(*this, value, seen);
}

Engine::Engine() : state_(std::make_unique<State>()) {
    state_->files = makeStdFileSystem();
    state_->heap.setRoots([this](std::vector<Value>& out) { state_->gatherRoots(out); });
}

Engine::~Engine() = default;

void Engine::useFileSystem(files::IFileSystem::Ptr fileSystem) {
    if (!fileSystem) {
        throw RError("file system is empty");
    }
    state_->files = std::move(fileSystem);
}

void Engine::useLog(log::ILogging::Ptr logger) { state_->log = std::move(logger); }


void Engine::Log(const log::Level level, const std::string& text)
{
	if (state_->log) {
		state_->log->message(level, text);
	}
}


void Engine::Error(const error::Code code, const std::string& text)
{
	if (state_->log) {
		state_->log->message(log::Level::RError, std::to_string(static_cast<int>(code)) + " " + text);
	}
}



void Engine::RegisterNative(std::string name, NativeFn function)
{
    if (!function) {
        throw RError("native '" + name + "' is empty");
    }
    if (std::find(state_->nativeNames.begin(), state_->nativeNames.end(), name) != state_->nativeNames.end()) {
        throw RError("native '" + name + "' is already registered");
    }
    state_->nativeNames.push_back(std::move(name));
    state_->natives.push_back(std::move(function));
}


void Engine::bind(std::string name, std::shared_ptr<HostObject> object) {
    if (!object) {
        throw RError("bind expects an object");
    }
    for (const Chunk& chunk : state_->functions) {
        if (chunk.name == name) {
            throw RError("global '" + name + "' shadows function '" + name + "'");
        }
    }
    Value value(state_->heap.allocate(HostBox{std::move(object)}));
    for (Global& global : state_->globals) {
        if (global.name == name) {
            global.value = std::move(value);
            return;
        }
    }
    state_->globals.push_back(Global{std::move(name), std::move(value)});
}

// Повторный load не сбрасывает уже созданные глобалы: addGlobal выходит,
// если имя есть. #def проходит отдельным циклом и значение перезаписывает.
void Engine::compileUnit(const SourceUnit& unit, std::string_view name) {
    Program program = parse(unit.source, name);
    auto addGlobal = [&](const std::string& global, Value value) {
        for (const Global& existing : state_->globals) {
            if (existing.name == global) {
                return;
            }
        }
        state_->globals.push_back(Global{global, std::move(value)});
    };
    auto initial = [&](GlobalKind kind) {
        switch (kind) {
        case GlobalKind::Int: return integer(0);
        case GlobalKind::Float: return real(0.f);
        case GlobalKind::Str: return state_->makeString("");
        case GlobalKind::Tab: return makeTable({});
        case GlobalKind::Ptr: return pointer();
        case GlobalKind::Dynamic:
        case GlobalKind::Ref: return null();
        }
        return null();
    };
    for (const std::string& global : unit.globals) addGlobal(global, null());
    for (const Literal& literal : unit.literals) {
        Value value = null();
        switch (literal.kind) {
        case Literal::Kind::Int: value = integer(literal.integer); break;
        case Literal::Kind::Float: value = real(literal.real); break;
        case Literal::Kind::String: value = state_->makeString(literal.text); break;
        case Literal::Kind::Pointer: value = pointer(); break;
        case Literal::Kind::Null: break;
        }
        bool replaced = false;
        for (Global& existing : state_->globals) {
            if (existing.name == literal.name) {
                existing.value = std::move(value);
                replaced = true;
                break;
            }
        }
        if (!replaced) addGlobal(literal.name, std::move(value));
    }
    for (const GlobalDecl& global : program.globals) addGlobal(global.name, initial(global.kind));

    CompileEnv env;
    for (uint16_t index = 0; index < state_->nativeNames.size(); ++index) {
        env.natives.emplace(state_->nativeNames[index], index);
    }
    for (uint16_t index = 0; index < state_->globals.size(); ++index) {
        env.globals.emplace(state_->globals[index].name, index);
    }
    state_->sourceName = std::string(name);
    state_->scope = unit.scope;
    state_->functions = compile(program, env);
    for (Chunk& chunk : state_->functions) chunk.scope = unit.scope;
}

void Engine::load(const std::string& path) {
    rejectActiveCall();
    compileUnit(preprocessFile(state_->files, path), path);
}

void Engine::loadSource(std::string_view source, std::string_view name) {
    rejectActiveCall();
    compileUnit(preprocessSource(state_->files, source, name, {}), name);
}

void Engine::rejectActiveCall() const {
    if (!state_->runStack.empty()) {
        throw RError("cannot load while a script call is active");
    }
}

// Удерживает результат на время сборки и оставляет его в state.result,
// чтобы хендл дожил до следующего call/resume.
Value Engine::retain(Value result) {
    state_->result = std::move(result);
    state_->pins.push_back(state_->result);
    state_->heap.collect();
    state_->pins.pop_back();
    return state_->result;
}

// Общий путь call и callIndex: сбросить прошлый результат, выполнить, удержать новый.
// Ошибка пишется в лог, если хост его подключил, и пробрасывается дальше.
Value Engine::invoke(size_t index, std::span<const Value> args) {
    state_->result = null();
    try {
        return retain(Machine(*this, *state_).run(index, args));
    } catch (const RError& error) {
        if (state_->log) {
            state_->log->message(log::Level::RError, error.what());
        }
        throw;
    }
}

Value Engine::call(std::string_view function, std::span<const Value> args) {
    const auto it = std::find_if(state_->functions.begin(), state_->functions.end(), [&](const Chunk& chunk) {
        return chunk.name == function;
    });
    if (it == state_->functions.end()) {
        throw RError("unknown function '" + std::string(function) + "'");
    }
    const auto index = static_cast<size_t>(std::distance(state_->functions.begin(), it));
    return invoke(index, args);
}

// Пауза — свойство верхнего кадра, а не отдельный флаг. Вложенный call,
// который доработал до конца, не имеет права погасить stop нижнего кадра.
bool Engine::stopped() const {
    if (state_->runStack.empty()) return false;
    const RunState& active = state_->runStack.back();
    if (active.frames.empty()) return false;
    const Frame& frame = active.frames.back();
    if (frame.function >= state_->functions.size()) return false;
    const Chunk& chunk = state_->functions[frame.function];
    if (frame.ip == 0 || frame.ip > chunk.code.size()) return false;
    return chunk.code[frame.ip - 1].op == Op::Stop;
}

Value Engine::resume() {
    if (!stopped()) {
        throw RError("script is not stopped");
    }
    state_->result = null();
    try {
        return retain(Machine(*this, *state_).resume());
    } catch (const RError& error) {
        if (state_->log) {
            state_->log->message(log::Level::RError, error.what());
        }
        throw;
    }
}

Value Engine::globalAt(size_t index) const {
    if (index >= state_->globals.size()) {
        return null();
    }
    return state_->globals[index].value;
}

size_t Engine::scope() const { return state_->scope; }

std::optional<size_t> Engine::functionScope(std::string_view name) const {
    for (const Chunk& chunk : state_->functions) {
        if (chunk.name == name) return chunk.scope;
    }
    return std::nullopt;
}

Value Engine::makeString(std::string text) { return state_->makeString(std::move(text)); }
std::string Engine::toString(const Value& value) const { return state_->toString(value); }
std::string_view Engine::typeName(const Value& value) const { return state_->typeName(value); }

Type Engine::typeOf(const Value& value) const { return state_->typeOf(value); }
std::string Engine::stringOf(const Value& value) const { return state_->stringOf(value); }
size_t Engine::lengthOf(const Value& value) const { return state_->lengthOf(value); }


void Engine::addFactory(std::string name, std::function<std::shared_ptr<HostObject>()> factory) {
    if (!factory) {
        throw RError("factory '" + name + "' is empty");
    }
    state_->factories[std::move(name)] = std::move(factory);
}

std::optional<size_t> Engine::findFunction(std::string_view name) const {
    for (size_t index = 0; index < state_->functions.size(); ++index) {
        if (state_->functions[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<size_t> Engine::findGlobal(std::string_view name) const {
    for (size_t index = 0; index < state_->globals.size(); ++index) {
        if (state_->globals[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

Value Engine::callIndex(size_t index, std::span<const Value> args) {
    if (index >= state_->functions.size()) {
        throw RError("unknown function");
    }
    return invoke(index, args);
}

bool Engine::isReference(const Value& value) const {
    const auto* handle = std::get_if<Handle>(&value);
    if (!handle || !state_->heap.check(*handle)) {
        return false;
    }
    return std::holds_alternative<Reference>(state_->heap.at(*handle));
}

Value Engine::follow(const Value& value) const {
    if (!isReference(value)) {
        return value;
    }
    const Reference ref = std::get<Reference>(state_->heap.at(std::get<Handle>(value)));
    if (ref.indexed) {
        if (!state_->heap.check(ref.container)) {
            return null();
        }
        const Object& object = state_->heap.at(ref.container);
        const auto* table = std::get_if<Table>(&object);
        if (!table || ref.index >= table->items.size()) {
            return null();
        }
        return table->items[ref.index];
    }
    if (ref.global) {
        if (ref.slot >= state_->globals.size()) {
            return null();
        }
        return state_->globals[ref.slot].value;
    }
    if (state_->runStack.empty() || ref.frame >= state_->runStack.back().frames.size()) {
        return null();
    }
    const Frame& frame = state_->runStack.back().frames[ref.frame];
    if (frame.generation != ref.frameGeneration || ref.slot >= frame.regs.size()) {
        return null();
    }
    return frame.regs[ref.slot];
}

int32_t Engine::typeCode(const Value& value) const {
    switch (state_->typeOf(value)) {
    case Type::Pointer: return 6;
    case Type::Int:
    case Type::Bool: return 1;
    case Type::Float: return 2;
    case Type::String: return 3;
    case Type::Table: return 4;
    case Type::Null:
    case Type::Ref:
    case Type::Object:
    case Type::Invalid: return 0;
    }
    return 0;
}

void Engine::resizeTable(const Value& table, size_t size) {
    Value target = follow(table);
    if (state_->typeOf(target) != Type::Table) {
        throw RError("tabresize expects a table");
    }
    Table& items = std::get<Table>(state_->heap.at(std::get<Handle>(target)));
    items.items.resize(size, integer(0));
}

Value Engine::makeTable(std::vector<Value> items) {
    const size_t mark = state_->pins.size();
    for (const Value& item : items) {
        state_->pins.push_back(item);
    }
    Value result{state_->heap.allocate(Table{std::move(items)})};
    state_->pins.resize(mark);
    return result;
}

void Engine::anchor(const Value& value) { state_->pins.push_back(value); }
void Engine::clearAnchors() { state_->pins.clear(); }
size_t Engine::anchorCount() const { return state_->pins.size(); }
void Engine::resizeAnchors(size_t count) { state_->pins.resize(count); }

}  // пространство имён sme
