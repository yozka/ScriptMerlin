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

std::mt19937& rng() {
    thread_local std::mt19937 generator{std::random_device{}()};
    return generator;
}

int randomInt(int lo, int hi) {
    if (lo > hi) {
        std::swap(lo, hi);
    }
    return std::uniform_int_distribution<int>{lo, hi}(rng());
}

}  // анонимное пространство имён

void addGsLibrary(Engine& engine) {
    engine.addNative("gs_sgn", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_sgn");
        double value = expectNumber(args[0], "gs_sgn");
        if (value > 0) return integer(1);
        if (value < 0) return integer(-1);
        return integer(0);
    });
    engine.addNative("gs_abs", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_abs");
        if (const auto* integerValue = std::get_if<int>(&args[0])) {
            if (*integerValue == std::numeric_limits<int>::min()) throw RError("integer overflow");
            return integer(std::abs(*integerValue));
        }
        return realResult(std::fabs(expectNumber(args[0], "gs_abs")));
    });
    engine.addNative("gs_floor", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_floor");
        return realResult(std::floor(expectNumber(args[0], "gs_floor")));
    });
    engine.addNative("gs_ceil", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_ceil");
        return realResult(std::ceil(expectNumber(args[0], "gs_ceil")));
    });
    engine.addNative("gs_round", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_round");
        return realResult(std::round(expectNumber(args[0], "gs_round")));
    });
    engine.addNative("gs_trunc", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "gs_trunc");
        double value = expectNumber(args[0], "gs_trunc");
        double scale = std::pow(10.0, static_cast<double>(asInt(args[1], "gs_trunc")));
        return realResult(std::trunc(value * scale) / scale);
    });
    engine.addNative("gs_exp", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_exp");
        return realResult(std::exp(expectNumber(args[0], "gs_exp")));
    });
    engine.addNative("gs_log", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_log");
        return realResult(std::log(expectNumber(args[0], "gs_log")));
    });
    engine.addNative("gs_pow", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "gs_pow");
        return realResult(std::pow(expectNumber(args[0], "gs_pow"), expectNumber(args[1], "gs_pow")));
    });
    engine.addNative("gs_sqrt", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_sqrt");
        return realResult(std::sqrt(expectNumber(args[0], "gs_sqrt")));
    });
    engine.addNative("gs_sin", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_sin");
        return realResult(std::sin(expectNumber(args[0], "gs_sin")));
    });
    engine.addNative("gs_cos", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_cos");
        return realResult(std::cos(expectNumber(args[0], "gs_cos")));
    });
    engine.addNative("gs_tan", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_tan");
        return realResult(std::tan(expectNumber(args[0], "gs_tan")));
    });
    engine.addNative("gs_asin", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_asin");
        return realResult(std::asin(expectNumber(args[0], "gs_asin")));
    });
    engine.addNative("gs_acos", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_acos");
        return realResult(std::acos(expectNumber(args[0], "gs_acos")));
    });
    engine.addNative("gs_atan", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "gs_atan");
        return realResult(std::atan(expectNumber(args[0], "gs_atan")));
    });
    engine.addNative("gs_atan2", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "gs_atan2");
        return realResult(std::atan2(expectNumber(args[0], "gs_atan2"), expectNumber(args[1], "gs_atan2")));
    });
    engine.addNative("gs_min", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "gs_min");
        return realResult(std::min(expectNumber(args[0], "gs_min"), expectNumber(args[1], "gs_min")));
    });
    engine.addNative("gs_max", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "gs_max");
        return realResult(std::max(expectNumber(args[0], "gs_max"), expectNumber(args[1], "gs_max")));
    });
    engine.addNative("gs_time", [](Engine&, std::span<const Value>) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
        return integer(static_cast<int>(seconds));
    });
    engine.addNative("gs_random", [](Engine&, std::span<const Value> args) {
        if (args.empty()) {
            return integer(randomInt(0, std::numeric_limits<int>::max()));
        }
        if (args.size() == 1) {
            return integer(randomInt(0, asInt(args[0], "gs_random")));
        }
        if (args.size() == 2) {
            return integer(randomInt(asInt(args[0], "gs_random"), asInt(args[1], "gs_random")));
        }
        throw RError("gs_random expects 0, 1 or 2 argument(s)");
    });
    engine.addNative("gs_rand", [](Engine&, std::span<const Value>) { return integer(0); });
    engine.addNative("gs_fid", [](Engine& engine, std::span<const Value> args) {
        expectArgs(args, 1, "gs_fid");
        const auto index = engine.findFunction(textOf(engine, args[0], "gs_fid"));
        return integer(index ? static_cast<int>(*index) : -1);
    });
    engine.addNative("gs_gid", [](Engine& engine, std::span<const Value> args) {
        expectArgs(args, 1, "gs_gid");
        const auto index = engine.findGlobal(textOf(engine, args[0], "gs_gid"));
        return integer(index ? static_cast<int>(*index) : -1);
    });
}

}  // пространство имён sme
