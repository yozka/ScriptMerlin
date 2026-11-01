#include "sme_state.h"

#include "sme_assert.h"
#include "sme_compiler.h"
#include "sme_machine.h"
#include "sme_parser.h"
#include "sme_preprocess.h"
#include "smelib_filesystem.h"

// Движок: связывает препроцессор, компиляцию и вызовы VM.

#include <algorithm>
#include <optional>
#include <utility>

namespace sme
{

	Engine::Engine()
		:
		_state(std::make_unique<State>())
	{
	}


	Engine::~Engine()
	{
		
	}


	void Engine::SetFileSystem(files::IFileSystem::Ptr files)
	{
		SME_ASSERT(files, "file system is empty");
		_state->files = std::move(files);
	}


	void Engine::SetLog(log::ILogging::Ptr log)
	{
		SME_ASSERT(log, "logging system is empty");
		_state->log = std::move(log);
	}


	void Engine::SetErrorHandle(error::IHandler::Ptr error)
	{
		SME_ASSERT(error, "error system is empty");
		_state->errorHandler = std::move(error);
	}


	void Engine::Log(const log::Level level, const std::string& text)
	{
		if (_state->log) {
			_state->log->message(level, text);
		}
	}


	void Engine::RuntimeError(const error::Code code, const std::string& text)
	{
		_state->RuntimeError({code, text});
	}


	void Engine::RegisterNative(std::string name, NativeFn function)
	{
		if (!function) {
			throw RError("native '" + name + "' is empty");
		}
		if (std::find(_state->nativeNames.begin(), _state->nativeNames.end(), name) != _state->nativeNames.end()) {
			throw RError("native '" + name + "' is already registered");
		}
		_state->nativeNames.push_back(std::move(name));
		_state->natives.push_back(std::move(function));
	}


	void Engine::bind(std::string name, std::shared_ptr<IScriptObject> object) {
		if (!object) {
			throw RError("bind expects an object");
		}
		for (const Chunk& chunk : _state->functions) {
			if (chunk.name == name) {
				throw RError("global '" + name + "' shadows function '" + name + "'");
			}
		}
		Value value(std::move(object));
		for (Global& global : _state->globals) {
			if (global.name == name) {
				global.value = std::move(value);
				return;
			}
		}
		_state->globals.push_back(Global{std::move(name), std::move(value)});
	}

	// Повторный load не сбрасывает уже созданные глобалы: addGlobal выходит,
	// если имя есть. #def проходит отдельным циклом и значение перезаписывает.
	error::Error::Opt Engine::CompileUnit(const SourceUnit& unit, std::string_view name) {
		ParseResult parsed = parse(unit.source, name);
		if (parsed.error) {
			return std::move(parsed.error);
		}
		
		Program& program = parsed.program;
		auto addGlobal = [&](const std::string& global, Value value) {
			for (const Global& existing : _state->globals) {
				if (existing.name == global) {
					return;
				}
			}
			_state->globals.push_back(Global{global, std::move(value)});
		};
		auto initial = [&](GlobalKind kind) {
			switch (kind) {
				case GlobalKind::Int: return Value::Integer(0);
				case GlobalKind::Float: return Value::Float(0.f);
				case GlobalKind::Str: return Value::String({});
				case GlobalKind::Tab: return Value::Table({});
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
			for (Global& existing : _state->globals) {
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
		for (uint16_t index = 0; index < _state->nativeNames.size(); ++index) {
			env.natives.emplace(_state->nativeNames[index], index);
		}
		for (uint16_t index = 0; index < _state->globals.size(); ++index) {
			env.globals.emplace(_state->globals[index].name, index);
		}
		_state->sourceName = std::string(name);
		CompileResult compiled = compile(program, env, name);
		if (compiled.error) {
			return std::move(compiled.error);
		}
		_state->functions = std::move(compiled.functions);
		return std::nullopt;
	}


	error::Error::Opt Engine::Load(const std::string& path)
	{
		if (!_state->runStack.empty()) {
			error::Location location;
			location.source = path;
			return error::Error(error::Code::ENGINE_LOAD_DURING_ACTIVE_CALL, "cannot load while a script call is active", std::move(location));
		}
		SourceUnit unit = PreprocessFile(_state->files, path);
		if (unit.error) {
			return std::move(unit.error);
		}
		return CompileUnit(unit, path);
	}


	error::Error::Opt Engine::LoadSource(std::string_view source, std::string_view name)
	{
		if (!_state->runStack.empty()) {
			error::Location location;
			location.source = name;
			return error::Error(error::Code::ENGINE_LOAD_DURING_ACTIVE_CALL,
				"cannot load while a script call is active", std::move(location));
		}
		SourceUnit unit = PreprocessSource(_state->files, source, name, {});
		if (unit.error) {
			return std::move(unit.error);
		}
		return CompileUnit(unit, name);
	}


	// Общий путь call и callIndex: выполнить и вернуть владеющее значение.
	// Ошибка пишется в лог, если хост его подключил, и пробрасывается дальше.
	Value Engine::invoke(size_t index, std::span<const Value> args) {
		try {
			return Machine(*this, *_state).run(index, args);
		} catch (const RError& error) {
			if (_state->log) {
				_state->log->message(log::Level::Error, error.what());
			}
			throw;
		}
	}

	Value Engine::call(std::string_view function, std::span<const Value> args) {
		const auto it = std::find_if(_state->functions.begin(), _state->functions.end(), [&](const Chunk& chunk) {
			return chunk.name == function;
		});
		if (it == _state->functions.end()) {
			throw RError("unknown function '" + std::string(function) + "'");
		}
		const auto index = static_cast<size_t>(std::distance(_state->functions.begin(), it));
		return invoke(index, args);
	}

	// Пауза — свойство верхнего кадра, а не отдельный флаг. Вложенный call,
	// который доработал до конца, не имеет права погасить stop нижнего кадра.
	bool Engine::stopped() const {
		if (_state->runStack.empty()) return false;
		const RunState& active = _state->runStack.back();
		if (active.frames.empty()) return false;
		const Frame& frame = active.frames.back();
		if (frame.function >= _state->functions.size()) return false;
		const Chunk& chunk = _state->functions[frame.function];
		if (frame.ip == 0 || frame.ip > chunk.code.size()) return false;
		return chunk.code[frame.ip - 1].op == Op::Stop;
	}

	Value Engine::resume() {
		if (!stopped()) {
			throw RError("script is not stopped");
		}
		try {
			return Machine(*this, *_state).resume();
		} catch (const RError& error) {
			if (_state->log) {
				_state->log->message(log::Level::Error, error.what());
			}
			throw;
		}
	}

	Value Engine::globalAt(size_t index) const {
		if (index >= _state->globals.size()) {
			return Value::Null();
		}
		return _state->globals[index].value;
	}

	void Engine::RegisterFactory(std::string name, std::function<std::shared_ptr<IScriptObject>()> factory) {
		if (!factory) {
			throw RError("factory '" + name + "' is empty");
		}
		_state->factories[std::move(name)] = std::move(factory);
	}

	std::optional<size_t> Engine::findFunction(std::string_view name) const {
		for (size_t index = 0; index < _state->functions.size(); ++index) {
			if (_state->functions[index].name == name) {
				return index;
			}
		}
		return std::nullopt;
	}

	std::optional<size_t> Engine::findGlobal(std::string_view name) const {
		for (size_t index = 0; index < _state->globals.size(); ++index) {
			if (_state->globals[index].name == name) {
				return index;
			}
		}
		return std::nullopt;
	}

	Value Engine::callIndex(size_t index, std::span<const Value> args) {
		if (index >= _state->functions.size()) {
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
			if (global->slot >= _state->globals.size()) {
				return Value::Null();
			}
			return _state->globals[global->slot].value;
		}
		const LocalReference& local = std::get<LocalReference>(value);
		if (_state->runStack.empty() || local.frame >= _state->runStack.back().frames.size()) {
			return Value::Null();
		}
		const Frame& frame = _state->runStack.back().frames[local.frame];
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

}
