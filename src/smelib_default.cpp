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
			return Value::Null();
		}
		return Value::Integer(static_cast<int>(args[0].Length()));
	});
	
	
	engine.RegisterNative("str", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "str expects 1 argument");
			return Value::Null();
		}
		if (const auto* count = std::get_if<int>(&args[0])) {
			if (*count < 0) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "str size is negative");
				return Value::Null();
			}
			return Value::String(std::string(static_cast<size_t>(*count), '\0'));
		}
		return Value::String(args[0].CastString());
	});
	
	
	engine.RegisterNative("tab", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tab expects 1 argument");
			return Value::Null();
		}
		if (!args[0].IsInteger() || std::get<int>(args[0]) < 0) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tab size expects a non-negative int");
			return Value::Null();
		}
		return engine.makeTable(std::vector<Value>(static_cast<size_t>(std::get<int>(args[0])), Value::Null()));
	});
	
	
	engine.RegisterNative("type", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "type expects 1 argument");
			return Value::Null();
		}
		return Value::String(std::string(args[0].TypeName()));
	});
	
	
	engine.RegisterNative("print", [](Engine& engine, std::span<const Value> args)
	{
		std::string line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i != 0) line.push_back(' ');
			line += args[i].CastString();
		}
		engine.Log(log::Level::Output, line);
		return Value::Null();
	});
	
	
	engine.RegisterNative("println", [](Engine& engine, std::span<const Value> args)
	{
		std::string line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i != 0) line.push_back(' ');
			line += args[i].CastString();
		}
		engine.Log(log::Level::Output, line);
		return Value::Null();
	});
	
	
	engine.RegisterNative("strlen", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strlen expects 1 argument");
			return Value::Null();
		}
		const auto& value =  args[0];
		
		if (!value.IsString()) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strlen expects a string");
			return Value::Null();
		}
		const std::string text = value.ToString();
		const auto end = text.find('\0');
		return Value::Integer(static_cast<int>(end == std::string::npos ? text.size() : end));
	});
	
	
	engine.RegisterNative("strsub", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 3) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strsub expects 3 arguments");
			return Value::Null();
		}
		
		const std::string text = args[0].ToString();
		const int start = args[1].ToInteger();
		const int length = args[2].ToInteger();

		size_t begin = static_cast<size_t>(start < 0 ? 0 : start);
		if (begin >= text.size()) return Value::String({});

		if (length < 0) return Value::String(text.substr(begin));

		size_t count = static_cast<size_t>(length);
		if (count > text.size() - begin) count = text.size() - begin;
		return Value::String(text.substr(begin, count));
	});
	
	
	engine.RegisterNative("strpos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strpos expects 2 arguments");
			return Value::Null();
		}
		
		const std::string text = args[0].ToString();
		const std::string needle = args[1].ToString();
		const size_t found = text.find(needle);
		if (found == std::string::npos) return Value::Integer(-1);
		return Value::Integer(static_cast<int>(found));
	});
	
	
	engine.RegisterNative("strexplode", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strexplode expects 2 arguments");
			return Value::Null();
		}

		const std::string text = args[0].ToString();
		const char delimiter = static_cast<char>(args[1].ToInteger());
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
			return Value::Null();
		}
		
		std::string text = args[0].ToString();
		for (char& character : text) {
			character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
		}
		return Value::String(std::move(text));
	});
	
	
	engine.RegisterNative("strlwr", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strlwr expects 1 arguments");
			return Value::Null();
		}
		
		std::string text = args[0].ToString();
		for (char& character : text) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		return Value::String(std::move(text));
	});
	
	
	engine.RegisterNative("strtrim", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "strtrim expects 2 arguments");
			return Value::Null();
		}
		
		const std::string text = args[0].ToString();
		const std::string cut = args[1].ToString();
		size_t begin = 0;
		size_t end = text.size();
		while (begin < end && cut.find(text[begin]) != std::string::npos) ++begin;
		while (end > begin && cut.find(text[end - 1]) != std::string::npos) --end;
		return Value::String(text.substr(begin, end - begin));
	});
	
	
	engine.RegisterNative("sizeof", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sizeof expects 1 arguments");
			return Value::Null();
		}
		if (args[0].IsReference()) return Value::Integer(0);
		if (args[0].IsString()) {
			const std::string text = args[0].ToString();
			const int32_t size = static_cast<int>(text.size());
			return Value::Integer(text.find('\0') == std::string::npos ? size + 1 : size);
		}
		if (args[0].IsTable()) return Value::Integer(static_cast<int>(args[0].Length()));
		return Value::Integer(0);
	});
	
	
	engine.RegisterNative("tabsize", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tabsize expects 1 arguments");
			return Value::Null();
		}
		Value target = engine.follow(args[0]);
		if (!target.IsTable()) return Value::Integer(0);
		return Value::Integer(static_cast<int>(target.Length()));
	});
	
	
	engine.RegisterNative("tabresize", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "tabresize expects 1 arguments");
			return Value::Null();
		}
		const int size = args[1].ToInteger();
		engine.resizeTable(args[0], size < 0 ? 0 : static_cast<size_t>(size));
		return Value::Integer(0);
	});
	
	
	engine.RegisterNative("typeof", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "typeof expects 1 arguments");
			return Value::Null();
		}
		return Value::Integer(args[0].TypeCode());
	});
	
	
	// call(fid) или call(аргументы..., fid). Индекс функции — последний аргумент.
	// Неизвестный индекс бросает ошибку из callIndex, а не возвращает 0.
	engine.RegisterNative("call", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() == 1 && args[0].IsInteger()) {
			const int i = args[0].ToInteger();
			if (i < 0) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "call bad arguments");
				return Value::Null();
			}
			
			return engine.callIndex(static_cast<size_t>(i), {});
		}
		if (args.size() >= 2 && args.back().IsInteger()) {
			const int i = args.back().ToInteger();
			if (i < 0) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "call bad arguments");
				return Value::Null();
			}
			
			const std::span<const Value> forwarded = args.first(args.size() - 1);
			return engine.callIndex(static_cast<size_t>(i), forwarded);
		}
		return Value::Integer(0);
	});
}
