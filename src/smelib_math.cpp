#include "sme/smelib_math.h"

#include "sme/sme_error.h"
#include "sme/sme_value.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>



void sme::library::InstallMath(Engine& engine)
{
	engine.RegisterNative("abs", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "abs expects 1 argument");
			return Value::Null();
		}
		if (const auto* integerValue = std::get_if<int>(&args[0])) {
			if (*integerValue == std::numeric_limits<int>::min()) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "abs overflow");
				return Value::Null();
			}
			return Value::Integer(std::abs(*integerValue));
		}
		return Value::Float(args[0].ToFloat());
	});
	
	
	engine.RegisterNative("floor", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "floor expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::floor(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("ceil", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "ceil expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::ceil(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("round", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "round expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::round(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("sqrt", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sqrt expects 1 argument");
			return Value::Null();
		}
		const float value = args[0].ToFloat();
		if (value < 0) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sqrt expects a non-negative number");
			return Value::Null();
		}
		return Value::Float(std::sqrt(value));
	});
	
	
	engine.RegisterNative("sin", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "sin expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::sin(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("cos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "cos expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::cos(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("pow", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "pow expects 2 argument");
			return Value::Null();
		}
		return Value::Float(std::pow(args[0].ToFloat(), args[1].ToFloat()));
	});
	
	
	engine.RegisterNative("min", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "min expects 2 argument");
			return Value::Null();
		}
		if (args[0].IsInteger() && args[1].IsInteger()) {
			return Value::Integer(std::min(args[0].ToInteger(), args[1].ToInteger()));
		}
		return Value::Float(std::min(args[0].ToFloat(), args[1].ToFloat()));
	});
	
	
	engine.RegisterNative("max", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "max expects 2 argument");
			return Value::Null();
		}
		if (args[0].IsInteger() && args[1].IsInteger()) {
			return Value::Integer(std::max(args[0].ToInteger(), args[1].ToInteger()));
		}
		return Value::Float(std::max(args[0].ToFloat(), args[1].ToFloat()));
	});
	
}
