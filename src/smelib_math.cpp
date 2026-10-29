#include "sme/smelib_math.h"

#include "sme/sme_error.h"
#include "sme/sme_value.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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

}  // анонимное пространство имён

void addMathLibrary(Engine& engine) {
    engine.addNative("abs", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "abs");
        if (const auto* integerValue = std::get_if<int>(&args[0])) {
            if (*integerValue == std::numeric_limits<int>::min()) throw RError("integer overflow");
            return integer(std::abs(*integerValue));
        }
        return realResult(std::fabs(expectNumber(args[0], "abs")));
    });
    engine.addNative("floor", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "floor");
        return realResult(std::floor(expectNumber(args[0], "floor")));
    });
    engine.addNative("ceil", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "ceil");
        return realResult(std::ceil(expectNumber(args[0], "ceil")));
    });
    engine.addNative("round", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "round");
        return realResult(std::round(expectNumber(args[0], "round")));
    });
    engine.addNative("sqrt", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "sqrt");
        double value = expectNumber(args[0], "sqrt");
        if (value < 0) throw RError("sqrt expects a non-negative number");
        return realResult(std::sqrt(value));
    });
    engine.addNative("sin", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "sin");
        return realResult(std::sin(expectNumber(args[0], "sin")));
    });
    engine.addNative("cos", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 1, "cos");
        return realResult(std::cos(expectNumber(args[0], "cos")));
    });
    engine.addNative("pow", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "pow");
        return realResult(std::pow(expectNumber(args[0], "pow"), expectNumber(args[1], "pow")));
    });
    engine.addNative("min", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "min");
        double lhs = expectNumber(args[0], "min");
        double rhs = expectNumber(args[1], "min");
        if (!std::holds_alternative<float>(args[0]) && !std::holds_alternative<float>(args[1])) {
            return integer(static_cast<int>(std::min(lhs, rhs)));
        }
        return realResult(std::min(lhs, rhs));
    });
    engine.addNative("max", [](Engine&, std::span<const Value> args) {
        expectArgs(args, 2, "max");
        double lhs = expectNumber(args[0], "max");
        double rhs = expectNumber(args[1], "max");
        if (!std::holds_alternative<float>(args[0]) && !std::holds_alternative<float>(args[1])) {
            return integer(static_cast<int>(std::max(lhs, rhs)));
        }
        return realResult(std::max(lhs, rhs));
    });
}

}  // пространство имён sme
