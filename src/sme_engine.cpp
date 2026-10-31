#include "sme_state.h"

#include "sme_compiler.h"
#include "sme_machine.h"
#include "sme_parser.h"
#include "sme_preprocess.h"
#include "smelib_filesystem.h"

// Движок: связывает препроцессор, компиляцию и вызовы VM.

#include <algorithm>
#include <optional>
#include <utility>

namespace sme {
namespace {

}  // анонимное пространство имён

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
        case GlobalKind::Int: return Value::Integer(0);
        case GlobalKind::Float: return Value::Float(0.f);
        case GlobalKind::Str: return Value::String({});
        case GlobalKind::Tab: return makeTable({});
        case GlobalKind::Ptr: return Value::Pointer();
        case GlobalKind::Dynamic:
        case GlobalKind::Ref: return Value::Null();
        }
        return Value::Null();
    };
    for (const std::string& global : unit.globals) addGlobal(global, Value::Null());
    for (const Literal& literal : unit.literals) {
        Value value = Value::Null();
        switch (literal.kind) {
        case Literal::Kind::Int: value = Value::Integer(literal.integer); break;
        case Literal::Kind::Float: value = Value::Float(literal.real); break;
        case Literal::Kind::String: value = Value::String(literal.text); break;
        case Literal::Kind::Pointer: value = Value::Pointer(); break;
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
        return Value::Null();
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

Value Engine::follow(const Value& value) const {
    if (!value.IsReference()) {
        return value;
    }
    if (const auto* link = std::get_if<SharedValue>(&value)) {
        return *link ? **link : Value::Null();
    }
    if (const auto* indexed = std::get_if<IndexedReference>(&value)) {
        if (!indexed->container) {
            return Value::Null();
        }
        if (indexed->index >= indexed->container->size()) {
            return Value::Null();
        }
        return (*indexed->container)[indexed->index];
    }
    if (const auto* global = std::get_if<GlobalReference>(&value)) {
        if (global->slot >= state_->globals.size()) {
            return Value::Null();
        }
        return state_->globals[global->slot].value;
    }
    const LocalReference& local = std::get<LocalReference>(value);
    if (state_->runStack.empty() || local.frame >= state_->runStack.back().frames.size()) {
        return Value::Null();
    }
    const Frame& frame = state_->runStack.back().frames[local.frame];
    if (frame.generation != local.frameGeneration || local.slot >= frame.regs.size()) {
        return Value::Null();
    }
    return frame.regs[local.slot];
}

void Engine::resizeTable(const Value& table, size_t size) {
    Value target = follow(table);
    if (!target.IsTable()) {
        throw RError("tabresize expects a table");
    }
    std::get<TableHandle>(target)->resize(size, Value::Integer(0));
}

Value Engine::makeTable(std::vector<Value> items) {
    auto table = std::make_shared<Table>();
    table->assign(std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    return Value(std::in_place_type<TableHandle>, std::move(table));
}



}  // пространство имён sme
