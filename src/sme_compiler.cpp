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

    std::optional<Chunk> run() {
        for (const std::string& param : _function.params) {
            if (_error) break;
            declare(param, _function.line);
        }
        for (const auto& stmt : _function.body) {
            if (_error) break;
            collect(*stmt);
        }
        if (_failed) return std::nullopt;
        _watermark = static_cast<uint16_t>(_locals.size());
        _regCount = _watermark;
        compileBody(_function.body);
        if (_failed) return std::nullopt;
        finishLabels();
        if (_failed) return std::nullopt;
        uint16_t result = allocTemp();
        emit(Op::LoadConst, result, addConst(std::monostate{}), 0, 0, 0, _function.line);
        emit(Op::Return, result, 0, 0, 0, 0, _function.line);

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

    void report(error::Code code, std::string message, int line = 0) {
        _failed = true;
        if (_error) return;
        error::Location location;
        location.source = _source;
        location.function = _function.name;
        location.line = line;
        _error.emplace(code, std::move(message), std::move(location));
    }

    void declare(const std::string& name, int line) {
        if (_locals.contains(name)) {
            report(error::Code::COMPILATION_DUPLICATE_VARIABLE, "duplicate variable '" + name + "'", line);
            return;
        }
        if (_locals.size() >= 65535) {
            report(error::Code::COMPILATION_TOO_MANY_LOCAL_VARIABLES, "too many local variables", line);
            return;
        }
        _locals.emplace(name, static_cast<uint16_t>(_locals.size()));
    }

    // Присваивание в неизвестное имя молча заводит локал. Опечатка в левой
    // части поэтому не ошибка, а новая переменная. Глобал с таким именем не перекрывается.
    void note(const std::string& name, int line) {
        if (_locals.contains(name) || _env.globals.contains(name)) {
            return;
        }
        declare(name, line);
    }

    void collectExpr(const Expr& expr) {
        if (_error) return;
        if ((expr.kind == Expr::Kind::Assign || expr.kind == Expr::Kind::Modify) && !expr.kids.empty() &&
            expr.kids[0]->kind == Expr::Kind::Name) {
            note(expr.kids[0]->text, expr.kids[0]->line);
        }
        if ((expr.kind == Expr::Kind::PostInc || expr.kind == Expr::Kind::PostDec) && !expr.kids.empty() &&
            expr.kids[0]->kind == Expr::Kind::Name) {
            note(expr.kids[0]->text, expr.line);
        }
        for (const auto& kid : expr.kids) {
            if (kid) {
                collectExpr(*kid);
            }
        }
    }

    // Второй var с тем же именем — ошибка. Параметр и var с тем же именем тоже.
    // Присваивание в новое имя локал создаёт, var для этого не обязателен.
    void collect(const Stmt& stmt) {
        if (_error) return;
        if (stmt.kind == Stmt::Kind::Var) {
            declare(stmt.name, stmt.line);
        }
        if (stmt.kind == Stmt::Kind::For && !_locals.contains(stmt.name) && !_env.globals.contains(stmt.name)) {
            declare(stmt.name, stmt.line);
        }
        if (stmt.expr) collectExpr(*stmt.expr);
        if (stmt.expr2) collectExpr(*stmt.expr2);
        if (stmt.expr3) collectExpr(*stmt.expr3);
        for (const auto& child : stmt.body) collect(*child);
        for (const auto& child : stmt.elseBody) collect(*child);
    }

    uint16_t allocTemp() {
        if (_watermark == 65535) {
            report(error::Code::COMPILATION_TOO_MANY_REGISTERS, "too many registers", _function.line);
            return 0;
        }
        uint16_t reg = _watermark++;
        if (_watermark > _regCount) {
            _regCount = _watermark;
        }
        return reg;
    }

    uint16_t addConst(Constant constant) {
        if (_constants.size() >= 65535) {
            report(error::Code::COMPILATION_TOO_MANY_CONSTANTS, "too many constants", _function.line);
            return 0;
        }
        _constants.push_back(std::move(constant));
        return static_cast<uint16_t>(_constants.size() - 1);
    }

    uint16_t emit(Op op, uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, int line) {
        if (_code.size() >= 65535) {
            report(error::Code::COMPILATION_FUNCTION_TOO_LARGE, "function is too large", line);
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

    uint16_t here() const { return static_cast<uint16_t>(_code.size()); }

    uint16_t compileExpr(const Expr& expr) {
        switch (expr.kind) {
        case Expr::Kind::Int: {
            uint16_t dest = allocTemp();
            emit(Op::LoadConst, dest, addConst(expr.intValue), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Float: {
            uint16_t dest = allocTemp();
            emit(Op::LoadConst, dest, addConst(expr.floatValue), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Bool: {
            uint16_t dest = allocTemp();
            emit(Op::LoadConst, dest, addConst(int{expr.boolValue ? 1 : 0}), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Null: {
            uint16_t dest = allocTemp();
            if (expr.intValue == 1) emit(Op::LoadConst, dest, addConst(nullptr), 0, 0, 0, expr.line);
            else emit(Op::LoadConst, dest, addConst(std::monostate{}), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::String: {
            uint16_t dest = allocTemp();
            emit(Op::LoadConst, dest, addConst(expr.text), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Name:
            return compileName(expr);
        case Expr::Kind::Unary:
            return compileUnary(expr);
        case Expr::Kind::Binary:
            return compileBinary(expr);
        case Expr::Kind::Call:
            return compileCall(expr);
        case Expr::Kind::Index:
        case Expr::Kind::CharIndex:
            return compileIndex(expr);
        case Expr::Kind::Member:
            return compileMember(expr);
        case Expr::Kind::List:
            return compileList(expr);
        case Expr::Kind::Assign:
            return compileAssign(expr);
        case Expr::Kind::Modify:
            return compileModify(expr);
        case Expr::Kind::Ternary:
            return compileTernary(expr);
        case Expr::Kind::Cast:
            return compileCast(expr);
        case Expr::Kind::Addr:
            return compileAddr(expr);
        case Expr::Kind::Deref:
            return compileDeref(expr);
        case Expr::Kind::PostInc:
            return compileUpdate(expr, 1);
        case Expr::Kind::PostDec:
            return compileUpdate(expr, -1);
        case Expr::Kind::NewObj: {
            uint16_t dest = allocTemp();
            emit(Op::NewObj, dest, addConst(expr.text), 0, 0, 0, expr.line);
            return dest;
        }
        case Expr::Kind::Query:
            return compileQuery(expr);
        }
        report(error::Code::COMPILATION_INTERNAL_EXPRESSION, "internal expression", expr.line);
        return 0;
    }

    // ?имя. Неизвестное имя — 0. Иначе код типа значения. Для непереданного
    // параметра слот null, код 0, поэтому if (!?x) x = значение по умолчанию работает.
    uint16_t compileQuery(const Expr& expr) {
        uint16_t dest = allocTemp();
        if (!_locals.contains(expr.text) && !_env.globals.contains(expr.text)) {
            emit(Op::LoadConst, dest, addConst(int{0}), 0, 0, 0, expr.line);
            return dest;
        }
        Expr name;
        name.kind = Expr::Kind::Name;
        name.text = expr.text;
        name.line = expr.line;
        uint16_t value = compileName(name);
        emit(Op::Typeof, dest, value, 0, 0, 0, expr.line);
        return dest;
    }

    uint16_t compileName(const Expr& expr) {
        if (auto it = _locals.find(expr.text); it != _locals.end()) {
            return it->second;
        }
        if (auto it = _env.globals.find(expr.text); it != _env.globals.end()) {
            uint16_t dest = allocTemp();
            emit(Op::LoadGlobal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        if (_functions.contains(expr.text) || _env.natives.contains(expr.text)) {
            report(error::Code::COMPILATION_FUNCTION_MUST_BE_CALLED,
                "function '" + expr.text + "' must be called", expr.line);
            return 0;
        }
        uint16_t dest = allocTemp();
        emit(Op::LoadConst, dest, addConst(std::monostate{}), 0, 0, 0, expr.line);
        return dest;
    }

    uint16_t compileUnary(const Expr& expr) {
        uint16_t value = compileExpr(*expr.kids[0]);
        uint16_t dest = allocTemp();
        Op op = Op::Not;
        if (expr.unary == UnaryOp::Neg) op = Op::Neg;
        else if (expr.unary == UnaryOp::BitNot) op = Op::BitNot;
        emit(op, dest, value, 0, 0, 0, expr.line);
        return dest;
    }

    uint16_t compileBinary(const Expr& expr) {
        if (expr.binary == BinaryOp::And || expr.binary == BinaryOp::Or) {
            return compileLogic(expr);
        }
        const Expr& right = *expr.kids[1];
        if (right.kind == Expr::Kind::Int && right.intValue >= -32768 && right.intValue <= 32767) {
            uint16_t lhs = compileExpr(*expr.kids[0]);
            uint16_t dest = allocTemp();
            emit(Op::BinaryImm, dest, lhs, static_cast<uint16_t>(binaryOp(expr.binary)), static_cast<uint16_t>(static_cast<int16_t>(right.intValue)), 0, expr.line);
            return dest;
        }
        uint16_t lhs = compileExpr(*expr.kids[0]);
        uint16_t rhs = compileExpr(*expr.kids[1]);
        uint16_t dest = allocTemp();
        emit(binaryOp(expr.binary), dest, lhs, rhs, 0, 0, expr.line);
        return dest;
    }

    // Оба операнда вычисляются всегда. Короткой схемы &&/|| нет: это зафиксировано тестами.
    uint16_t compileLogic(const Expr& expr) {
        uint16_t lhs = compileExpr(*expr.kids[0]);
        uint16_t rhs = compileExpr(*expr.kids[1]);
        uint16_t dest = allocTemp();
        Op op = expr.binary == BinaryOp::And ? Op::LogicAnd : Op::LogicOr;
        emit(op, dest, lhs, rhs, 0, 0, expr.line);
        return dest;
    }

    // var(индекс) обрабатывается до копирования аргументов: RefGlobal читает
    // регистр с индексом глобала, а не слот вызова.
    uint16_t compileCall(const Expr& expr) {
        const Expr& callee = *expr.kids[0];
        std::vector<uint16_t> args;
        for (size_t i = 1; i < expr.kids.size(); ++i) {
            args.push_back(compileExpr(*expr.kids[i]));
        }
        if (callee.kind == Expr::Kind::Name && callee.text == "var" && args.size() == 1) {
            uint16_t dest = allocTemp();
            emit(Op::RefGlobal, dest, args[0], 0, 0, 0, expr.line);
            return dest;
        }
        uint16_t base = _watermark;
        for (uint16_t arg : args) {
            uint16_t slot = allocTemp();
            emit(Op::Copy, slot, arg, 0, 0, 0, expr.line);
        }
        uint16_t dest = allocTemp();
        uint16_t argc = static_cast<uint16_t>(args.size());
        if (callee.kind == Expr::Kind::Name) {
            if (auto function = _functions.find(callee.text); function != _functions.end()) {
                emit(Op::Call, dest, function->second, base, argc, 0, expr.line);
                return dest;
            }
            if (auto native = _env.natives.find(callee.text); native != _env.natives.end()) {
                emit(Op::CallNative, dest, native->second, base, argc, 0, expr.line);
                return dest;
            }
            emit(Op::CallName, dest, addConst(callee.text), base, argc, 0, expr.line);
            return dest;
        }
        if (callee.kind == Expr::Kind::Member) {
            uint16_t object = compileExpr(*callee.kids[0]);
            uint16_t name = addConst(callee.text);
            emit(Op::CallMethod, dest, object, name, base, argc, expr.line);
            return dest;
        }
        report(error::Code::COMPILATION_EXPRESSION_NOT_CALLABLE, "expression is not a function", expr.line);
        return 0;
    }

    uint16_t compileIndex(const Expr& expr) {
        uint16_t object = compileExpr(*expr.kids[0]);
        uint16_t index = compileExpr(*expr.kids[1]);
        uint16_t dest = allocTemp();
        emit(expr.kind == Expr::Kind::CharIndex ? Op::GetChar : Op::GetIndex, dest, object, index, 0, 0, expr.line);
        return dest;
    }

    uint16_t compileMember(const Expr& expr) {
        uint16_t object = compileExpr(*expr.kids[0]);
        uint16_t dest = allocTemp();
        emit(Op::GetProp, dest, object, addConst(expr.text), 0, 0, expr.line);
        return dest;
    }

    uint16_t compileList(const Expr& expr) {
        std::vector<uint16_t> items;
        items.reserve(expr.kids.size());
        for (const auto& kid : expr.kids) {
            items.push_back(compileExpr(*kid));
        }
        uint16_t base = _watermark;
        for (uint16_t item : items) {
            uint16_t slot = allocTemp();
            emit(Op::Copy, slot, item, 0, 0, 0, expr.line);
        }
        uint16_t dest = allocTemp();
        emit(Op::MakeTable, dest, base, static_cast<uint16_t>(items.size()), 0, 0, expr.line);
        return dest;
    }

    uint16_t compileAssign(const Expr& expr) {
        const Expr& target = *expr.kids[0];
        const Expr& valueExpr = *expr.kids[1];
        if (target.kind == Expr::Kind::Name) {
            const auto local = _locals.find(target.text);
            if (local != _locals.end()) {
                const uint16_t dest = local->second;
                if (valueExpr.kind == Expr::Kind::Binary) {
                    const uint16_t lhs = compileExpr(*valueExpr.kids[0]);
                    const Expr& right = *valueExpr.kids[1];
                    if (valueExpr.binary == BinaryOp::And || valueExpr.binary == BinaryOp::Or) {
                        const uint16_t rhs = compileExpr(right);
                        const Op op = valueExpr.binary == BinaryOp::And ? Op::LogicAnd : Op::LogicOr;
                        emit(op, dest, lhs, rhs, 0, 0, expr.line);
                    } else if (right.kind == Expr::Kind::Int &&
                        right.intValue >= -32768 && right.intValue <= 32767) {
                        emit(Op::BinaryImm, dest, lhs,
                             static_cast<uint16_t>(binaryOp(valueExpr.binary)),
                             static_cast<uint16_t>(static_cast<int16_t>(right.intValue)), 0, expr.line);
                    } else {
                        const uint16_t rhs = compileExpr(right);
                        emit(binaryOp(valueExpr.binary), dest, lhs, rhs, 0, 0, expr.line);
                    }
                    return dest;
                }
                if (valueExpr.kind == Expr::Kind::Unary) {
                    const uint16_t value = compileExpr(*valueExpr.kids[0]);
                    Op op = Op::Not;
                    if (valueExpr.unary == UnaryOp::Neg) op = Op::Neg;
                    else if (valueExpr.unary == UnaryOp::BitNot) op = Op::BitNot;
                    emit(op, dest, value, 0, 0, 0, expr.line);
                    return dest;
                }
                if (valueExpr.kind == Expr::Kind::Cast) {
                    const uint16_t value = compileExpr(*valueExpr.kids[0]);
                    uint16_t kind = 2;
                    if (valueExpr.text == "int") kind = 0;
                    else if (valueExpr.text == "float") kind = 1;
                    const uint16_t format = valueExpr.format.empty()
                        ? static_cast<uint16_t>(65535)
                        : addConst(valueExpr.format);
                    emit(Op::Cast, dest, value, kind, format, 0, expr.line);
                    return dest;
                }
            }
        }
        uint16_t value = compileExpr(*expr.kids[1]);
        store(target, value, expr.line);
        return value;
    }

    struct Place {
        enum class Kind { Local, Global, Index, Char, Prop, Ref } kind = Kind::Local;
        uint16_t primary = 0;
        uint16_t secondary = 0;
    };

    // Адрес цели присваивания вычисляется один раз. Иначе obj.prop += 1
    // дважды вызвал бы выражение объекта, и чтение с записью могли разъехаться.
    Place placeOf(const Expr& target, const char* invalid) {
        if (target.kind == Expr::Kind::Name) {
            if (auto it = _locals.find(target.text); it != _locals.end()) {
                return Place{Place::Kind::Local, it->second, 0};
            }
            if (auto it = _env.globals.find(target.text); it != _env.globals.end()) {
                return Place{Place::Kind::Global, it->second, 0};
            }
            report(error::Code::COMPILATION_UNKNOWN_VARIABLE,
                "unknown variable '" + target.text + "'", target.line);
            return {};
        }
        if (target.kind == Expr::Kind::Index || target.kind == Expr::Kind::CharIndex) {
            Place place;
            place.kind = target.kind == Expr::Kind::CharIndex ? Place::Kind::Char : Place::Kind::Index;
            place.primary = compileExpr(*target.kids[0]);
            place.secondary = compileExpr(*target.kids[1]);
            return place;
        }
        if (target.kind == Expr::Kind::Member) {
            return Place{Place::Kind::Prop, compileExpr(*target.kids[0]), addConst(target.text)};
        }
        if (target.kind == Expr::Kind::Deref) {
            return Place{Place::Kind::Ref, compileExpr(*target.kids[0]), 0};
        }
        if (target.kind == Expr::Kind::Call) {
            return Place{Place::Kind::Ref, compileExpr(target), 0};
        }
        report(error::Code::COMPILATION_INVALID_ASSIGNMENT_TARGET, invalid, target.line);
        return {};
    }

    uint16_t loadPlace(const Place& place, int line) {
        switch (place.kind) {
        case Place::Kind::Local:
            return place.primary;
        case Place::Kind::Global: {
            uint16_t dest = allocTemp();
            emit(Op::LoadGlobal, dest, place.primary, 0, 0, 0, line);
            return dest;
        }
        case Place::Kind::Index:
        case Place::Kind::Char: {
            uint16_t dest = allocTemp();
            emit(place.kind == Place::Kind::Char ? Op::GetChar : Op::GetIndex, dest, place.primary, place.secondary, 0, 0, line);
            return dest;
        }
        case Place::Kind::Prop: {
            uint16_t dest = allocTemp();
            emit(Op::GetProp, dest, place.primary, place.secondary, 0, 0, line);
            return dest;
        }
        case Place::Kind::Ref: {
            uint16_t dest = allocTemp();
            emit(Op::LoadRef, dest, place.primary, 0, 0, 0, line);
            return dest;
        }
        }
        report(error::Code::COMPILATION_INTERNAL_ASSIGNMENT, "internal assignment", line);
        return 0;
    }

    void storePlace(const Place& place, uint16_t value, int line) {
        switch (place.kind) {
        case Place::Kind::Local:
            emit(Op::Move, place.primary, value, 0, 0, 0, line);
            return;
        case Place::Kind::Global:
            emit(Op::StoreGlobal, place.primary, value, 0, 0, 0, line);
            return;
        case Place::Kind::Index:
            emit(Op::SetIndex, place.primary, place.secondary, value, 0, 0, line);
            return;
        case Place::Kind::Char:
            emit(Op::SetChar, place.primary, place.secondary, value, 0, 0, line);
            return;
        case Place::Kind::Prop:
            emit(Op::SetProp, place.primary, place.secondary, value, 0, 0, line);
            return;
        case Place::Kind::Ref:
            emit(Op::StoreRef, place.primary, value, 0, 0, 0, line);
            return;
        }
        report(error::Code::COMPILATION_INTERNAL_ASSIGNMENT, "internal assignment", line);
    }

    void store(const Expr& target, uint16_t value, int line) {
        storePlace(placeOf(target, "invalid assignment target"), value, line);
    }

    uint16_t compileModify(const Expr& expr) {
        Place place = placeOf(*expr.kids[0], "invalid assignment target");
        const Expr& right = *expr.kids[1];
        if (place.kind == Place::Kind::Local && right.kind == Expr::Kind::Int &&
            right.intValue >= -32768 && right.intValue <= 32767) {
            emit(Op::BinaryImm, place.primary, place.primary,
                 static_cast<uint16_t>(binaryOp(expr.binary)),
                 static_cast<uint16_t>(static_cast<int16_t>(right.intValue)), 0, expr.line);
            return place.primary;
        }
        uint16_t current = loadPlace(place, expr.line);
        uint16_t rhs = compileExpr(right);
        uint16_t dest = allocTemp();
        emit(binaryOp(expr.binary), dest, current, rhs, 0, 0, expr.line);
        storePlace(place, dest, expr.line);
        return dest;
    }

    uint16_t compileUpdate(const Expr& expr, int delta) {
        Place place = placeOf(*expr.kids[0], "invalid update target");
        if (place.kind == Place::Kind::Local) {
            uint16_t saved = place.primary;
            if (!expr.prefix) {
                saved = allocTemp();
                emit(Op::Copy, saved, place.primary, 0, 0, 0, expr.line);
            }
            emit(Op::BinaryImm, place.primary, place.primary,
                 static_cast<uint16_t>(Op::Add), static_cast<uint16_t>(delta), 0, expr.line);
            return expr.prefix ? place.primary : saved;
        }
        uint16_t current = loadPlace(place, expr.line);
        uint16_t saved = current;
        uint16_t step = allocTemp();
        emit(Op::LoadConst, step, addConst(delta), 0, 0, 0, expr.line);
        uint16_t next = allocTemp();
        emit(Op::Add, next, current, step, 0, 0, expr.line);
        storePlace(place, next, expr.line);
        return expr.prefix ? next : saved;
    }

    uint16_t compileTernary(const Expr& expr) {
        uint16_t dest = allocTemp();
        const uint16_t jumpFalse = compileConditionJumpFalse(*expr.kids[0], expr.line);
        uint16_t yes = compileExpr(*expr.kids[1]);
        emit(Op::Copy, dest, yes, 0, 0, 0, expr.line);
        uint16_t jumpEnd = emit(Op::Jump, 0, 0, 0, 0, 0, expr.line);
        patchConditionJump(jumpFalse, here());
        uint16_t no = compileExpr(*expr.kids[2]);
        emit(Op::Copy, dest, no, 0, 0, 0, expr.line);
        _code[jumpEnd].a = here();
        return dest;
    }

    uint16_t compileConditionJumpFalse(const Expr& expr, int line) {
        if (expr.kind == Expr::Kind::Binary &&
            expr.binary != BinaryOp::And && expr.binary != BinaryOp::Or) {
            const Op op = binaryOp(expr.binary);
            if (op == Op::CmpEq || op == Op::CmpNe || op == Op::CmpLt ||
                op == Op::CmpLe || op == Op::CmpGt || op == Op::CmpGe) {
                const uint16_t lhs = compileExpr(*expr.kids[0]);
                const Expr& right = *expr.kids[1];
                if (right.kind == Expr::Kind::Int &&
                    right.intValue >= -32768 && right.intValue <= 32767) {
                    return emit(Op::JumpIfCmpImmFalse, lhs, 0,
                                static_cast<uint16_t>(op),
                                static_cast<uint16_t>(static_cast<int16_t>(right.intValue)),
                                0, line);
                }
                const uint16_t rhs = compileExpr(right);
                return emit(Op::JumpIfCmpFalse, lhs, rhs, static_cast<uint16_t>(op), 0, 0, line);
            }
        }
        const uint16_t cond = compileExpr(expr);
        return emit(Op::JumpIfFalse, cond, 0, 0, 0, 0, line);
    }

    void patchConditionJump(uint16_t jump, uint16_t target) {
        if (_code[jump].op == Op::JumpIfCmpFalse || _code[jump].op == Op::JumpIfCmpImmFalse) {
            _code[jump].e = target;
        }
        else _code[jump].b = target;
    }

    uint16_t compileCast(const Expr& expr) {
        uint16_t value = compileExpr(*expr.kids[0]);
        uint16_t dest = allocTemp();
        uint16_t kind = 2;
        if (expr.text == "int") kind = 0;
        else if (expr.text == "float") kind = 1;
        uint16_t format = expr.format.empty() ? static_cast<uint16_t>(65535) : addConst(expr.format);
        emit(Op::Cast, dest, value, kind, format, 0, expr.line);
        return dest;
    }

    uint16_t compileAddr(const Expr& expr) {
        const Expr& target = *expr.kids[0];
        if (target.kind == Expr::Kind::Index) {
            uint16_t object = compileExpr(*target.kids[0]);
            uint16_t index = compileExpr(*target.kids[1]);
            uint16_t dest = allocTemp();
            emit(Op::MakeRefIndex, dest, object, index, 0, 0, expr.line);
            return dest;
        }
        if (target.kind != Expr::Kind::Name) {
            report(error::Code::COMPILATION_ADDRESS_EXPECTS_VARIABLE,
                "address expects a variable", expr.line);
            return 0;
        }
        uint16_t dest = allocTemp();
        if (auto it = _locals.find(target.text); it != _locals.end()) {
            emit(Op::MakeRefLocal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        if (auto it = _env.globals.find(target.text); it != _env.globals.end()) {
            emit(Op::MakeRefGlobal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        report(error::Code::COMPILATION_UNKNOWN_VARIABLE,
            "unknown variable '" + target.text + "'", target.line);
        return 0;
    }

    uint16_t compileDeref(const Expr& expr) {
        uint16_t ref = compileExpr(*expr.kids[0]);
        uint16_t dest = allocTemp();
        emit(Op::LoadRef, dest, ref, 0, 0, 0, expr.line);
        return dest;
    }

    void compileStmt(const Stmt& stmt) {
        switch (stmt.kind) {
        case Stmt::Kind::Var:
            if (stmt.expr) {
                uint16_t value = compileExpr(*stmt.expr);
                emit(Op::Move, _locals.at(stmt.name), value, 0, 0, 0, stmt.line);
            }
            break;
        case Stmt::Kind::Expr:
            if (stmt.expr) compileExpr(*stmt.expr);
            break;
        case Stmt::Kind::Block:
            compileBody(stmt.body);
            break;
        case Stmt::Kind::Stop:
            emit(Op::Stop, 0, 0, 0, 0, 0, stmt.line);
            break;
        case Stmt::Kind::Label:
            compileLabel(stmt);
            break;
        case Stmt::Kind::Goto:
            compileGoto(stmt);
            break;
        case Stmt::Kind::Return: {
            uint16_t value = stmt.expr ? compileExpr(*stmt.expr) : [&] {
                uint16_t dest = allocTemp();
                emit(Op::LoadConst, dest, addConst(std::monostate{}), 0, 0, 0, stmt.line);
                return dest;
            }();
            emit(Op::Return, value, 0, 0, 0, 0, stmt.line);
            break;
        }
        case Stmt::Kind::Break: {
            if (_loops.empty()) {
                report(error::Code::COMPILATION_BREAK_OUTSIDE_LOOP, "break outside of a loop", stmt.line);
                break;
            }
            _loops.back().breaks.push_back(emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line));
            break;
        }
        case Stmt::Kind::Continue: {
            if (_loops.empty()) {
                report(error::Code::COMPILATION_CONTINUE_OUTSIDE_LOOP, "continue outside of a loop", stmt.line);
                break;
            }
            _loops.back().continues.push_back(emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line));
            break;
        }
        case Stmt::Kind::If:
            compileIf(stmt);
            break;
        case Stmt::Kind::While:
            compileWhile(stmt);
            break;
        case Stmt::Kind::Do:
            compileDo(stmt);
            break;
        case Stmt::Kind::For:
            compileFor(stmt);
            break;
        case Stmt::Kind::ForC:
            compileForC(stmt);
            break;
        }
    }

    // Временные регистры переиспользуются между операторами. В них null
    // пишет Copy, поэтому перед оператором не нужно затирать все регистры
    // отдельным LoadConst: это был почти весь байткод горячего цикла.
    void compileBody(const std::vector<std::unique_ptr<Stmt>>& body) {
        uint16_t base = _watermark;
        for (const auto& stmt : body) {
            _watermark = base;
            compileStmt(*stmt);
            if (_failed) break;
        }
        _watermark = base;
    }

    void compileLabel(const Stmt& stmt) {
        if (_labels.contains(stmt.name)) {
            report(error::Code::COMPILATION_DUPLICATE_LABEL,
                "duplicate label '" + stmt.name + "'", stmt.line);
            return;
        }
        _labels.emplace(stmt.name, here());
        for (uint16_t jump : _pendingGotos[stmt.name]) {
            _code[jump].a = here();
        }
        _pendingGotos.erase(stmt.name);
    }

    void compileGoto(const Stmt& stmt) {
        uint16_t jump = emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line);
        if (auto it = _labels.find(stmt.name); it != _labels.end()) {
            _code[jump].a = it->second;
        } else {
            _pendingGotos[stmt.name].push_back(jump);
        }
    }

    void finishLabels() {
        for (const auto& [name, jumps] : _pendingGotos) {
            if (!jumps.empty()) {
                report(error::Code::COMPILATION_UNKNOWN_LABEL,
                    "unknown label '" + name + "'", _function.line);
            }
        }
    }

    void compileIf(const Stmt& stmt) {
        const uint16_t jumpFalse = compileConditionJumpFalse(*stmt.expr, stmt.line);
        compileBody(stmt.body);
        if (stmt.hasElse) {
            uint16_t jumpEnd = emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line);
            patchConditionJump(jumpFalse, here());
            compileBody(stmt.elseBody);
            _code[jumpEnd].a = here();
        } else {
            patchConditionJump(jumpFalse, here());
        }
    }

    void compileWhile(const Stmt& stmt) {
        uint16_t start = here();
        _loops.push_back(Loop{});
        const uint16_t jumpFalse = compileConditionJumpFalse(*stmt.expr, stmt.line);
        compileBody(stmt.body);
        uint16_t step = here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = step;
        }
        emit(Op::Jump, start, 0, 0, 0, 0, stmt.line);
        uint16_t end = here();
        patchConditionJump(jumpFalse, end);
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = end;
        }
        _loops.pop_back();
    }

    void compileDo(const Stmt& stmt) {
        uint16_t start = here();
        _loops.push_back(Loop{});
        compileBody(stmt.body);
        uint16_t condIp = here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = condIp;
        }
        uint16_t cond = compileExpr(*stmt.expr);
        emit(Op::JumpIfTrue, cond, start, 0, 0, 0, stmt.line);
        uint16_t end = here();
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = end;
        }
        _loops.pop_back();
    }

    // Границы включительные, шаг всегда +1. Конец диапазона копируется в регистр
    // один раз: если справа локал, цикл не должен разъехаться, когда этот локал меняют внутри.
    void compileFor(const Stmt& stmt) {
        uint16_t index = _locals.contains(stmt.name) ? _locals.at(stmt.name) : _env.globals.at(stmt.name);
        bool global = !_locals.contains(stmt.name);
        uint16_t start = compileExpr(*stmt.expr);
        if (global) emit(Op::StoreGlobal, index, start, 0, 0, 0, stmt.line);
        else emit(Op::Move, index, start, 0, 0, 0, stmt.line);
        uint16_t endSource = compileExpr(*stmt.expr2);
        uint16_t end = allocTemp();
        emit(Op::Move, end, endSource, 0, 0, 0, stmt.line);
        uint16_t one = 0;
        if (global) {
            one = allocTemp();
            emit(Op::LoadConst, one, addConst(int{1}), 0, 0, 0, stmt.line);
        }

        _loops.push_back(Loop{});
        uint16_t condIp = here();
        uint16_t current = index;
        if (global) {
            current = allocTemp();
            emit(Op::LoadGlobal, current, index, 0, 0, 0, stmt.line);
        }
        uint16_t jumpFalse = emit(Op::JumpIfNotLe, current, end, 0, 0, 0, stmt.line);
        const uint16_t bodyIp = here();
        compileBody(stmt.body);
        uint16_t increment = here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = increment;
        }
        if (global) {
            uint16_t loaded = allocTemp();
            emit(Op::LoadGlobal, loaded, index, 0, 0, 0, stmt.line);
            emit(Op::Add, loaded, loaded, one, 0, 0, stmt.line);
            emit(Op::StoreGlobal, index, loaded, 0, 0, 0, stmt.line);
            emit(Op::Jump, condIp, 0, 0, 0, 0, stmt.line);
        } else {
            emit(Op::IncJumpLe, index, end, bodyIp, 0, 0, stmt.line);
        }
        uint16_t endIp = here();
        _code[jumpFalse].c = endIp;
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = endIp;
        }
        _loops.pop_back();
    }

    void compileForC(const Stmt& stmt) {
        if (stmt.expr) {
            compileExpr(*stmt.expr);
        }
        uint16_t start = here();
        uint16_t jumpFalse = 0;
        bool hasCond = stmt.expr2 != nullptr;
        if (hasCond) {
            jumpFalse = compileConditionJumpFalse(*stmt.expr2, stmt.line);
        }
        _loops.push_back(Loop{});
        compileBody(stmt.body);
        uint16_t step = here();
        for (uint16_t jump : _loops.back().continues) {
            _code[jump].a = step;
        }
        if (stmt.expr3) {
            compileExpr(*stmt.expr3);
        }
        emit(Op::Jump, start, 0, 0, 0, 0, stmt.line);
        uint16_t end = here();
        if (hasCond) {
            patchConditionJump(jumpFalse, end);
        }
        for (uint16_t jump : _loops.back().breaks) {
            _code[jump].a = end;
        }
        _loops.pop_back();
    }

    Op binaryOp(BinaryOp op) {
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
        report(error::Code::COMPILATION_INTERNAL_OPERATOR, "internal operator", _function.line);
        return Op::Add;
    }
};

}  // анонимное пространство имён

CompileResult compile(const Program& program, const CompileEnv& env, std::string_view source) {
    CompileResult result;
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
    result.functions.reserve(program.functions.size());
    for (const Function& function : program.functions) {
        if (result.error) break;
        if (auto chunk = FunctionCompiler(function, functions, env, source, result.error).run()) {
            result.functions.push_back(std::move(*chunk));
        }
    }
    if (result.error) result.functions.clear();
    return result;
}

}  // пространство имён sme
