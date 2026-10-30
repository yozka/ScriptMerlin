#include "sme_state.h"

#include "sme_compiler.h"
#include "sme_machine.h"
#include "sme_parser.h"
#include "sme_preprocess.h"
#include "smelib_filesystem.h"

// Движок: связывает препроцессор, компиляцию и вызовы VM.

#include <algorithm>
#include <optional>
#include <sstream>
#include <utility>

namespace sme {
namespace {

std::string render(const State& state, const Value& value, std::vector<TableHandle>& seen);

}  // анонимное пространство имён

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

Type State::typeOf(const Value& value) const {
    // Сначала частые типы: два сравнения и прямой переход быстрее таблицы switch.
    const auto kind = value.index();
    if (kind == VT_INT) return Type::Int;
    if (kind == VT_FLOAT) return Type::Float;
    if (kind == VT_STRING) return Type::String;
    if (kind == VT_SHARED_STRING) {
        const SharedString& text = std::get<SharedString>(value);
        return text ? Type::String : Type::Invalid;
    }
    if (kind == VT_NULL) return Type::Null;
    if (kind == VT_TABLE) {
        const TableHandle& table = std::get<TableHandle>(value);
        return table ? Type::Table : Type::Invalid;
    }
    if (kind == VT_HANDLE) {
        const Handle* handle = std::get_if<Handle>(&value);
        if (handle == nullptr || !*handle) return Type::Invalid;
        return Type::Object;
    }
    if (kind == VT_SHARED_VALUE) {
        return std::get<SharedValue>(value) ? Type::Ref : Type::Invalid;
    }
    if (kind == VT_LOCAL_REF || kind == VT_GLOBAL_REF || kind == VT_INDEXED_REF) return Type::Ref;
    if (kind == VT_BOOL) return Type::Bool;
    if (kind == VT_PTR) return Type::Pointer;
    return Type::Invalid;
}

// Имя для type() и отладки. Код typeof — отдельно, в Engine::typeCode.
// Пустой shared_ptr не называется null: это некорректное/пустое значение хоста.
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
    if (const auto* text = std::get_if<std::string>(&value)) {
        return *text;
    }
    if (const auto* text = std::get_if<SharedString>(&value); text && *text) return **text;
    throw RError("value is not a string");
}

size_t State::lengthOf(const Value& value) const {
    switch (typeOf(value)) {
    case Type::String:
        return stringOf(value).size();
    case Type::Table:
        return std::get<TableHandle>(value)->size();
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

std::string render(const State& state, const Value& value, std::vector<TableHandle>& seen) {
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
        return state.stringOf(value);
    case Type::Ref:
        return "ref";
    case Type::Object:
        return "object";
    case Type::Table:
        break;
    }

    TableHandle table = std::get<TableHandle>(value);
    if (std::find(seen.begin(), seen.end(), table) != seen.end()) return "[...]";

    seen.push_back(table);
    std::string out = "[";
    for (size_t i = 0; i < table->size(); ++i) {
        if (i != 0) out += ", ";
        out += render(state, (*table)[i], seen);
    }
    out += "]";
    seen.pop_back();
    return out;
}

}  // анонимное пространство имён

std::string State::toString(const Value& value) const {
    std::vector<TableHandle> seen;
    return render(*this, value, seen);
}

Engine::Engine() : state_(std::make_unique<State>()) {
    state_->files = makeStdFileSystem();
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
		state_->log->message(log::Level::Error, std::to_string(static_cast<int>(code)) + " " + text);
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
    Value value(std::move(object));
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
        case GlobalKind::Int: return Value::integer(0);
        case GlobalKind::Float: return Value::real(0.f);
        case GlobalKind::Str: return Value::string({});
        case GlobalKind::Tab: return makeTable({});
        case GlobalKind::Ptr: return Value::pointer();
        case GlobalKind::Dynamic:
        case GlobalKind::Ref: return Value::null();
        }
        return Value::null();
    };
    for (const std::string& global : unit.globals) addGlobal(global, Value::null());
    for (const Literal& literal : unit.literals) {
        Value value = Value::null();
        switch (literal.kind) {
        case Literal::Kind::Int: value = Value::integer(literal.integer); break;
        case Literal::Kind::Float: value = Value::real(literal.real); break;
        case Literal::Kind::String: value = Value::string(literal.text); break;
        case Literal::Kind::Pointer: value = Value::pointer(); break;
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

// Общий путь call и callIndex: выполнить и вернуть владеющее значение.
// Ошибка пишется в лог, если хост его подключил, и пробрасывается дальше.
Value Engine::invoke(size_t index, std::span<const Value> args) {
    try {
        return Machine(*this, *state_).run(index, args);
    } catch (const RError& error) {
        if (state_->log) {
            state_->log->message(log::Level::Error, error.what());
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
    try {
        return Machine(*this, *state_).resume();
    } catch (const RError& error) {
        if (state_->log) {
            state_->log->message(log::Level::Error, error.what());
        }
        throw;
    }
}

Value Engine::globalAt(size_t index) const {
    if (index >= state_->globals.size()) {
        return Value::null();
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

std::string Engine::toString(const Value& value) const { return state_->toString(value); }
std::string_view Engine::typeName(const Value& value) const { return state_->typeName(value); }

Type Engine::typeOf(const Value& value) const { return state_->typeOf(value); }
std::string Engine::stringOf(const Value& value) const { return state_->stringOf(value); }
size_t Engine::lengthOf(const Value& value) const { return state_->lengthOf(value); }


int Engine::integerOf(const Value& value) const
{
	if (const auto* integerValue = std::get_if<int>(&value)) {
		return *integerValue;
	}
	if (const auto* floatValue = std::get_if<float>(&value)) {
		return static_cast<int>(*floatValue);
	}
	throw RError("not integer");
	return 0;
}


float Engine::floatOf(const Value& value) const
{
	if (const auto* floatValue = std::get_if<float>(&value)) {
		return *floatValue;
	}
	if (const auto* integerValue = std::get_if<int>(&value)) {
		return static_cast<float>(*integerValue);
	}
	
	throw RError("not float");
	return 0;
}



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
    return value.isReference();
}

Value Engine::follow(const Value& value) const {
    if (!isReference(value)) {
        return value;
    }
    if (const auto* link = std::get_if<SharedValue>(&value)) {
        return *link ? **link : Value::null();
    }
    if (const auto* indexed = std::get_if<IndexedReference>(&value)) {
        if (!indexed->container) {
            return Value::null();
        }
        if (indexed->index >= indexed->container->size()) {
            return Value::null();
        }
        return (*indexed->container)[indexed->index];
    }
    if (const auto* global = std::get_if<GlobalReference>(&value)) {
        if (global->slot >= state_->globals.size()) {
            return Value::null();
        }
        return state_->globals[global->slot].value;
    }
    const LocalReference& local = std::get<LocalReference>(value);
    if (state_->runStack.empty() || local.frame >= state_->runStack.back().frames.size()) {
        return Value::null();
    }
    const Frame& frame = state_->runStack.back().frames[local.frame];
    if (frame.generation != local.frameGeneration || local.slot >= frame.regs.size()) {
        return Value::null();
    }
    return frame.regs[local.slot];
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
    std::get<TableHandle>(target)->resize(size, Value::integer(0));
}

Value Engine::makeTable(std::vector<Value> items) {
    auto table = std::make_shared<Table>();
    table->assign(std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    return Value(std::in_place_type<TableHandle>, std::move(table));
}



}  // пространство имён sme
