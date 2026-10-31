#include "sme/smelib_gs.h"

#include "sme/sme_error.h"
#include "sme/sme_value.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>

 
namespace
{
	
	std::mt19937& rng()
	{
		thread_local std::mt19937 generator{std::random_device{}()};
		return generator;
	}

	int randomInt(int lo, int hi)
	{
		if (lo > hi) {
			std::swap(lo, hi);
		}
		return std::uniform_int_distribution<int>{lo, hi}(rng());
	}

}


void sme::library::InstallGS(Engine& engine)
{
	
	engine.RegisterNative("gs_sgn", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_sgn expects 1 argument");
			return Value::Null();
		}
		const int value = args[0].ToInteger();
		if (value > 0) return Value::Integer(1);
		if (value < 0) return Value::Integer(-1);
		return Value::Integer(0);
	});
	
	
	engine.RegisterNative("gs_abs", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_abs expects 1 argument");
			return Value::Null();
		}
		if (const auto* integerValue = std::get_if<int>(&args[0])) {
			if (*integerValue == std::numeric_limits<int>::min()) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_abs overflow");
				return Value::Null();
			}
			return Value::Integer(std::abs(*integerValue));
		}
		return Value::Float(args[0].ToFloat());
	});
	
	
	engine.RegisterNative("gs_floor", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_floor expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::floor(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_ceil", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_ceil expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::ceil(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_round", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_round expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::round(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_trunc", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_trunc expects 2 arguments");
			return Value::Null();
		}
		auto value = args[0].ToFloat();
		auto scale = std::pow(10.0, args[1].ToFloat());
		return Value::Float(std::trunc(value * scale) / scale);
	});
	
	
	engine.RegisterNative("gs_exp", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_exp expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::exp(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_log", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_log expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::log(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_pow", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_pow expects 2 arguments");
			return Value::Null();
		}
		return Value::Float(std::pow(args[0].ToFloat(), args[1].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_sqrt", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_sqrt expects 1 argument");
			return Value::Null();
		}
		const float value = args[0].ToFloat();
		if (value < 0) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_sqrt expects a non-negative number");
			return Value::Null();
		}
		return Value::Float(std::sqrt(value));
	});
	
	
	engine.RegisterNative("gs_sin", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_sin expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::sin(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_cos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_cos expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::cos(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_tan", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_tan expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::tan(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_asin", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_asin expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::asin(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_acos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_acos expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::acos(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_atan", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_atan expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::atan(args[0].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_atan2", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_atan2 expects 1 argument");
			return Value::Null();
		}
		return Value::Float(std::atan2(args[0].ToFloat(), args[1].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_min", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_min expects 2 argument");
			return Value::Null();
		}
		if (args[0].IsInteger() && args[1].IsInteger()) {
			return Value::Integer(std::min(args[0].ToInteger(), args[1].ToInteger()));
		}
		return Value::Float(std::min(args[0].ToFloat(), args[1].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_max", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_max expects 2 argument");
			return Value::Null();
		}
		if (args[0].IsInteger() && args[1].IsInteger()) {
			return Value::Integer(std::max(args[0].ToInteger(), args[1].ToInteger()));
		}
		return Value::Float(std::max(args[0].ToFloat(), args[1].ToFloat()));
	});
	
	
	engine.RegisterNative("gs_time", [](Engine&, std::span<const Value>)
	{
		const auto now = std::chrono::system_clock::now().time_since_epoch();
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
		return Value::Integer(static_cast<int>(seconds));
	});
	
	
	engine.RegisterNative("gs_random", [](Engine& engine, std::span<const Value> args)
	{
		if (args.empty()) {
			return Value::Integer(randomInt(0, std::numeric_limits<int>::max()));
		}
		if (args.size() == 1) {
			return Value::Integer(randomInt(0, args[0].ToInteger()));
		}
		if (args.size() == 2) {
			return Value::Integer(randomInt(args[0].ToInteger(), args[1].ToInteger()));
		}
		engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_random expects 0, 1 or 2 argument(s)");
		return Value::Null();
	});
	
	
	engine.RegisterNative("gs_rand", [](Engine&, std::span<const Value>) { return Value::Integer(0); });
	
	
	engine.RegisterNative("gs_fid", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_fid expects 1 argument");
			return Value::Null();
		}

		const auto index = engine.findFunction(args[0].ToString());
		return Value::Integer(index ? static_cast<int>(*index) : -1);
	});
	
	
	engine.RegisterNative("gs_gid", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_gid expects 1 argument");
			return Value::Null();
		}

		const auto index = engine.findGlobal(args[0].ToString());
		return Value::Integer(index ? static_cast<int>(*index) : -1);
	});
}
