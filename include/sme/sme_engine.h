#pragma once

#include "sme/sme_error.h"
#include "sme/sme_filesystem.h"
#include "sme/sme_object.h"
#include "sme/sme_logging.h"
#include "sme/sme_value.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sme
{

	struct State;

	// Функция, которую игра регистрирует и которую скрипт вызывает по имени.
	// Аргументы — уже вычисленные значения. Возврат null допустим.
	using NativeFn = std::function<Value(Engine& engine, Arguments args)>;

	// Фабрика, которую игра регистрирует и которую newobj<Имя>(...) вызывает по имени.
	// Аргументы — уже вычисленные значения «конструктора».
	using FactoryFn = std::function<std::shared_ptr<IScriptObject>(Engine& engine, std::span<const Value> args)>;

	// Рантайм ScriptMerlin. Конвейер такой:
	// исходник → препроцессор → лексер → парсер → компилятор → регистровая VM.
	//
	//   func main() {
	//       var n = abs(-3);
	//       return n + 39;
	//   }
	//
	// Объекты хоста и нативы нужно привязать до Load() / LoadSource().
	class Engine
	{
	public:
		Engine();
		Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;
		Engine(Engine&&) = delete;
		Engine& operator=(Engine&&) = delete;
		~Engine();
		
	public:
		
		// Подмена диска и логгера. load и #include читают через эту файловую систему.
		// Логгер получает сообщения и println.
		void SetFileSystem(files::IFileSystem::Ptr files);
		void SetLog(log::ILogging::Ptr log);
		
	public:
		// Натив виден скрипту как обычная функция. Повторное имя — ошибка.
		void RegisterFn(std::string name, NativeFn function);
		
		// newobj<Имя>(...) в скрипте ищет фабрику, зарегистрированную здесь.
		void RegisterFactory(std::string name, FactoryFn factory);
		
		// Глобал с этим именем начинает указывать на объект хоста.
		void Bind(std::string name, std::shared_ptr<IScriptObject> object);
		void SetGlobal(std::string name, Value value);
		// Scalar constants are compiled into scripts; register them before Load / LoadSource.
		void SetConstant(std::string name, Value value);
		void RegisterInteger(std::string name, int value);
		void RegisterString(std::string name, std::string value);
		
		
	public:
		// Индекс функции или глобала. nullopt — имени нет.
		std::optional<size_t> FindFunction(std::string_view name) const;
		std::optional<size_t> FindGlobal(std::string_view name) const;
		// Вызов по индексу. Ошибки выполнения возвращаются в Result и останавливают
		// всю активную цепочку вызовов.
		error::Result<Value> CallIndex(size_t index, Arguments args);
		
		// Если value — ссылка, вернуть то, на что она указывает. Иначе само value.
		// Протухшая ссылка читается как null, без исключения.
		Value Follow(const Value& value) const;
		void ResizeTable(const Value& table, size_t size);
		
		// Подменяет таблицу функций. Уже существующие глобалы сохраняют значения;
		// #def того же имени значение перезаписывает. Пока на стеке есть вызов, Load
		// отказывается: старые кадры смотрели бы в новую таблицу функций.
		// Ошибка загрузки/компиляции возвращается; при наличии ошибки Engine
		// считается непригодным и не должен использоваться дальше.
		error::Error::Opt Load(const std::string& path);
		error::Error::Opt LoadSource(std::string_view source, std::string_view name = "script");
		
		// Недостающие аргументы приходят как null, лишние отбрасываются — как при
		// вызове скрипт→скрипт. Result содержит значение или первую runtime-ошибку.
		error::Result<Value> Call(std::string_view function, Arguments args = {});
		// Верхний кадр стоит сразу за stop. Вложенный call чужую паузу не снимает.
		bool IsStopped() const;
		error::Result<Value> Resume();
		Value GlobalAt(size_t index) const;
		
	public:
		void Log(const log::Level level, const std::string& text);
		void RuntimeError(const error::Code code, const std::string& text);
		
	private:
		error::Error::Opt CompileUnit(const struct SourceUnit& unit, std::string_view name);
		error::Result<Value> Invoke(size_t index, Arguments args);
		std::unique_ptr<State> _state;
	};

}
