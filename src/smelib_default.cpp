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
#include <utility>
#include <vector>


void sme::library::InstallDefault(Engine& engine)
{
	
	engine.RegisterFn("len", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "len expects 1 argument");
			return Value::Null();
		}
		return Value::Integer(static_cast<int>(args[0].Length()));
	});
	
	
	engine.RegisterFn("str", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "str expects 1 argument");
			return Value::Null();
		}
		if (const auto* count = std::get_if<int>(&args[0])) {
			if (*count < 0) {
				engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "str size is negative");
				return Value::Null();
			}
			return Value::String(std::string(static_cast<size_t>(*count), '\0'));
		}
		return Value::String(args[0].CastString());
	});
	
	
	engine.RegisterFn("tab", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "tab expects 1 argument");
			return Value::Null();
		}
		if (!args[0].IsInteger() || std::get<int>(args[0]) < 0) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "tab size expects a non-negative int");
			return Value::Null();
		}
		return Value::Table(std::vector<Value>(static_cast<size_t>(std::get<int>(args[0])), Value::Null()));
	});
	
	
	engine.RegisterFn("type", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "type expects 1 argument");
			return Value::Null();
		}
		return Value::String(std::string(args[0].TypeName()));
	});
	
	
	engine.RegisterFn("print", [](Engine& engine, std::span<const Value> args)
	{
		std::string line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i != 0) line.push_back(' ');
			line += args[i].CastString();
		}
		engine.Log(log::Level::Output, line);
		return Value::Null();
	});
	
	
	engine.RegisterFn("println", [](Engine& engine, std::span<const Value> args)
	{
		std::string line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i != 0) line.push_back(' ');
			line += args[i].CastString();
		}
		engine.Log(log::Level::Output, line);
		return Value::Null();
	});
	
	
	engine.RegisterFn("strlen", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strlen expects 1 argument");
			return Value::Null();
		}
		const auto& value =  args[0];
		
		if (!value.IsString()) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strlen expects a string");
			return Value::Null();
		}
		const std::string text = value.ToString();
		const auto end = text.find('\0');
		return Value::Integer(static_cast<int>(end == std::string::npos ? text.size() : end));
	});
	
	
	engine.RegisterFn("strsub", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 3) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strsub expects 3 arguments");
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
	
	
	engine.RegisterFn("strpos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strpos expects 2 arguments");
			return Value::Null();
		}
		
		const std::string text = args[0].ToString();
		const std::string needle = args[1].ToString();
		const size_t found = text.find(needle);
		if (found == std::string::npos) return Value::Integer(-1);
		return Value::Integer(static_cast<int>(found));
	});
	
	
	engine.RegisterFn("strexplode", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strexplode expects 2 arguments");
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
		return Value::Table(std::move(parts));
	});
	
	
	engine.RegisterFn("strupr", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strupr expects 1 arguments");
			return Value::Null();
		}
		
		std::string text = args[0].ToString();
		for (char& character : text) {
			character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
		}
		return Value::String(std::move(text));
	});
	
	
	engine.RegisterFn("strlwr", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strlwr expects 1 arguments");
			return Value::Null();
		}
		
		std::string text = args[0].ToString();
		for (char& character : text) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		return Value::String(std::move(text));
	});
	
	
	engine.RegisterFn("strtrim", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "strtrim expects 2 arguments");
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
	
	
	engine.RegisterFn("sizeof", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "sizeof expects 1 arguments");
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
	
	
	engine.RegisterFn("tabsize", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "tabsize expects 1 arguments");
			return Value::Null();
		}
		Value target = engine.Follow(args[0]);
		if (!target.IsTable()) return Value::Integer(0);
		return Value::Integer(static_cast<int>(target.Length()));
	});
	
	
	engine.RegisterFn("tabresize", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "tabresize expects 1 arguments");
			return Value::Null();
		}
		const int size = args[1].ToInteger();
		engine.ResizeTable(args[0], size < 0 ? 0 : static_cast<size_t>(size));
		return Value::Integer(0);
	});
	
	
	engine.RegisterFn("typeof", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "typeof expects 1 arguments");
			return Value::Null();
		}
		return Value::Integer(args[0].TypeCode());
	});
	
	
	// call(fid) или call(аргументы..., fid). Индекс функции — последний аргумент.
	// Ошибка callIndex прерывает весь активный скриптовый вызов.
	engine.RegisterFn("call", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() == 1 && args[0].IsInteger()) {
			const int i = args[0].ToInteger();
			if (i < 0) {
				engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "call bad arguments");
				return Value::Null();
			}
			
			auto result = engine.CallIndex(static_cast<size_t>(i), {});
			if (result.error) return Value::Null();
			return std::move(static_cast<Value&>(result));
		}
		if (args.size() >= 2 && args.back().IsInteger()) {
			const int i = args.back().ToInteger();
			if (i < 0) {
				engine.RuntimeError(error::Code::FUNCTION_ARGUMENTS, "call bad arguments");
				return Value::Null();
			}
			
			const std::span<const Value> forwarded = args.first(args.size() - 1);
			auto result = engine.CallIndex(static_cast<size_t>(i), forwarded);
			if (result.error) return Value::Null();
			return std::move(static_cast<Value&>(result));
		}
		return Value::Integer(0);
	});
}
