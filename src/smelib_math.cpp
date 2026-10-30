#include "sme/smelib_math.h"

#include "sme/sme_error.h"
#include "sme/sme_value.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>


/*
namespace sme {
namespace {

std::optional<double> asDouble(const Value& value) {
    if (const auto* integer = std::get_if<int>(&value)) {
        return static_cast<double>(*integer);
    }
    if (const auto* real = std::get_if<float>(&value)) {
        return static_cast<double>(*real);
    }
    return std::nullopt;
}

void expectArgs(std::span<const Value> args, size_t count, const char* name) {
    if (args.size() != count) {
        throw RError(std::string(name) + " expects " + std::to_string(count) + " argument(s)");
    }
}

double expectNumber(const Value& value, const char* name) {
    auto number = asDouble(value);
    if (!number) {
        throw RError(std::string(name) + " expects a number");
    }
    return *number;
}

Value realResult(double value) { return real(static_cast<float>(value)); }

}  // анонимное пространство имён
*/

void sme::library::InstallMath(Engine& engine)
{
	engine.RegisterNative("abs", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "abs expects 1 argument");
			return null();
		}
		if (const auto* integerValue = std::get_if<int>(&args[0])) {
			if (*integerValue == std::numeric_limits<int>::min()) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "abs overflow");
				return null();
			}
			return integer(std::abs(*integerValue));
		}
		return real(engine.floatOf(args[0]));
	});
	
	
	engine.RegisterNative("floor", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "floor expects 1 argument");
			return null();
		}
		return real(std::floor(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("ceil", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "ceil expects 1 argument");
			return null();
		}
		return real(std::ceil(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("round", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "round expects 1 argument");
			return null();
		}
		return real(std::round(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("sqrt", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sqrt expects 1 argument");
			return null();
		}
		const float value = engine.floatOf(args[0]);
		if (value < 0) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sqrt expects a non-negative number");
			return null();
		}
		return real(std::sqrt(value));
	});
	
	
	engine.RegisterNative("sin", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sin expects 1 argument");
			return null();
		}
		return real(std::sin(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("cos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "cos expects 1 argument");
			return null();
		}
		return real(std::cos(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("pow", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "pow expects 2 argument");
			return null();
		}
		return real(std::pow(engine.floatOf(args[0]), engine.floatOf(args[1])));
	});
	
	
	engine.RegisterNative("min", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "min expects 2 argument");
			return null();
		}
		if (std::holds_alternative<int>(args[0]) && std::holds_alternative<int>(args[1])) {
			return integer(static_cast<int>(std::min(engine.integerOf(args[0]), engine.integerOf(args[1]))));
		}
		return real(std::min(engine.floatOf(args[0]), engine.floatOf(args[1])));
	});
	
	
	engine.RegisterNative("max", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "max expects 2 argument");
			return null();
		}
		if (std::holds_alternative<int>(args[0]) && std::holds_alternative<int>(args[1])) {
			return integer(static_cast<int>(std::max(engine.integerOf(args[0]), engine.integerOf(args[1]))));
		}
		return real(std::max(engine.floatOf(args[0]), engine.floatOf(args[1])));
	});
	
}


