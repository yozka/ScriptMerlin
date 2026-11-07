#include "sme_compiler.h"

#include "sme/sme_error.h"

// Компилятор функции. Сначала collect объявляет все локалы, включая те,
// что появились из присваивания в неизвестное имя. Потом codegen кладёт
// значения в регистры. Временные регистры живут один оператор: watermark
// откатывается, а слоты перед оператором обнуляются.

#include <unordered_map>
#include <optional>
#include <utility>

namespace sme {
namespace {

class FunctionCompiler {
public:
    FunctionCompiler(const Function& function,
                     const std::unordered_map<std::string, uint16_t>& functions,
                     const CompileEnv& env,
                     std::string_view source,
                     error::Error::Opt& error)
        : _function(function)
        , _functions(functions)
        , _env(env)
        , _source(source)
        , _error(error) {}

    std::optional<Chunk> Run() {
        for (const std::string& param : _function.params) {
            if (_error) break;
            Declare(param, _function.line);
        }
        for (const auto& stmt : _function.body) {
            if (_error) break;
            Collect(*stmt);
        }
        if (_failed) return std::nullopt;
        _watermark = static_cast<uint16_t>(_locals.size());
        _regCount = _watermark;
        CompileBody(_function.body);
        if (_failed) return std::nullopt;
        FinishLabels();
        if (_failed) return std::nullopt;
        uint16_t result = AllocTemp();
        Emit(Op::LoadConst, result, AddConst(std::monostate{}), 0, 0, 0, _function.line);
        Emit(Op::Return, result, 0, 0, 0, 0, _function.line);

        Chunk chunk;
        chunk.name = _function.name;
        chunk.parameterCount = static_cast<uint16_t>(_function.params.size());
        chunk.registerCount = _regCount;
        chunk.code = std::move(_code);
        chunk.constants = std::move(_constants);
        return chunk;
    }

private:
    struct Loop {
        std::vector<uint16_t> breaks;
        std::vector<uint16_t> continues;
    };

    const Function& _function;
    const std::unordered_map<std::string, uint16_t>& _functions;
    const CompileEnv& _env;
    std::string _source;
    error::Error::Opt& _error;
    bool _failed = false;
    std::unordered_map<std::string, uint16_t> _locals;
    std::unordered_map<std::string, uint16_t> _labels;
    std::unordered_map<std::string, std::vector<uint16_t>> _pendingGotos;
    std::vector<Instruction> _code;
    std::vector<Constant> _constants;
    std::vector<Loop> _loops;
    uint16_t _watermark = 0;
    uint16_t _regCount = 0;

    void Report(error::Code code, std::string message, int line = 0) {
        _failed = true;
        if (_error) return;
        error::Location location;
        location.source = _source;
        location.function = _function.name;
        location.line = line;
        _error.emplace(code, std::move(message), std::move(location));
    }

    void Declare(const std::string& name, int line) {
        if (_locals.contains(name)) {
            Report(error::Code::COMPILATION_DUPLICATE_VARIABLE, "duplicate variable '" + name + "'", line);
            return;
        }
        if (_locals.size() >= 65535) {
            Report(error::Code::COMPILATION_TOO_MANY_LOCAL_VARIABLES, "too many local variables", line);
            return;
        }
        _locals.emplace(name, static_cast<uint16_t>(_locals.size()));
    }

    // Присваивание в неизвестное имя молча заводит локал. Опечатка в левой
    // части поэтому не ошибка, а новая переменная. Глобал с таким именем не перекрывается.
    void Note(const std::string& name, int line) {
        if (_locals.contains(name) || _env.globals.contains(name) || _env.constants.contains(name)) {
            return;
        }
        Declare(name, line);
    }

    void CollectExpr(const Expr& expr) {
        if (_error) return;
        if ((expr.kind == Expr::Kind::Assign || expr.kind == Expr::Kind::Modify) && !expr.kids.empty() &&
            expr.kids[0]->kind == Expr::Kind::Name) {
            Note(expr.kids[0]->text, expr.kids[0]->line);
        }
        if ((expr.kind == Expr::Kind::PostInc || expr.kind == Expr::Kind::PostDec) && !expr.kids.empty() &&
            expr.kids[0]->kind == Expr::Kind::Name) {
            Note(expr.kids[0]->text, expr.line);
        }
        for (const auto& kid : expr.kids) {
            if (kid) {
                CollectExpr(*kid);
            }
        }
    }

    // Второй var с тем же именем — ошибка. Параметр и var с тем же именем тоже.
    // Присваивание в новое имя локал создаёт, var для этого не обязателен.
    void Collect(const Stmt& stmt) {
        if (_error) return;
        if (stmt.kind == Stmt::Kind::Var) {
            Declare(stmt.name, stmt.line);
        }
        if (stmt.kind == Stmt::Kind::For && !_locals.contains(stmt.name) &&
            !_env.globals.contains(stmt.name) && !_env.constants.contains(stmt.name)) {
            Declare(stmt.name, stmt.line);
        }
        if (stmt.expr) CollectExpr(*stmt.expr);
        if (stmt.expr2) CollectExpr(*stmt.expr2);
        if (stmt.expr3) CollectExpr(*stmt.expr3);
        for (const auto& child : stmt.body) Collect(*child);
        for (const auto& child : stmt.elseBody) Collect(*child);
    }

    uint16_t AllocTemp() {
        if (_watermark == 65535) {
            Report(error::Code::COMPILATION_TOO_MANY_REGISTERS, "too many registers", _function.line);
            return 0;
        }
        uint16_t reg = _watermark++;
        if (_watermark > _regCount) {
            _regCount = _watermark;
        }
        return reg;
    }

    uint16_t AddConst(Constant constant) {
        if (_constants.size() >= 65535) {
            Report(error::Code::COMPILATION_TOO_MANY_CONSTANTS, "too many constants", _function.line);
            return 0;
        }
        _constants.push_back(std::move(constant));
        return static_cast<uint16_t>(_constants.size() - 1);
    }

    uint16_t Emit(Op op, uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, int line) {
        if (_code.size() >= 65535) {
            Report(error::Code::COMPILATION_FUNCTION_TOO_LARGE, "function is too large", line);
            return 0;
        }
        Instruction ins;
        ins.op = op;
        ins.a = a;
        ins.b = b;
        ins.c = c;
        ins.d = d;
        ins.e = e;
        ins.line = static_cast<uint16_t>(line > 0 && line <= 65535 ? line : 1);
        _code.push_back(ins);
        return static_cast<uint16_t>(_code.size() - 1);
    }

    uint16_t Here() const { return static_cast<uint16_t>(_code.size()); }

    uint16_t CompileExpr(const Expr& expr) {
        switch (expr.kind) {
        case Expr::Kind::Int: {
            uint16_t dest = AllocTemp();
            Emit(Op::LoadConst, dest, AddConst(expr.intValue), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Float: {
            uint16_t dest = AllocTemp();
            Emit(Op::LoadConst, dest, AddConst(expr.floatValue), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Bool: {
            uint16_t dest = AllocTemp();
            Emit(Op::LoadConst, dest, AddConst(int{expr.boolValue ? 1 : 0}), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Null: {
            uint16_t dest = AllocTemp();
            if (expr.intValue == 1) Emit(Op::LoadConst, dest, AddConst(nullptr), 0, 0, 0, expr.line);
            else Emit(Op::LoadConst, dest, AddConst(std::monostate{}), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::String: {
            uint16_t dest = AllocTemp();
            Emit(Op::LoadConst, dest, AddConst(expr.text), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Name:
            return CompileName(expr);
        case Expr::Kind::Unary:
            return CompileUnary(expr);
        case Expr::Kind::Binary:
            return CompileBinary(expr);
        case Expr::Kind::Call:
            return CompileCall(expr);
        case Expr::Kind::Index:
        case Expr::Kind::CharIndex:
            return CompileIndex(expr);
        case Expr::Kind::Member:
            return CompileMember(expr);
        case Expr::Kind::List:
            return CompileList(expr);
        case Expr::Kind::Assign:
            return CompileAssign(expr);
        case Expr::Kind::Modify:
            return CompileModify(expr);
        case Expr::Kind::Ternary:
            return CompileTernary(expr);
        case Expr::Kind::Cast:
            return CompileCast(expr);
        case Expr::Kind::Addr:
            return CompileAddr(expr);
        case Expr::Kind::Deref:
            return CompileDeref(expr);
        case Expr::Kind::PostInc:
            return CompileUpdate(expr, 1);
        case Expr::Kind::PostDec:
            return CompileUpdate(expr, -1);
        case Expr::Kind::NewObj: {
            std::vector<uint16_t> args;
            args.reserve(expr.kids.size());
            for (const auto& arg : expr.kids) {
                args.push_back(CompileExpr(*arg));
            }
            uint16_t base = _watermark;
            for (uint16_t arg : args) {
                uint16_t slot = AllocTemp();
                Emit(Op::Copy, slot, arg, 0, 0, 0, expr.line);
            }
            uint16_t dest = AllocTemp();
            Emit(Op::NewObj, dest, AddConst(expr.text), base, static_cast<uint16_t>(args.size()), 0, expr.line);
            return dest;
        }
        case Expr::Kind::Query:
            return CompileQuery(expr);
        }
        Report(error::Code::COMPILATION_INTERNAL_EXPRESSION, "internal expression", expr.line);
        return 0;
    }

    // ?имя. Неизвестное имя — 0. Иначе код типа значения. Для непереданного
    // параметра слот null, код 0, поэтому if (!?x) x = значение по умолчанию работает.
    uint16_t CompileQuery(const Expr& expr) {
        uint16_t dest = AllocTemp();
        if (!_locals.contains(expr.text) && !_env.globals.contains(expr.text) &&
            !_env.constants.contains(expr.text)) {
            Emit(Op::LoadConst, dest, AddConst(int{0}), 0, 0, 0, expr.line);
            return dest;
        }
        Expr name;
        name.kind = Expr::Kind::Name;
        name.text = expr.text;
        name.line = expr.line;
        uint16_t value = CompileName(name);
        Emit(Op::Typeof, dest, value, 0, 0, 0, expr.line);
        return dest;
    }

    uint16_t CompileName(const Expr& expr) {
        if (auto it = _locals.find(expr.text); it != _locals.end()) {
            return it->second;
        }
        if (auto it = _env.constants.find(expr.text); it != _env.constants.end()) {
            uint16_t dest = AllocTemp();
            Constant constant;
            const Value& value = it->second;
            if (const auto* integer = std::get_if<int>(&value)) constant = *integer;
            else if (const auto* real = std::get_if<float>(&value)) constant = *real;
            else if (const auto* boolean = std::get_if<bool>(&value)) constant = *boolean;
            else if (value.IsString()) constant = value.ToString();
            else if (value.IsPointer()) constant = nullptr;
            Emit(Op::LoadConst, dest, AddConst(std::move(constant)), 0, 0, 0, expr.line);
            return dest;
        }
        if (auto it = _env.globals.find(expr.text); it != _env.globals.end()) {
            uint16_t dest = AllocTemp();
            Emit(Op::LoadGlobal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        if (_functions.contains(expr.text) || _env.natives.contains(expr.text)) {
            Report(error::Code::COMPILATION_FUNCTION_MUST_BE_CALLED,
                "function '" + expr.text + "' must be called", expr.line);
            return 0;
        }
        uint16_t dest = AllocTemp();
        Emit(Op::LoadConst, dest, AddConst(std::monostate{}), 0, 0, 0, expr.line);
        return dest;
    }

    uint16_t CompileUnary(const Expr& expr) {
        uint16_t value = CompileExpr(*expr.kids[0]);
        uint16_t dest = AllocTemp();
        Op op = Op::Not;
        if (expr.unary == UnaryOp::Neg) op = Op::Neg;
        else if (expr.unary == UnaryOp::BitNot) op = Op::BitNot;
        Emit(op, dest, value, 0, 0, 0, expr.line);
        return dest;
    }

    uint16_t CompileBinary(const Expr& expr) {
        if (expr.binary == BinaryOp::And || expr.binary == BinaryOp::Or) {
            return CompileLogic(expr);
        }
        const Expr& right = *expr.kids[1];
        if (right.kind == Expr::Kind::Int && right.intValue >= -32768 && right.intValue <= 32767) {
            uint16_t lhs = CompileExpr(*expr.kids[0]);
            uint16_t dest = AllocTemp();
            Emit(Op::BinaryImm, dest, lhs, static_cast<uint16_t>(BinaryOp(expr.binary)), static_cast<uint16_t>(static_cast<int16_t>(right.intValue)), 0, expr.line);
            return dest;
        }
        uint16_t lhs = CompileExpr(*expr.kids[0]);
        uint16_t rhs = CompileExpr(*expr.kids[1]);
        uint16_t dest = AllocTemp();
        Emit(BinaryOp(expr.binary), dest, lhs, rhs, 0, 0, expr.line);
        return dest;
    }

    // Оба операнда вычисляются всегда. Короткой схемы &&/|| нет: это зафиксировано тестами.
    uint16_t CompileLogic(const Expr& expr) {
        uint16_t lhs = CompileExpr(*expr.kids[0]);
        uint16_t rhs = CompileExpr(*expr.kids[1]);
        uint16_t dest = AllocTemp();
        Op op = expr.binary == BinaryOp::And ? Op::LogicAnd : Op::LogicOr;
        Emit(op, dest, lhs, rhs, 0, 0, expr.line);
        return dest;
    }

    // var(индекс) обрабатывается до копирования аргументов: RefGlobal читает
    // регистр с индексом глобала, а не слот вызова.
    uint16_t CompileCall(const Expr& expr) {
        const Expr& callee = *expr.kids[0];
        std::vector<uint16_t> args;
        for (size_t i = 1; i < expr.kids.size(); ++i) {
            args.push_back(CompileExpr(*expr.kids[i]));
        }
        if (callee.kind == Expr::Kind::Name && callee.text == "var" && args.size() == 1) {
            uint16_t dest = AllocTemp();
            Emit(Op::RefGlobal, dest, args[0], 0, 0, 0, expr.line);
            return dest;
        }
        uint16_t base = _watermark;
        for (uint16_t arg : args) {
            uint16_t slot = AllocTemp();
            Emit(Op::Copy, slot, arg, 0, 0, 0, expr.line);
        }
        uint16_t dest = AllocTemp();
        uint16_t argc = static_cast<uint16_t>(args.size());
        if (callee.kind == Expr::Kind::Name) {
            if (auto function = _functions.find(callee.text); function != _functions.end()) {
                Emit(Op::Call, dest, function->second, base, argc, 0, expr.line);
                return dest;
            }
            if (auto native = _env.natives.find(callee.text); native != _env.natives.end()) {
                Emit(Op::CallNative, dest, native->second, base, argc, 0, expr.line);
                return dest;
            }
            Emit(Op::CallName, dest, AddConst(callee.text), base, argc, 0, expr.line);
            return dest;
        }
        if (callee.kind == Expr::Kind::Member) {
            uint16_t object = CompileExpr(*callee.kids[0]);
            uint16_t name = AddConst(callee.text);
            Emit(Op::CallMethod, dest, object, name, base, argc, expr.line);
            return dest;
        }
        Report(error::Code::COMPILATION_EXPRESSION_NOT_CALLABLE, "expression is not a function", expr.line);
        return 0;
    }

    uint16_t CompileIndex(const Expr& expr) {
        uint16_t object = CompileExpr(*expr.kids[0]);
        uint16_t index = CompileExpr(*expr.kids[1]);
        uint16_t dest = AllocTemp();
        Emit(expr.kind == Expr::Kind::CharIndex ? Op::GetChar : Op::GetIndex, dest, object, index, 0, 0, expr.line);
        return dest;
    }

    uint16_t CompileMember(const Expr& expr) {
        uint16_t object = CompileExpr(*expr.kids[0]);
        uint16_t dest = AllocTemp();
        Emit(Op::GetProp, dest, object, AddConst(expr.text), 0, 0, expr.line);
        return dest;
    }

    uint16_t CompileList(const Expr& expr) {
        std::vector<uint16_t> items;
        items.reserve(expr.kids.size());
        for (const auto& kid : expr.kids) {
            items.push_back(CompileExpr(*kid));
        }
        uint16_t base = _watermark;
        for (uint16_t item : items) {
            uint16_t slot = AllocTemp();
            Emit(Op::Copy, slot, item, 0, 0, 0, expr.line);
        }
        uint16_t dest = AllocTemp();
        Emit(Op::MakeTable, dest, base, static_cast<uint16_t>(items.size()), 0, 0, expr.line);
        return dest;
    }

    uint16_t CompileAssign(const Expr& expr) {
        const Expr& target = *expr.kids[0];
        const Expr& valueExpr = *expr.kids[1];
        if (target.kind == Expr::Kind::Name) {
            const auto local = _locals.find(target.text);
            if (local != _locals.end()) {
                const uint16_t dest = local->second;
                if (valueExpr.kind == Expr::Kind::Binary) {
                    const uint16_t lhs = CompileExpr(*valueExpr.kids[0]);
                    const Expr& right = *valueExpr.kids[1];
                    if (valueExpr.binary == BinaryOp::And || valueExpr.binary == BinaryOp::Or) {
                        const uint16_t rhs = CompileExpr(right);
                        const Op op = valueExpr.binary == BinaryOp::And ? Op::LogicAnd : Op::LogicOr;
                        Emit(op, dest, lhs, rhs, 0, 0, expr.line);
                    } else if (right.kind == Expr::Kind::Int &&
                        right.intValue >= -32768 && right.intValue <= 32767) {
                        Emit(Op::BinaryImm, dest, lhs,
                             static_cast<uint16_t>(BinaryOp(valueExpr.binary)),
                             static_cast<uint16_t>(static_cast<int16_t>(right.intValue)), 0, expr.line);
                    } else {
                        const uint16_t rhs = CompileExpr(right);
                        Emit(BinaryOp(valueExpr.binary), dest, lhs, rhs, 0, 0, expr.line);
                    }
                    return dest;
                }
                if (valueExpr.kind == Expr::Kind::Unary) {
                    const uint16_t value = CompileExpr(*valueExpr.kids[0]);
                    Op op = Op::Not;
                    if (valueExpr.unary == UnaryOp::Neg) op = Op::Neg;
                    else if (valueExpr.unary == UnaryOp::BitNot) op = Op::BitNot;
                    Emit(op, dest, value, 0, 0, 0, expr.line);
                    return dest;
                }
                if (valueExpr.kind == Expr::Kind::Cast) {
                    const uint16_t value = CompileExpr(*valueExpr.kids[0]);
                    uint16_t kind = 2;
                    if (valueExpr.text == "int") kind = 0;
                    else if (valueExpr.text == "float") kind = 1;
                    const uint16_t format = valueExpr.format.empty()
                        ? static_cast<uint16_t>(65535)
                        : AddConst(valueExpr.format);
                    Emit(Op::Cast, dest, value, kind, format, 0, expr.line);
                    return dest;
                }
            }
        }
        uint16_t value = CompileExpr(*expr.kids[1]);
        Store(target, value, expr.line);
        return value;
    }

    struct Place {
        enum class Kind { Local, Global, Index, Char, Prop, Ref } kind = Kind::Local;
        uint16_t primary = 0;
        uint16_t secondary = 0;
    };

    // Адрес цели присваивания вычисляется один раз. Иначе obj.prop += 1
    // дважды вызвал бы выражение объекта, и чтение с записью могли разъехаться.
    Place PlaceOf(const Expr& target, const char* invalid) {
        if (target.kind == Expr::Kind::Name) {
            if (auto it = _locals.find(target.text); it != _locals.end()) {
                return Place{Place::Kind::Local, it->second, 0};
            }
            if (_env.constants.contains(target.text)) {
                Report(error::Code::COMPILATION_INVALID_ASSIGNMENT_TARGET,
                    "cannot assign to constant '" + target.text + "'", target.line);
                return {};
            }
            if (auto it = _env.globals.find(target.text); it != _env.globals.end()) {
                return Place{Place::Kind::Global, it->second, 0};
            }
            Report(error::Code::COMPILATION_UNKNOWN_VARIABLE,
                "unknown variable '" + target.text + "'", target.line);
            return {};
        }
        if (target.kind == Expr::Kind::Index || target.kind == Expr::Kind::CharIndex) {
            Place place;
            place.kind = target.kind == Expr::Kind::CharIndex ? Place::Kind::Char : Place::Kind::Index;
            place.primary = CompileExpr(*target.kids[0]);
            place.secondary = CompileExpr(*target.kids[1]);
            return place;
        }
        if (target.kind == Expr::Kind::Member) {
            return Place{Place::Kind::Prop, CompileExpr(*target.kids[0]), AddConst(target.text)};
        }
        if (target.kind == Expr::Kind::Deref) {
            return Place{Place::Kind::Ref, CompileExpr(*target.kids[0]), 0};
        }
        if (target.kind == Expr::Kind::Call) {
            return Place{Place::Kind::Ref, CompileExpr(target), 0};
        }
        Report(error::Code::COMPILATION_INVALID_ASSIGNMENT_TARGET, invalid, target.line);
        return {};
    }

    uint16_t LoadPlace(const Place& place, int line) {
        switch (place.kind) {
        case Place::Kind::Local:
            return place.primary;
        case Place::Kind::Global: {
            uint16_t dest = AllocTemp();
            Emit(Op::LoadGlobal, dest, place.primary, 0, 0, 0, line);
            return dest;
        }
        case Place::Kind::Index:
        case Place::Kind::Char: {
            uint16_t dest = AllocTemp();
            Emit(place.kind == Place::Kind::Char ? Op::GetChar : Op::GetIndex, dest, place.primary, place.secondary, 0, 0, line);
            return dest;
        }
        case Place::Kind::Prop: {
            uint16_t dest = AllocTemp();
            Emit(Op::GetProp, dest, place.primary, place.secondary, 0, 0, line);
            return dest;
        }
        case Place::Kind::Ref: {
            uint16_t dest = AllocTemp();
            Emit(Op::LoadRef, dest, place.primary, 0, 0, 0, line);
            return dest;
        }
        }
        Report(error::Code::COMPILATION_INTERNAL_ASSIGNMENT, "internal assignment", line);
        return 0;
    }

    void StorePlace(const Place& place, uint16_t value, int line) {
        switch (place.kind) {
        case Place::Kind::Local:
            Emit(Op::Move, place.primary, value, 0, 0, 0, line);
            return;
        case Place::Kind::Global:
            Emit(Op::StoreGlobal, place.primary, value, 0, 0, 0, line);
            return;
        case Place::Kind::Index:
            Emit(Op::SetIndex, place.primary, place.secondary, value, 0, 0, line);
            return;
        case Place::Kind::Char:
            Emit(Op::SetChar, place.primary, place.secondary, value, 0, 0, line);
            return;
        case Place::Kind::Prop:
            Emit(Op::SetProp, place.primary, place.secondary, value, 0, 0, line);
            return;
        case Place::Kind::Ref:
            Emit(Op::StoreRef, place.primary, value, 0, 0, 0, line);
            return;
        }
        Report(error::Code::COMPILATION_INTERNAL_ASSIGNMENT, "internal assignment", line);
    }

    void Store(const Expr& target, uint16_t value, int line) {
        StorePlace(PlaceOf(target, "invalid assignment target"), value, line);
    }

    uint16_t CompileModify(const Expr& expr) {
        Place place = PlaceOf(*expr.kids[0], "invalid assignment target");
        const Expr& right = *expr.kids[1];
        if (place.kind == Place::Kind::Local && right.kind == Expr::Kind::Int &&
            right.intValue >= -32768 && right.intValue <= 32767) {
            Emit(Op::BinaryImm, place.primary, place.primary,
                 static_cast<uint16_t>(BinaryOp(expr.binary)),
                 static_cast<uint16_t>(static_cast<int16_t>(right.intValue)), 0, expr.line);
            return place.primary;
        }
        uint16_t current = LoadPlace(place, expr.line);
        uint16_t rhs = CompileExpr(right);
        uint16_t dest = AllocTemp();
        Emit(BinaryOp(expr.binary), dest, current, rhs, 0, 0, expr.line);
        StorePlace(place, dest, expr.line);
        return dest;
    }

    uint16_t CompileUpdate(const Expr& expr, int delta) {
        Place place = PlaceOf(*expr.kids[0], "invalid update target");
        if (place.kind == Place::Kind::Local) {
            uint16_t saved = place.primary;
            if (!expr.prefix) {
                saved = AllocTemp();
                Emit(Op::Copy, saved, place.primary, 0, 0, 0, expr.line);
            }
            Emit(Op::BinaryImm, place.primary, place.primary,
                 static_cast<uint16_t>(Op::Add), static_cast<uint16_t>(delta), 0, expr.line);
            return expr.prefix ? place.primary : saved;
        }
        uint16_t current = LoadPlace(place, expr.line);
        uint16_t saved = current;
        uint16_t step = AllocTemp();
        Emit(Op::LoadConst, step, AddConst(delta), 0, 0, 0, expr.line);
        uint16_t next = AllocTemp();
        Emit(Op::Add, next, current, step, 0, 0, expr.line);
        StorePlace(place, next, expr.line);
        return expr.prefix ? next : saved;
    }

    uint16_t CompileTernary(const Expr& expr) {
        uint16_t dest = AllocTemp();
        const uint16_t jumpFalse = CompileConditionJumpFalse(*expr.kids[0], expr.line);
        uint16_t yes = CompileExpr(*expr.kids[1]);
        Emit(Op::Copy, dest, yes, 0, 0, 0, expr.line);
        uint16_t jumpEnd = Emit(Op::Jump, 0, 0, 0, 0, 0, expr.line);
        PatchConditionJump(jumpFalse, Here());
        uint16_t no = CompileExpr(*expr.kids[2]);
        Emit(Op::Copy, dest, no, 0, 0, 0, expr.line);
        _code[jumpEnd].a = Here();
        return dest;
    }

    uint16_t CompileConditionJumpFalse(const Expr& expr, int line) {
        if (expr.kind == Expr::Kind::Binary &&
            expr.binary != BinaryOp::And && expr.binary != BinaryOp::Or) {
            const Op op = BinaryOp(expr.binary);
            if (op == Op::CmpEq || op == Op::CmpNe || op == Op::CmpLt ||
                op == Op::CmpLe || op == Op::CmpGt || op == Op::CmpGe) {
                const uint16_t lhs = CompileExpr(*expr.kids[0]);
                const Expr& right = *expr.kids[1];
                if (right.kind == Expr::Kind::Int &&
                    right.intValue >= -32768 && right.intValue <= 32767) {
                    return Emit(Op::JumpIfCmpImmFalse, lhs, 0,
                                static_cast<uint16_t>(op),
                                static_cast<uint16_t>(static_cast<int16_t>(right.intValue)),
                                0, line);
                }
                const uint16_t rhs = CompileExpr(right);
                return Emit(Op::JumpIfCmpFalse, lhs, rhs, static_cast<uint16_t>(op), 0, 0, line);
            }
        }
        const uint16_t cond = CompileExpr(expr);
        return Emit(Op::JumpIfFalse, cond, 0, 0, 0, 0, line);
    }

    void PatchConditionJump(uint16_t jump, uint16_t target) {
        if (_code[jump].op == Op::JumpIfCmpFalse || _code[jump].op == Op::JumpIfCmpImmFalse) {
            _code[jump].e = target;
        }
        else _code[jump].b = target;
    }

    uint16_t CompileCast(const Expr& expr) {
        uint16_t value = CompileExpr(*expr.kids[0]);
        uint16_t dest = AllocTemp();
        uint16_t kind = 2;
        if (expr.text == "int") kind = 0;
        else if (expr.text == "float") kind = 1;
        uint16_t format = expr.format.empty() ? static_cast<uint16_t>(65535) : AddConst(expr.format);
        Emit(Op::Cast, dest, value, kind, format, 0, expr.line);
        return dest;
    }

    uint16_t CompileAddr(const Expr& expr) {
        const Expr& target = *expr.kids[0];
        if (target.kind == Expr::Kind::Index) {
            uint16_t object = CompileExpr(*target.kids[0]);
            uint16_t index = CompileExpr(*target.kids[1]);
            uint16_t dest = AllocTemp();
            Emit(Op::MakeRefIndex, dest, object, index, 0, 0, expr.line);
            return dest;
        }
        if (target.kind != Expr::Kind::Name) {
            Report(error::Code::COMPILATION_ADDRESS_EXPECTS_VARIABLE,
                "address expects a variable", expr.line);
            return 0;
        }
        uint16_t dest = AllocTemp();
        if (auto it = _locals.find(target.text); it != _locals.end()) {
            Emit(Op::MakeRefLocal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        if (_env.constants.contains(target.text)) {
            Report(error::Code::COMPILATION_ADDRESS_EXPECTS_VARIABLE,
                "constant '" + target.text + "' has no address", target.line);
            return 0;
        }
        if (auto it = _env.globals.find(target.text); it != _env.globals.end()) {
            Emit(Op::MakeRefGlobal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        Report(error::Code::COMPILATION_UNKNOWN_VARIABLE,
            "unknown variable '" + target.text + "'", target.line);
        return 0;
    }

    uint16_t CompileDeref(const Expr& expr) {
        uint16_t ref = CompileExpr(*expr.kids[0]);
        uint16_t dest = AllocTemp();
        Emit(Op::LoadRef, dest, ref, 0, 0, 0, expr.line);
        return dest;
    }

    void CompileStmt(const Stmt& stmt) {
        switch (stmt.kind) {
        case Stmt::Kind::Var:
            if (stmt.expr) {
                uint16_t value = CompileExpr(*stmt.expr);
                Emit(Op::Move, _locals.at(stmt.name), value, 0, 0, 0, stmt.line);
            }
            break;
        case Stmt::Kind::Expr:
            if (stmt.expr) CompileExpr(*stmt.expr);
            break;
        case Stmt::Kind::Block:
            CompileBody(stmt.body);
            break;
        case Stmt::Kind::Stop:
            Emit(Op::Stop, 0, 0, 0, 0, 0, stmt.line);
            break;
        case Stmt::Kind::Label:
            CompileLabel(stmt);
            break;
        case Stmt::Kind::Goto:
            CompileGoto(stmt);
            break;
        case Stmt::Kind::Return: {
            uint16_t value = stmt.expr ? CompileExpr(*stmt.expr) : [&] {
                uint16_t dest = AllocTemp();
                Emit(Op::LoadConst, dest, AddConst(std::monostate{}), 0, 0, 0, stmt.line);
                return dest;
            }();
            Emit(Op::Return, value, 0, 0, 0, 0, stmt.line);
            break;
        }
        case Stmt::Kind::Break: {
            if (_loops.empty()) {
                Report(error::Code::COMPILATION_BREAK_OUTSIDE_LOOP, "break outside of a loop", stmt.line);
                break;
            }
            _loops.back().breaks.push_back(Emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line));
            break;
        }
        case Stmt::Kind::Continue: {
            if (_loops.empty()) {
                Report(error::Code::COMPILATION_CONTINUE_OUTSIDE_LOOP, "continue outside of a loop", stmt.line);
                break;
            }
            _loops.back().continues.push_back(Emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line));
            break;
        }
        case Stmt::Kind::If:
            CompileIf(stmt);
            break;
        case Stmt::Kind::While:
            CompileWhile(stmt);
            break;
        case Stmt::Kind::Do:
            CompileDo(stmt);
            break;
        case Stmt::Kind::For:
            CompileFor(stmt);
            break;
        case Stmt::Kind::ForC:
            CompileForC(stmt);
            break;
        }
    }

    // Временные регистры переиспользуются между операторами. В них null
    // пишет Copy, поэтому перед оператором не нужно затирать все регистры
    // отдельным LoadConst: это был почти весь байткод горячего цикла.
    void CompileBody(const std::vector<std::unique_ptr<Stmt>>& body) {
        uint16_t base = _watermark;
        for (const auto& stmt : body) {
            _watermark = base;
            CompileStmt(*stmt);
            if (_failed) break;
        }
        _watermark = base;
    }

    void CompileLabel(const Stmt& stmt) {
        if (_labels.contains(stmt.name)) {
            Report(error::Code::COMPILATION_DUPLICATE_LABEL,
                "duplicate label '" + stmt.name + "'", stmt.line);
            return;
        }
        _labels.emplace(stmt.name, Here());
        for (uint16_t jump : _pendingGotos[stmt.name]) {
            _code[jump].a = Here();
        }
        _pendingGotos.erase(stmt.name);
    }

    void CompileGoto(const Stmt& stmt) {
        uint16_t jump = Emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line);
        if (auto it = _labels.find(stmt.name); it != _labels.end()) {
            _code[jump].a = it->second;
        } else {
            _pendingGotos[stmt.name].push_back(jump);
        }
    }

    void FinishLabels() {
        for (const auto& [name, jumps] : _pendingGotos) {
            if (!jumps.empty()) {
                Report(error::Code::COMPILATION_UNKNOWN_LABEL,
                    "unknown label '" + name + "'", _function.line);
            }
        }
    }

    void CompileIf(const Stmt& stmt) {
        const uint16_t jumpFalse = CompileConditionJumpFalse(*stmt.expr, stmt.line);
        CompileBody(stmt.body);
        if (stmt.hasElse) {
            uint16_t jumpEnd = Emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line);
            PatchConditionJump(jumpFalse, Here());
            CompileBody(stmt.elseBody);
            _code[jumpEnd].a = Here();
        } else {
            PatchConditionJump(jumpFalse, Here());
        }
    }

    void CompileWhile(const Stmt& stmt) {
        uint16_t start = Here();
        _loops.push_back(Loop{});
        const uint16_t jumpFalse = CompileConditionJumpFalse(*stmt.expr, stmt.line);
        CompileBody(stmt.body);
        uint16_t step = Here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = step;
        }
        Emit(Op::Jump, start, 0, 0, 0, 0, stmt.line);
        uint16_t end = Here();
        PatchConditionJump(jumpFalse, end);
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = end;
        }
        _loops.pop_back();
    }

    void CompileDo(const Stmt& stmt) {
        uint16_t start = Here();
        _loops.push_back(Loop{});
        CompileBody(stmt.body);
        uint16_t condIp = Here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = condIp;
        }
        uint16_t cond = CompileExpr(*stmt.expr);
        Emit(Op::JumpIfTrue, cond, start, 0, 0, 0, stmt.line);
        uint16_t end = Here();
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = end;
        }
        _loops.pop_back();
    }

    // Границы включительные, шаг всегда +1. Конец диапазона копируется в регистр
    // один раз: если справа локал, цикл не должен разъехаться, когда этот локал меняют внутри.
    void CompileFor(const Stmt& stmt) {
        const auto local = _locals.find(stmt.name);
        const auto global = _env.globals.find(stmt.name);
        if (local == _locals.end() && _env.constants.contains(stmt.name)) {
            Report(error::Code::COMPILATION_INVALID_ASSIGNMENT_TARGET,
                "cannot use constant '" + stmt.name + "' as a loop variable", stmt.line);
            return;
        }
        if (local == _locals.end() && global == _env.globals.end()) {
            Report(error::Code::COMPILATION_UNKNOWN_VARIABLE,
                "unknown variable '" + stmt.name + "'", stmt.line);
            return;
        }
        const uint16_t index = local != _locals.end() ? local->second : global->second;
        const bool isGlobal = local == _locals.end();
        uint16_t start = CompileExpr(*stmt.expr);
        if (isGlobal) Emit(Op::StoreGlobal, index, start, 0, 0, 0, stmt.line);
        else Emit(Op::Move, index, start, 0, 0, 0, stmt.line);
        uint16_t endSource = CompileExpr(*stmt.expr2);
        uint16_t end = AllocTemp();
        Emit(Op::Move, end, endSource, 0, 0, 0, stmt.line);
        uint16_t one = 0;
        if (isGlobal) {
            one = AllocTemp();
            Emit(Op::LoadConst, one, AddConst(int{1}), 0, 0, 0, stmt.line);
        }

        _loops.push_back(Loop{});
        uint16_t condIp = Here();
        uint16_t current = index;
        if (isGlobal) {
            current = AllocTemp();
            Emit(Op::LoadGlobal, current, index, 0, 0, 0, stmt.line);
        }
        uint16_t jumpFalse = Emit(Op::JumpIfNotLe, current, end, 0, 0, 0, stmt.line);
        const uint16_t bodyIp = Here();
        CompileBody(stmt.body);
        uint16_t increment = Here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = increment;
        }
        if (isGlobal) {
            uint16_t loaded = AllocTemp();
            Emit(Op::LoadGlobal, loaded, index, 0, 0, 0, stmt.line);
            Emit(Op::Add, loaded, loaded, one, 0, 0, stmt.line);
            Emit(Op::StoreGlobal, index, loaded, 0, 0, 0, stmt.line);
            Emit(Op::Jump, condIp, 0, 0, 0, 0, stmt.line);
        } else {
            Emit(Op::IncJumpLe, index, end, bodyIp, 0, 0, stmt.line);
        }
        uint16_t endIp = Here();
        _code[jumpFalse].c = endIp;
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = endIp;
        }
        _loops.pop_back();
    }

    void CompileForC(const Stmt& stmt) {
        if (stmt.expr) {
            CompileExpr(*stmt.expr);
        }
        uint16_t start = Here();
        uint16_t jumpFalse = 0;
        bool hasCond = stmt.expr2 != nullptr;
        if (hasCond) {
            jumpFalse = CompileConditionJumpFalse(*stmt.expr2, stmt.line);
        }
        _loops.push_back(Loop{});
        CompileBody(stmt.body);
        uint16_t step = Here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = step;
        }
        if (stmt.expr3) {
            CompileExpr(*stmt.expr3);
        }
        Emit(Op::Jump, start, 0, 0, 0, 0, stmt.line);
        uint16_t end = Here();
        if (hasCond) {
            PatchConditionJump(jumpFalse, end);
        }
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = end;
        }
        _loops.pop_back();
    }

    Op BinaryOp(BinaryOp op) {
        switch (op) {
        case BinaryOp::Add: return Op::Add;
        case BinaryOp::Sub: return Op::Sub;
        case BinaryOp::Mul: return Op::Mul;
        case BinaryOp::Div: return Op::Div;
        case BinaryOp::Mod: return Op::Mod;
        case BinaryOp::Eq: return Op::CmpEq;
        case BinaryOp::Ne: return Op::CmpNe;
        case BinaryOp::Lt: return Op::CmpLt;
        case BinaryOp::Le: return Op::CmpLe;
        case BinaryOp::Gt: return Op::CmpGt;
        case BinaryOp::Ge: return Op::CmpGe;
        case BinaryOp::BitAnd: return Op::BitAnd;
        case BinaryOp::BitOr: return Op::BitOr;
        case BinaryOp::BitXor: return Op::BitXor;
        case BinaryOp::Shl: return Op::Shl;
        case BinaryOp::Shr: return Op::Shr;
        case BinaryOp::And:
        case BinaryOp::Or:
            break;
        }
        Report(error::Code::COMPILATION_INTERNAL_OPERATOR, "internal operator", _function.line);
        return Op::Add;
    }
};

}  // анонимное пространство имён

error::Result<std::vector<Chunk>> compile(const Program& program, const CompileEnv& env, std::string_view source) {
    error::Result<std::vector<Chunk>> result;
    std::unordered_map<std::string, uint16_t> functions;
    for (const Function& function : program.functions) {
        if (functions.contains(function.name)) {
            if (!result.error) {
                error::Location location;
                location.source = source;
                location.function = function.name;
                location.line = function.line;
                result.error.emplace(error::Code::COMPILATION_DUPLICATE_FUNCTION,
                    "duplicate function '" + function.name + "'", std::move(location));
            }
            break;
        }
        functions.emplace(function.name, static_cast<uint16_t>(functions.size()));
    }

    if (result.error) return result;
    result.reserve(program.functions.size());
    for (const Function& function : program.functions) {
        if (result.error) break;
        if (auto chunk = FunctionCompiler(function, functions, env, source, result.error).Run()) {
            result.push_back(std::move(*chunk));
        }
    }
    if (result.error) result.clear();
    return result;
}

}  // пространство имён sme
