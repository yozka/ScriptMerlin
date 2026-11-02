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


	void Engine::Log(const log::Level level, const std::string& text)
	{
		if (_state->log) {
			_state->log->Message(level, text);
		}
	}


	void Engine::RuntimeError(const error::Code code, const std::string& text)
	{
		_state->RecordRuntimeError(code, text);
	}


	void Engine::RegisterFn(std::string name, NativeFn function)
	{
		if (!function) {
			SME_ASSERT_FAIL("native '" + name + "' is empty");
			return;
		}
		if (std::find(_state->nativeNames.begin(), _state->nativeNames.end(), name) != _state->nativeNames.end()) {
			SME_ASSERT_FAIL("native '" + name + "' is already registered");
			return;
		}
		_state->nativeNames.push_back(std::move(name));
		_state->natives.push_back(std::move(function));
	}


	void Engine::Bind(std::string name, std::shared_ptr<IScriptObject> object) {
		if (!object) {
			SME_ASSERT_FAIL("bind expects an object");
			return;
		}
		if (_state->constants.contains(name)) {
			SME_ASSERT_FAIL("global '" + name + "' conflicts with a constant");
			return;
		}
		for (const Chunk& chunk : _state->functions) {
			if (chunk.name == name) {
				SME_ASSERT_FAIL("global '" + name + "' shadows function '" + name + "'");
				return;
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

	void Engine::SetGlobal(std::string name, Value value) {
		if (_state->constants.contains(name)) {
			SME_ASSERT_FAIL("global '" + name + "' conflicts with a constant");
			return;
		}
		for (const Chunk& chunk : _state->functions) {
			if (chunk.name == name) {
				SME_ASSERT_FAIL("global '" + name + "' shadows function '" + name + "'");
				return;
			}
		}
		for (Global& global : _state->globals) {
			if (global.name == name) {
				global.value = std::move(value);
				return;
			}
		}
		_state->globals.push_back(Global{std::move(name), std::move(value)});
	}

	void Engine::SetConstant(std::string name, Value value) {
		if (!_state->functions.empty()) {
			SME_ASSERT_FAIL("constants must be registered before loading scripts");
			return;
		}
		if (std::any_of(_state->globals.begin(), _state->globals.end(), [&](const Global& global) {
			return global.name == name;
		})) {
			SME_ASSERT_FAIL("constant '" + name + "' conflicts with a global");
			return;
		}

		if (!value.IsNull() && !value.IsInteger() && !value.IsFloat() &&
			!value.IsBool() && !value.IsString() && !value.IsPointer()) {
			SME_ASSERT_FAIL("constant '" + name + "' must be a scalar value");
			return;
		}
		_state->constants[std::move(name)] = std::move(value);
	}

	void Engine::RegisterInteger(std::string name, const int value) {
		SetConstant(std::move(name), Value::Integer(value));
	}

	void Engine::RegisterString(std::string name, std::string value) {
		SetConstant(std::move(name), Value::String(std::move(value)));
	}


	void Engine::RegisterFactory(std::string name, std::function<std::shared_ptr<IScriptObject>()> factory)
	{
		if (!factory) {
			SME_ASSERT_FAIL("factory '" + name + "' is empty");
			return;
		}
		_state->factories[std::move(name)] = std::move(factory);
	}

	// Повторный load не сбрасывает уже созданные глобалы: addGlobal выходит,
	// если имя есть. #def проходит отдельным циклом и значение перезаписывает.
	error::Error::Opt Engine::CompileUnit(const SourceUnit& unit, std::string_view name) {
		auto parsed = parse(unit.source, name);
		if (parsed.error) {
			return std::move(parsed.error);
		}
		
		Program& program = parsed;
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
		auto literalValue = [](const Literal& literal) {
			Value value = Value::Null();
			switch (literal.kind) {
				case Literal::Kind::Int: value = Value::Integer(literal.integer); break;
				case Literal::Kind::Float: value = Value::Float(literal.real); break;
				case Literal::Kind::String: value = Value::String(literal.text); break;
				case Literal::Kind::Pointer: value = Value::Pointer(); break;
				case Literal::Kind::Null: break;
			}
			return value;
		};
		for (const GlobalDecl& global : program.globals) addGlobal(global.name, initial(global.kind));
		
		CompileEnv env;
		for (uint16_t index = 0; index < _state->nativeNames.size(); ++index) {
			env.natives.emplace(_state->nativeNames[index], index);
		}
		for (uint16_t index = 0; index < _state->globals.size(); ++index) {
			env.globals.emplace(_state->globals[index].name, index);
		}
		env.constants = _state->constants;
		for (const Literal& literal : unit.literals) {
			env.constants.insert_or_assign(literal.name, literalValue(literal));
		}
		_state->sourceName = std::string(name);
		auto compiled = compile(program, env, name);
		if (compiled.error) {
			return std::move(compiled.error);
		}
		_state->functions = std::move(compiled);
		return std::nullopt;
	}


	error::Error::Opt Engine::Load(const std::string& path)
	{
		if (!_state->runStack.empty()) {
			error::Location location;
			location.source = path;
			return error::Error(error::Code::ENGINE_LOAD_DURING_ACTIVE_CALL, "cannot load while a script call is active", std::move(location));
		}
		auto unit = PreprocessFile(_state->files, path);
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
		auto unit = PreprocessSource(_state->files, source, name, {});
		if (unit.error) {
			return std::move(unit.error);
		}
		return CompileUnit(unit, name);
	}


	// Общий путь call и callIndex: выполнить и вернуть владеющее значение либо ошибку.
	error::Result<Value> Engine::Invoke(size_t index, Arguments args) {
		const bool nested = !_state->runStack.empty();
		if (!nested) {
			_state->ClearRuntimeError();
		} else if (_state->hasRuntimeError) {
			return {Value::Null(), _state->GetRuntimeError()};
		}
		auto result = Machine(*this, *_state).Run(index, args);
		if (result.error) {
			if (nested) {
				_state->RecordRuntimeError(result.error.value());
				return {Value::Null(), _state->GetRuntimeError()};
			}
			_state->ClearRuntimeError();
		}
		return result;
	}

	error::Result<Value> Engine::Call(std::string_view function, Arguments args)
	{
		if (_state->runStack.empty()) {
			_state->ClearRuntimeError();
		} else if (_state->hasRuntimeError) {
			return {Value::Null(), _state->GetRuntimeError()};
		}
		const auto it = std::find_if(_state->functions.begin(), _state->functions.end(), [&](const Chunk& chunk) {
			return chunk.name == function;
		});
		if (it == _state->functions.end()) {
			error::Error error = _state->MakeRuntimeError(error::Code::ENGINE_UNKNOWN_FUNCTION,
				"unknown function '" + std::string(function) + "'");
			if (!_state->runStack.empty()) {
				_state->RecordRuntimeError(error.Code(), error.Message());
				return {Value::Null(), _state->GetRuntimeError()};
			}
			return {Value::Null(), std::move(error)};
		}
		const auto index = static_cast<size_t>(std::distance(_state->functions.begin(), it));
		return Invoke(index, args);
	}

	// Пауза — свойство верхнего кадра, а не отдельный флаг. Вложенный call,
	// который доработал до конца, не имеет права погасить stop нижнего кадра.
	bool Engine::IsStopped() const {
		if (_state->runStack.empty()) return false;
		const RunState& active = _state->runStack.back();
		if (active.frames.empty()) return false;
		const Frame& frame = active.frames.back();
		if (frame.function >= _state->functions.size()) return false;
		const Chunk& chunk = _state->functions[frame.function];
		if (frame.ip == 0 || frame.ip > chunk.code.size()) return false;
		return chunk.code[frame.ip - 1].op == Op::Stop;
	}

	error::Result<Value> Engine::Resume()
	{
		if (_state->hasRuntimeError) {
			error::Error error = _state->GetRuntimeError();
			_state->ClearRuntimeError();
			return {Value::Null(), std::move(error)};
		}
		if (!IsStopped()) {
			SME_ASSERT_FAIL("script is not stopped");
			return {Value::Null(), _state->MakeRuntimeError(error::Code::ENGINE_NOT_STOPPED, "script is not stopped")};
		}
		auto result = Machine(*this, *_state).Resume();
		if (result.error) {
			_state->ClearRuntimeError();
		}
		return result;
	}

	Value Engine::GlobalAt(size_t index) const
	{
		if (index >= _state->globals.size()) {
			return Value::Null();
		}
		return _state->globals[index].value;
	}


	std::optional<size_t> Engine::FindFunction(std::string_view name) const
	{
		for (size_t index = 0; index < _state->functions.size(); ++index) {
			if (_state->functions[index].name == name) {
				return index;
			}
		}
		return std::nullopt;
	}

	std::optional<size_t> Engine::FindGlobal(std::string_view name) const
	{
		for (size_t index = 0; index < _state->globals.size(); ++index) {
			if (_state->globals[index].name == name) {
				return index;
			}
		}
		return std::nullopt;
	}

	error::Result<Value> Engine::CallIndex(size_t index, Arguments args) {
		if (_state->runStack.empty()) {
			_state->ClearRuntimeError();
		} else if (_state->hasRuntimeError) {
			return {Value::Null(), _state->GetRuntimeError()};
		}
		if (index >= _state->functions.size()) {
			error::Error error = _state->MakeRuntimeError(error::Code::ENGINE_FUNCTION_INDEX_OUT_OF_RANGE,
				"function index is out of range");
			if (!_state->runStack.empty()) {
				_state->RecordRuntimeError(error.Code(), error.Message());
				return {Value::Null(), _state->GetRuntimeError()};
			}
			return {Value::Null(), std::move(error)};
		}
		return Invoke(index, args);
	}

	Value Engine::Follow(const Value& value) const
	{
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

	void Engine::ResizeTable(const Value& table, size_t size)
	{
		Value target = Follow(table);
		if (!target.IsTable()) {
			RuntimeError(error::Code::ENGINE_TABRESIZE_EXPECTS_TABLE, "tabresize expects a table");
			return;
		}
		std::get<TableHandle>(target)->resize(size, Value::Integer(0));
	}

}
