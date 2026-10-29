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

/*
namespace {

void expectArgs(std::span<const Value> args, size_t count, const char* name) {
	if (args.size() != count) {
		throw RError(std::string(name) + " expects " + std::to_string(count) + " argument(s)");
	}
}

int asInt(const Value& value, const char* name) {
	if (const auto* integerValue = std::get_if<int>(&value)) return *integerValue;
	if (const auto* flag = std::get_if<bool>(&value)) return *flag ? 1 : 0;
	if (const auto* realValue = std::get_if<float>(&value)) {
		const double number = *realValue;
		if (!std::isfinite(number) ||
			number < static_cast<double>(std::numeric_limits<int>::min()) ||
			number >= static_cast<double>(std::numeric_limits<int>::max()) + 1.0) {
			throw RError(std::string(name) + " integer overflow");
		}
		return static_cast<int>(number);
	}
	throw RError(std::string(name) + " expects an int");
}

std::string textOf(Engine& engine, const Value& value, const char* name) {
	if (engine.typeOf(value) != Type::String) {
		throw RError(std::string(name) + " expects a string");
	}
	return engine.stringOf(value);
}

}  // анонимное пространство имён
*/
 
void sme::library::InstallDefault(Engine& engine)
{
	
	engine.RegisterNative("len", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "len expects 1 argument");
			return null();
		}
		return integer(static_cast<int>(engine.lengthOf(args[0])));
	});
	
	
	engine.RegisterNative("str", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "str expects 1 argument");
			return null();
		}
		if (const auto* count = std::get_if<int>(&args[0])) {
			if (*count < 0) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "str size is negative");
				return null();
			}
			return engine.makeString(std::string(static_cast<size_t>(*count), '\0'));
		}
		return engine.makeString(engine.toString(args[0]));
	});
	
	
	engine.RegisterNative("tab", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "tab");
		if (!std::holds_alternative<int>(args[0]) || std::get<int>(args[0]) < 0) {
			throw RError("tab size expects a non-negative int");
		}
		return engine.makeTable(std::vector<Value>(static_cast<size_t>(std::get<int>(args[0])), null()));
	});
	
	
	engine.RegisterNative("type", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "type");
		return engine.makeString(std::string(engine.typeName(args[0])));
	});
	
	
	engine.RegisterNative("println", [](Engine& engine, std::span<const Value> args)
	{
		std::string line;
		for (size_t i = 0; i < args.size(); ++i) {
			if (i != 0) line.push_back(' ');
			line += engine.toString(args[i]);
		}
		engine.Log(log::Level::Output, line);
		return null();
	});
	
	
	engine.RegisterNative("strlen", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "strlen");
		const std::string text = textOf(engine, args[0], "strlen");
		const auto end = text.find('\0');
		return integer(static_cast<int>(end == std::string::npos ? text.size() : end));
	});
	
	
	engine.RegisterNative("strsub", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 3, "strsub");
		const std::string text = textOf(engine, args[0], "strsub");
		int32_t start = asInt(args[1], "strsub");
		int32_t length = asInt(args[2], "strsub");
		if (start < 0) start = 0;
		if (static_cast<size_t>(start) >= text.size()) return engine.makeString("");
		if (length < 0) return engine.makeString(text.substr(static_cast<size_t>(start)));
		return engine.makeString(text.substr(static_cast<size_t>(start), static_cast<size_t>(length)));
	});
	
	
	engine.RegisterNative("strpos", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 2, "strpos");
		const std::string text = textOf(engine, args[0], "strpos");
		const std::string needle = textOf(engine, args[1], "strpos");
		const size_t found = text.find(needle);
		if (found == std::string::npos) return integer(-1);
		return integer(static_cast<int>(found));
	});
	
	
	// Части копятся в pins, иначе сборка внутри makeString съест уже готовую часть.
	// В конце обрезаем пины до отметки, а не clearAnchors: чужие anchor хоста должны остаться.
	engine.RegisterNative("strexplode", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 2, "strexplode");
		const std::string text = textOf(engine, args[0], "strexplode");
		const char delimiter = static_cast<char>(asInt(args[1], "strexplode"));
		const size_t mark = engine.anchorCount();
		std::vector<Value> parts;
		std::string piece;
		for (char character : text) {
			if (character == delimiter) {
				Value part = engine.makeString(piece);
				engine.anchor(part);
				parts.push_back(std::move(part));
				piece.clear();
			} else {
				piece.push_back(character);
			}
		}
		Value tail = engine.makeString(piece);
		engine.anchor(tail);
		parts.push_back(std::move(tail));
		Value table = engine.makeTable(std::move(parts));
		engine.resizeAnchors(mark);
		return table;
	});
	
	
	engine.RegisterNative("strupr", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "strupr");
		std::string text = textOf(engine, args[0], "strupr");
		for (char& character : text) {
			character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
		}
		return engine.makeString(std::move(text));
	});
	
	
	engine.RegisterNative("strlwr", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "strlwr");
		std::string text = textOf(engine, args[0], "strlwr");
		for (char& character : text) {
			character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
		}
		return engine.makeString(std::move(text));
	});
	
	
	engine.RegisterNative("strtrim", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 2, "strtrim");
		const std::string text = textOf(engine, args[0], "strtrim");
		const std::string cut = textOf(engine, args[1], "strtrim");
		size_t begin = 0;
		size_t end = text.size();
		while (begin < end && cut.find(text[begin]) != std::string::npos) ++begin;
		while (end > begin && cut.find(text[end - 1]) != std::string::npos) --end;
		return engine.makeString(text.substr(begin, end - begin));
	});
	
	
	engine.RegisterNative("sizeof", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "sizeof");
		if (engine.isReference(args[0])) return integer(0);
		const Type kind = engine.typeOf(args[0]);
		if (kind == Type::String) {
			const std::string text = engine.stringOf(args[0]);
			const int32_t size = static_cast<int>(text.size());
			return integer(text.find('\0') == std::string::npos ? size + 1 : size);
		}
		if (kind == Type::Table) return integer(static_cast<int>(engine.lengthOf(args[0])));
		return integer(0);
	});
	
	
	engine.RegisterNative("tabsize", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "tabsize");
		Value target = engine.follow(args[0]);
		if (engine.typeOf(target) != Type::Table) return integer(0);
		return integer(static_cast<int>(engine.lengthOf(target)));
	});
	
	
	engine.RegisterNative("tabresize", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 2, "tabresize");
		const int size = asInt(args[1], "tabresize");
		engine.resizeTable(args[0], size < 0 ? 0 : static_cast<size_t>(size));
		return integer(0);
	});
	
	
	engine.RegisterNative("typeof", [](Engine& engine, std::span<const Value> args)
	{
		expectArgs(args, 1, "typeof");
		return integer(engine.typeCode(args[0]));
	});
	
	
	// call(fid) или call(аргументы..., fid). Индекс функции — последний аргумент.
	// Неизвестный индекс бросает ошибку из callIndex, а не возвращает 0.
	engine.RegisterNative("call", [](Engine& engine, std::span<const Value> args)
	{
		auto asIndex = [](int value) {
			if (value < 0) throw RError("unknown function");
			return static_cast<size_t>(value);
		};
		if (args.size() == 1 && std::holds_alternative<int>(args[0])) {
			return engine.callIndex(asIndex(asInt(args[0], "call")), {});
		}
		if (args.size() >= 2 && std::holds_alternative<int>(args.back())) {
			std::vector<Value> forwarded(args.begin(), args.end() - 1);
			return engine.callIndex(asIndex(asInt(args.back(), "call")), forwarded);
		}
		return integer(0);
	});
}


