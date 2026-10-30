#include "sme/smelib_default.h"

#include "sme/sme_error.h"
#include "sme/sme_logging.h"
#include "sme/sme_value.h"

#include <cctype>
#include <cmath>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>


void sme::library::InstallDefault(Engine& engine)
{
	
	engine.RegisterNative("len", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "len expects 1 argument");
			return Value::null();
		}
		return Value::integer(static_cast<int>(engine.lengthOf(args[0])));
	});
	
	
	engine.RegisterNative("str", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "str expects 1 argument");
			return Value::null();
		}
		if (const auto* count = std::get_if<int>(&args[0])) {
			if (*count < 0) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "str size is negative");
				return Value::null();
			}
			return Value::string(std::string(static_cast<size_t>(*count), '\0'));
		}
		return Value::string(engine.toString(args[0]));
	});
	
	
	engine.RegisterNative("tab", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tab expects 1 argument");
			return Value::null();
		}
		if (!std::holds_alternative<int>(args[0]) || std::get<int>(args[0]) < 0) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tab size expects a non-negative int");
			return Value::null();
		}
		return engine.makeTable(std::vector<Value>(static_cast<size_t>(std::get<int>(args[0])), Value::null()));
	});
	
	
	engine.RegisterNative("type", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "type expects 1 argument");
			return Value::null();
		}
		return Value::string(std::string(engine.typeName(args[0])));
	});
	
	
	engine.RegisterNative("print", [](Engine& engine, std::span<const Value> args)
	{
		std::string line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i != 0) line.push_back(' ');
			line += engine.toString(args[i]);
		}
		engine.Log(log::Level::Output, line);
		return Value::null();
	});
	
	
	engine.RegisterNative("println", [](Engine& engine, std::span<const Value> args)
	{
		std::string line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i != 0) line.push_back(' ');
			line += engine.toString(args[i]);
		}
		engine.Log(log::Level::Output, line);
		return Value::null();
	});
	
	
	engine.RegisterNative("strlen", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strlen expects 1 argument");
			return Value::null();
		}
		const auto& value =  args[0];
		
		if (engine.typeOf(value) != Type::String) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strlen expects a string");
			return Value::null();
		}
		const std::string text = engine.stringOf(value);
		const auto end = text.find('\0');
		return Value::integer(static_cast<int>(end == std::string::npos ? text.size() : end));
	});
	
	
	engine.RegisterNative("strsub", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 3) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strsub expects 3 arguments");
			return Value::null();
		}
		
		const std::string text = engine.stringOf(args[0]);
		const int start = engine.integerOf(args[1]);
		const int length = engine.integerOf(args[2]);

		size_t begin = static_cast<size_t>(start < 0 ? 0 : start);
		if (begin >= text.size()) return Value::string({});

		if (length < 0) return Value::string(text.substr(begin));

		size_t count = static_cast<size_t>(length);
		if (count > text.size() - begin) count = text.size() - begin;
		return Value::string(text.substr(begin, count));
	});
	
	
	engine.RegisterNative("strpos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strpos expects 2 arguments");
			return Value::null();
		}
		
		const std::string text = engine.stringOf(args[0]);
		const std::string needle = engine.stringOf(args[1]);
		const size_t found = text.find(needle);
		if (found == std::string::npos) return Value::integer(-1);
		return Value::integer(static_cast<int>(found));
	});
	
	
	engine.RegisterNative("strexplode", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strexplode expects 2 arguments");
			return Value::null();
		}

		const std::string text = engine.stringOf(args[0]);
		const char delimiter = static_cast<char>(engine.integerOf(args[1]));
		std::vector<Value> parts;
		std::string piece;
		for (char character : text) {
			if (character == delimiter) {
				parts.emplace_back(piece);
				piece.clear();
			} else {
				piece.push_back(character);
			}
		}
		parts.emplace_back(piece);
		return engine.makeTable(std::move(parts));
	});
	
	
	engine.RegisterNative("strupr", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strupr expects 1 arguments");
			return Value::null();
		}
		
		std::string text = engine.stringOf(args[0]);
		for (char& character : text) {
			character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
		}
		return Value::string(std::move(text));
	});
	
	
	engine.RegisterNative("strlwr", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strlwr expects 1 arguments");
			return Value::null();
		}
		
		std::string text = engine.stringOf(args[0]);
		for (char& character : text) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		return Value::string(std::move(text));
	});
	
	
	engine.RegisterNative("strtrim", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strtrim expects 2 arguments");
			return Value::null();
		}
		
		const std::string text = engine.stringOf(args[0]);
		const std::string cut = engine.stringOf(args[1]);
		size_t begin = 0;
		size_t end = text.size();
		while (begin < end && cut.find(text[begin]) != std::string::npos) ++begin;
		while (end > begin && cut.find(text[end - 1]) != std::string::npos) --end;
		return Value::string(text.substr(begin, end - begin));
	});
	
	
	engine.RegisterNative("sizeof", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sizeof expects 1 arguments");
			return Value::null();
		}
		if (engine.isReference(args[0])) return Value::integer(0);
		const Type kind = engine.typeOf(args[0]);
		if (kind == Type::String) {
			const std::string text = engine.stringOf(args[0]);
			const int32_t size = static_cast<int>(text.size());
			return Value::integer(text.find('\0') == std::string::npos ? size + 1 : size);
		}
		if (kind == Type::Table) return Value::integer(static_cast<int>(engine.lengthOf(args[0])));
		return Value::integer(0);
	});
	
	
	engine.RegisterNative("tabsize", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tabsize expects 1 arguments");
			return Value::null();
		}
		Value target = engine.follow(args[0]);
		if (engine.typeOf(target) != Type::Table) return Value::integer(0);
		return Value::integer(static_cast<int>(engine.lengthOf(target)));
	});
	
	
	engine.RegisterNative("tabresize", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tabresize expects 1 arguments");
			return Value::null();
		}
		const int size =  engine.integerOf(args[1]);
		engine.resizeTable(args[0], size < 0 ? 0 : static_cast<size_t>(size));
		return Value::integer(0);
	});
	
	
	engine.RegisterNative("typeof", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "typeof expects 1 arguments");
			return Value::null();
		}
		return Value::integer(engine.typeCode(args[0]));
	});
	
	
	// call(fid) или call(аргументы..., fid). Индекс функции — последний аргумент.
	// Неизвестный индекс бросает ошибку из callIndex, а не возвращает 0.
	engine.RegisterNative("call", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() == 1 && std::holds_alternative<int>(args[0])) {
			const int i = engine.integerOf(args[0]);
			if (i < 0) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "call bad arguments");
				return Value::null();
			}
			
			return engine.callIndex(static_cast<size_t>(i), {});
		}
		if (args.size() >= 2 && std::holds_alternative<int>(args.back())) {
			const int i = engine.integerOf(args.back());
			if (i < 0) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "call bad arguments");
				return Value::null();
			}
			
			const std::span<const Value> forwarded = args.first(args.size() - 1);
			return engine.callIndex(static_cast<size_t>(i), forwarded);
		}
		return Value::integer(0);
	});
}
