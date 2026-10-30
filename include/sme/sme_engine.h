#pragma once

#include "sme/sme_error.h"
#include "sme/sme_filesystem.h"
#include "sme/sme_host.h"
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
	using NativeFn = std::function<Value(Engine& engine, std::span<const Value> args)>;

	// Рантайм ScriptMerlin. Конвейер такой:
	// исходник → препроцессор → лексер → парсер → компилятор → регистровая VM.
	//
	//   fn main() {
	//       var n = abs(-3);
	//       return n + 39;
	//   }
	//
	// Объекты хоста и нативы нужно привязать до load() / loadSource().
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
		// Логгер получает ошибки call/resume и println.
		void useFileSystem(files::IFileSystem::Ptr fileSystem);
		void useLog(log::ILogging::Ptr logger);
		
	public:
		// Натив виден скрипту как обычная функция. Повторное имя — ошибка.
		void RegisterNative(std::string name, NativeFn function);
		
		// newobj<Имя>() в скрипте ищет фабрику, зарегистрированную здесь.
		void addFactory(std::string name, std::function<std::shared_ptr<HostObject>()> factory);
		
		// Глобал с этим именем начинает указывать на объект хоста.
		void bind(std::string name, std::shared_ptr<HostObject> object);
		
		
	public:
		// Индекс функции или глобала. nullopt — имени нет.
		std::optional<size_t> findFunction(std::string_view name) const;
		std::optional<size_t> findGlobal(std::string_view name) const;
		// Вызов по индексу. Индекс вне таблицы функций — ошибка, не тихий 0.
		Value callIndex(size_t index, std::span<const Value> args);
		
		bool isReference(const Value& value) const;
		// Если value — ссылка, вернуть то, на что она указывает. Иначе само value.
		// Протухшая ссылка читается как null, без исключения.
		Value follow(const Value& value) const;
		// Числовой код типа: 0 null/ссылка/объект, 1 int, 2 float, 3 string,
		// 4 table, 6 NULL. Тот же код возвращают typeof и ?имя.
		int32_t typeCode(const Value& value) const;
		void resizeTable(const Value& table, size_t size);
		Value makeTable(std::vector<Value> items);
		
		// Подменяет таблицу функций. Уже существующие глобалы сохраняют значения;
		// #def того же имени значение перезаписывает. Пока на стеке есть вызов, load
		// отказывается: старые кадры смотрели бы в новую таблицу функций.
		void load(const std::string& path);
		void loadSource(std::string_view source, std::string_view name = "script");
		
		// Недостающие аргументы приходят как null, лишние отбрасываются — как при
		// вызове скрипт→скрипт. Результат владеет данными и может храниться дольше Engine.
		Value call(std::string_view function, std::span<const Value> args = {});
		// Верхний кадр стоит сразу за stop. Вложенный call чужую паузу не снимает.
		bool stopped() const;
		Value resume();
		Value globalAt(size_t index) const;
		// #scope — одно число на весь загруженный файл, не на отдельную функцию.
		size_t scope() const;
		// nullopt — функции с таким именем нет.
		std::optional<size_t> functionScope(std::string_view name) const;
		
		std::string toString(const Value& value) const;
		// Имя типа для type() и отладки. NULL — "pointer", пустой shared_ptr — "invalid".
		std::string_view typeName(const Value& value) const;
		Type typeOf(const Value& value) const;
		std::string stringOf(const Value& value) const;
		size_t lengthOf(const Value& value) const;
		int integerOf(const Value& value) const;
		float floatOf(const Value& value) const;
		
	public:
		void Log(const log::Level level, const std::string& text);
		void Error(const error::Code code, const std::string& text);
		
	private:
		void compileUnit(const struct SourceUnit& unit, std::string_view name);
		void rejectActiveCall() const;
		Value invoke(size_t index, std::span<const Value> args);
		std::unique_ptr<State> state_;
	};

}
