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
			return Value::null();
		}
		const int value = engine.integerOf(args[0]);
		if (value > 0) return Value::integer(1);
		if (value < 0) return Value::integer(-1);
		return Value::integer(0);
	});
	
	
	engine.RegisterNative("gs_abs", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_abs expects 1 argument");
			return Value::null();
		}
		if (const auto* integerValue = std::get_if<int>(&args[0])) {
			if (*integerValue == std::numeric_limits<int>::min()) {
				engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_abs overflow");
				return Value::null();
			}
			return Value::integer(std::abs(*integerValue));
		}
		return Value::real(engine.floatOf(args[0]));
	});
	
	
	engine.RegisterNative("gs_floor", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_floor expects 1 argument");
			return Value::null();
		}
		return Value::real(std::floor(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_ceil", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_ceil expects 1 argument");
			return Value::null();
		}
		return Value::real(std::ceil(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_round", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_round expects 1 argument");
			return Value::null();
		}
		return Value::real(std::round(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_trunc", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_trunc expects 2 arguments");
			return Value::null();
		}
		auto value = engine.floatOf(args[0]);
		auto scale = std::pow(10.0, engine.floatOf(args[1]));
		return Value::real(std::trunc(value * scale) / scale);
	});
	
	
	engine.RegisterNative("gs_exp", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_exp expects 1 argument");
			return Value::null();
		}
		return Value::real(std::exp(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_log", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_log expects 1 argument");
			return Value::null();
		}
		return Value::real(std::log(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_pow", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_pow expects 2 arguments");
			return Value::null();
		}
		return Value::real(std::pow(engine.floatOf(args[0]), engine.floatOf(args[1])));
	});
	
	
	engine.RegisterNative("gs_sqrt", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_sqrt expects 1 argument");
			return Value::null();
		}
		const float value = engine.floatOf(args[0]);
		if (value < 0) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_sqrt expects a non-negative number");
			return Value::null();
		}
		return Value::real(std::sqrt(value));
	});
	
	
	engine.RegisterNative("gs_sin", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_sin expects 1 argument");
			return Value::null();
		}
		return Value::real(std::sin(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_cos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_cos expects 1 argument");
			return Value::null();
		}
		return Value::real(std::cos(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_tan", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_tan expects 1 argument");
			return Value::null();
		}
		return Value::real(std::tan(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_asin", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_asin expects 1 argument");
			return Value::null();
		}
		return Value::real(std::asin(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_acos", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_acos expects 1 argument");
			return Value::null();
		}
		return Value::real(std::acos(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_atan", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_atan expects 1 argument");
			return Value::null();
		}
		return Value::real(std::atan(engine.floatOf(args[0])));
	});
	
	
	engine.RegisterNative("gs_atan2", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_atan2 expects 1 argument");
			return Value::null();
		}
		return Value::real(std::atan2(engine.floatOf(args[0]), engine.floatOf(args[1])));
	});
	
	
	engine.RegisterNative("gs_min", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_min expects 2 argument");
			return Value::null();
		}
		if (std::holds_alternative<int>(args[0]) && std::holds_alternative<int>(args[1])) {
			return Value::integer(static_cast<int>(std::min(engine.integerOf(args[0]), engine.integerOf(args[1]))));
		}
		return Value::real(std::min(engine.floatOf(args[0]), engine.floatOf(args[1])));
	});
	
	
	engine.RegisterNative("gs_max", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 2) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_max expects 2 argument");
			return Value::null();
		}
		if (std::holds_alternative<int>(args[0]) && std::holds_alternative<int>(args[1])) {
			return Value::integer(static_cast<int>(std::max(engine.integerOf(args[0]), engine.integerOf(args[1]))));
		}
		return Value::real(std::max(engine.floatOf(args[0]), engine.floatOf(args[1])));
	});
	
	
	engine.RegisterNative("gs_time", [](Engine&, std::span<const Value>)
	{
		const auto now = std::chrono::system_clock::now().time_since_epoch();
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
		return Value::integer(static_cast<int>(seconds));
	});
	
	
	engine.RegisterNative("gs_random", [](Engine& engine, std::span<const Value> args)
	{
		if (args.empty()) {
			return Value::integer(randomInt(0, std::numeric_limits<int>::max()));
		}
		if (args.size() == 1) {
			return Value::integer(randomInt(0, engine.integerOf(args[0])));
		}
		if (args.size() == 2) {
			return Value::integer(randomInt(engine.integerOf(args[0]), engine.integerOf(args[1])));
		}
		engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_random expects 0, 1 or 2 argument(s)");
		return Value::null();
	});
	
	
	engine.RegisterNative("gs_rand", [](Engine&, std::span<const Value>) { return Value::integer(0); });
	
	
	engine.RegisterNative("gs_fid", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_fid expects 1 argument");
			return Value::null();
		}

		const auto index = engine.findFunction(engine.stringOf(args[0]));
		return Value::integer(index ? static_cast<int>(*index) : -1);
	});
	
	
	engine.RegisterNative("gs_gid", [](Engine& engine, std::span<const Value> args)
	{
		if (args.size() != 1) {
			engine.Error(error::Code::FUNCTION_ARGUMENTS, "gs_gid expects 1 argument");
			return Value::null();
		}

		const auto index = engine.findGlobal(engine.stringOf(args[0]));
		return Value::integer(index ? static_cast<int>(*index) : -1);
	});
}


