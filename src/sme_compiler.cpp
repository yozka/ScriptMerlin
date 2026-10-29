#include "sme_compiler.h"

#include "sme/sme_error.h"

// Компилятор функции. Сначала collect объявляет все локалы, включая те,
// что появились из присваивания в неизвестное имя. Потом codegen кладёт
// значения в регистры. Временные регистры живут один оператор: watermark
// откатывается, а слоты перед оператором обнуляются.

#include <unordered_map>

namespace sme {
namespace {

class FunctionCompiler {
public:
    FunctionCompiler(const Function& function,
                     const std::unordered_map<std::string, uint16_t>& functions,
                     const CompileEnv& env)
        : function_(function)
        , functions_(functions)
        , env_(env) {}

    Chunk run() {
        for (const std::string& param : function_.params) {
            declare(param, function_.line);
        }
        for (const auto& stmt : function_.body) {
            collect(*stmt);
        }
        watermark_ = static_cast<uint16_t>(locals_.size());
        regCount_ = watermark_;
        compileBody(function_.body);
        finishLabels();
        uint16_t result = allocTemp();
        emit(Op::LoadConst, result, addConst(std::monostate{}), 0, 0, 0, function_.line);
        emit(Op::Return, result, 0, 0, 0, 0, function_.line);

        Chunk chunk;
        chunk.name = function_.name;
        chunk.parameterCount = static_cast<uint16_t>(function_.params.size());
        chunk.registerCount = regCount_;
        chunk.code = std::move(code_);
        chunk.constants = std::move(constants_);
        return chunk;
    }

private:
    struct Loop {
        std::vector<uint16_t> breaks;
        std::vector<uint16_t> continues;
    };

    const Function& function_;
    const std::unordered_map<std::string, uint16_t>& functions_;
    const CompileEnv& env_;
    std::unordered_map<std::string, uint16_t> locals_;
    std::unordered_map<std::string, uint16_t> labels_;
    std::unordered_map<std::string, std::vector<uint16_t>> pendingGotos_;
    std::vector<Instruction> code_;
    std::vector<Constant> constants_;
    std::vector<Loop> loops_;
    uint16_t watermark_ = 0;
    uint16_t regCount_ = 0;

    void declare(const std::string& name, int line) {
        if (locals_.contains(name)) {
            throw RError("duplicate variable '" + name + "'", line);
        }
        if (locals_.size() >= 65535) {
            throw RError("too many local variables", line);
        }
        locals_.emplace(name, static_cast<uint16_t>(locals_.size()));
    }

    // Присваивание в неизвестное имя молча заводит локал. Опечатка в левой
    // части поэтому не ошибка, а новая переменная. Глобал с таким именем не перекрывается.
    void note(const std::string& name, int line) {
        if (locals_.contains(name) || env_.globals.contains(name)) {
            return;
        }
        declare(name, line);
    }

    void collectExpr(const Expr& expr) {
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
        if (stmt.kind == Stmt::Kind::Var) {
            declare(stmt.name, stmt.line);
        }
        if (stmt.kind == Stmt::Kind::For && !locals_.contains(stmt.name) && !env_.globals.contains(stmt.name)) {
            declare(stmt.name, stmt.line);
        }
        if (stmt.expr) collectExpr(*stmt.expr);
        if (stmt.expr2) collectExpr(*stmt.expr2);
        if (stmt.expr3) collectExpr(*stmt.expr3);
        for (const auto& child : stmt.body) collect(*child);
        for (const auto& child : stmt.elseBody) collect(*child);
    }

    uint16_t allocTemp() {
        if (watermark_ == 65535) {
            throw RError("too many registers", function_.line);
        }
        uint16_t reg = watermark_++;
        if (watermark_ > regCount_) {
            regCount_ = watermark_;
        }
        return reg;
    }

    uint16_t addConst(Constant constant) {
        if (constants_.size() >= 65535) {
            throw RError("too many constants", function_.line);
        }
        constants_.push_back(std::move(constant));
        return static_cast<uint16_t>(constants_.size() - 1);
    }

    uint16_t emit(Op op, uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e, int line) {
        if (code_.size() >= 65535) {
            throw RError("function is too large", line);
        }
        Instruction ins;
        ins.op = op;
        ins.a = a;
        ins.b = b;
        ins.c = c;
        ins.d = d;
        ins.e = e;
        ins.line = static_cast<uint16_t>(line > 0 && line <= 65535 ? line : 1);
        code_.push_back(ins);
        return static_cast<uint16_t>(code_.size() - 1);
    }

    uint16_t here() const { return static_cast<uint16_t>(code_.size()); }

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
            if (expr.intValue == 1) emit(Op::LoadConst, dest, addConst(Pointer{}), 0, 0, 0, expr.line);
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
        throw RError("internal expression", expr.line);
    }

    // ?имя. Неизвестное имя — 0. Иначе код типа значения. Для непереданного
    // параметра слот null, код 0, поэтому if (!?x) x = значение по умолчанию работает.
    uint16_t compileQuery(const Expr& expr) {
        uint16_t dest = allocTemp();
        if (!locals_.contains(expr.text) && !env_.globals.contains(expr.text)) {
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
        if (auto it = locals_.find(expr.text); it != locals_.end()) {
            return it->second;
        }
        if (auto it = env_.globals.find(expr.text); it != env_.globals.end()) {
            uint16_t dest = allocTemp();
            emit(Op::LoadGlobal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        if (functions_.contains(expr.text) || env_.natives.contains(expr.text)) {
            throw RError("function '" + expr.text + "' must be called", expr.line);
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
        uint16_t base = watermark_;
        for (uint16_t arg : args) {
            uint16_t slot = allocTemp();
            emit(Op::Copy, slot, arg, 0, 0, 0, expr.line);
        }
        uint16_t dest = allocTemp();
        uint16_t argc = static_cast<uint16_t>(args.size());
        if (callee.kind == Expr::Kind::Name) {
            if (auto function = functions_.find(callee.text); function != functions_.end()) {
                emit(Op::Call, dest, function->second, base, argc, 0, expr.line);
                return dest;
            }
            if (auto native = env_.natives.find(callee.text); native != env_.natives.end()) {
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
        throw RError("expression is not a function", expr.line);
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
        uint16_t base = watermark_;
        for (uint16_t item : items) {
            uint16_t slot = allocTemp();
            emit(Op::Copy, slot, item, 0, 0, 0, expr.line);
        }
        uint16_t dest = allocTemp();
        emit(Op::MakeTable, dest, base, static_cast<uint16_t>(items.size()), 0, 0, expr.line);
        return dest;
    }

    uint16_t compileAssign(const Expr& expr) {
        uint16_t value = compileExpr(*expr.kids[1]);
        store(*expr.kids[0], value, expr.line);
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
            if (auto it = locals_.find(target.text); it != locals_.end()) {
                return Place{Place::Kind::Local, it->second, 0};
            }
            if (auto it = env_.globals.find(target.text); it != env_.globals.end()) {
                return Place{Place::Kind::Global, it->second, 0};
            }
            throw RError("unknown variable '" + target.text + "'", target.line);
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
        throw RError(invalid, target.line);
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
        throw RError("internal assignment", line);
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
        throw RError("internal assignment", line);
    }

    void store(const Expr& target, uint16_t value, int line) {
        storePlace(placeOf(target, "invalid assignment target"), value, line);
    }

    uint16_t compileModify(const Expr& expr) {
        Place place = placeOf(*expr.kids[0], "invalid assignment target");
        uint16_t current = loadPlace(place, expr.line);
        uint16_t rhs = compileExpr(*expr.kids[1]);
        uint16_t dest = allocTemp();
        emit(binaryOp(expr.binary), dest, current, rhs, 0, 0, expr.line);
        storePlace(place, dest, expr.line);
        return dest;
    }

    uint16_t compileUpdate(const Expr& expr, int delta) {
        Place place = placeOf(*expr.kids[0], "invalid update target");
        uint16_t current = loadPlace(place, expr.line);
        uint16_t saved = current;
        if (!expr.prefix && place.kind == Place::Kind::Local) {
            saved = allocTemp();
            emit(Op::Copy, saved, current, 0, 0, 0, expr.line);
        }
        uint16_t step = allocTemp();
        emit(Op::LoadConst, step, addConst(delta), 0, 0, 0, expr.line);
        uint16_t next = allocTemp();
        emit(Op::Add, next, current, step, 0, 0, expr.line);
        storePlace(place, next, expr.line);
        return expr.prefix ? next : saved;
    }

    uint16_t compileTernary(const Expr& expr) {
        uint16_t dest = allocTemp();
        uint16_t cond = compileExpr(*expr.kids[0]);
        uint16_t jumpFalse = emit(Op::JumpIfFalse, cond, 0, 0, 0, 0, expr.line);
        uint16_t yes = compileExpr(*expr.kids[1]);
        emit(Op::Copy, dest, yes, 0, 0, 0, expr.line);
        uint16_t jumpEnd = emit(Op::Jump, 0, 0, 0, 0, 0, expr.line);
        code_[jumpFalse].b = here();
        uint16_t no = compileExpr(*expr.kids[2]);
        emit(Op::Copy, dest, no, 0, 0, 0, expr.line);
        code_[jumpEnd].a = here();
        return dest;
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
            throw RError("address expects a variable", expr.line);
        }
        uint16_t dest = allocTemp();
        if (auto it = locals_.find(target.text); it != locals_.end()) {
            emit(Op::MakeRefLocal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        if (auto it = env_.globals.find(target.text); it != env_.globals.end()) {
            emit(Op::MakeRefGlobal, dest, it->second, 0, 0, 0, expr.line);
            return dest;
        }
        throw RError("unknown variable '" + target.text + "'", target.line);
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
                emit(Op::Move, locals_.at(stmt.name), value, 0, 0, 0, stmt.line);
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
            if (loops_.empty()) throw RError("break outside of a loop", stmt.line);
            loops_.back().breaks.push_back(emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line));
            break;
        }
        case Stmt::Kind::Continue: {
            if (loops_.empty()) throw RError("continue outside of a loop", stmt.line);
            loops_.back().continues.push_back(emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line));
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
        uint16_t base = watermark_;
        for (const auto& stmt : body) {
            watermark_ = base;
            compileStmt(*stmt);
        }
        watermark_ = base;
    }

    void compileLabel(const Stmt& stmt) {
        if (labels_.contains(stmt.name)) {
            throw RError("duplicate label '" + stmt.name + "'", stmt.line);
        }
        labels_.emplace(stmt.name, here());
        for (uint16_t jump : pendingGotos_[stmt.name]) {
            code_[jump].a = here();
        }
        pendingGotos_.erase(stmt.name);
    }

    void compileGoto(const Stmt& stmt) {
        uint16_t jump = emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line);
        if (auto it = labels_.find(stmt.name); it != labels_.end()) {
            code_[jump].a = it->second;
        } else {
            pendingGotos_[stmt.name].push_back(jump);
        }
    }

    void finishLabels() {
        for (const auto& [name, jumps] : pendingGotos_) {
            if (!jumps.empty()) {
                throw RError("unknown label '" + name + "'", function_.line);
            }
        }
    }

    void compileIf(const Stmt& stmt) {
        uint16_t cond = compileExpr(*stmt.expr);
        uint16_t jumpFalse = emit(Op::JumpIfFalse, cond, 0, 0, 0, 0, stmt.line);
        compileBody(stmt.body);
        if (stmt.hasElse) {
            uint16_t jumpEnd = emit(Op::Jump, 0, 0, 0, 0, 0, stmt.line);
            code_[jumpFalse].b = here();
            compileBody(stmt.elseBody);
            code_[jumpEnd].a = here();
        } else {
            code_[jumpFalse].b = here();
        }
    }

    void compileWhile(const Stmt& stmt) {
        uint16_t start = here();
        loops_.push_back(Loop{});
        uint16_t cond = compileExpr(*stmt.expr);
        uint16_t jumpFalse = emit(Op::JumpIfFalse, cond, 0, 0, 0, 0, stmt.line);
        compileBody(stmt.body);
        uint16_t step = here();
        for (uint16_t jump : loops_.back().continues) {
            code_[jump].a = step;
        }
        emit(Op::Jump, start, 0, 0, 0, 0, stmt.line);
        uint16_t end = here();
        code_[jumpFalse].b = end;
        for (uint16_t jump : loops_.back().breaks) {
            code_[jump].a = end;
        }
        loops_.pop_back();
    }

    void compileDo(const Stmt& stmt) {
        uint16_t start = here();
        loops_.push_back(Loop{});
        compileBody(stmt.body);
        uint16_t condIp = here();
        for (uint16_t jump : loops_.back().continues) {
            code_[jump].a = condIp;
        }
        uint16_t cond = compileExpr(*stmt.expr);
        emit(Op::JumpIfTrue, cond, start, 0, 0, 0, stmt.line);
        uint16_t end = here();
        for (uint16_t jump : loops_.back().breaks) {
            code_[jump].a = end;
        }
        loops_.pop_back();
    }

    // Границы включительные, шаг всегда +1. Конец диапазона копируется в регистр
    // один раз: если справа локал, цикл не должен разъехаться, когда этот локал меняют внутри.
    void compileFor(const Stmt& stmt) {
        uint16_t index = locals_.contains(stmt.name) ? locals_.at(stmt.name) : env_.globals.at(stmt.name);
        bool global = !locals_.contains(stmt.name);
        uint16_t start = compileExpr(*stmt.expr);
        if (global) emit(Op::StoreGlobal, index, start, 0, 0, 0, stmt.line);
        else emit(Op::Move, index, start, 0, 0, 0, stmt.line);
        uint16_t endSource = compileExpr(*stmt.expr2);
        uint16_t end = allocTemp();
        emit(Op::Move, end, endSource, 0, 0, 0, stmt.line);
        uint16_t one = allocTemp();
        emit(Op::LoadConst, one, addConst(int{1}), 0, 0, 0, stmt.line);

        loops_.push_back(Loop{});
        uint16_t condIp = here();
        uint16_t current = index;
        if (global) {
            current = allocTemp();
            emit(Op::LoadGlobal, current, index, 0, 0, 0, stmt.line);
        }
        uint16_t cond = allocTemp();
        emit(Op::CmpLe, cond, current, end, 0, 0, stmt.line);
        uint16_t jumpFalse = emit(Op::JumpIfFalse, cond, 0, 0, 0, 0, stmt.line);
        compileBody(stmt.body);
        uint16_t increment = here();
        for (uint16_t jump : loops_.back().continues) {
            code_[jump].a = increment;
        }
        if (global) {
            uint16_t loaded = allocTemp();
            emit(Op::LoadGlobal, loaded, index, 0, 0, 0, stmt.line);
            emit(Op::Add, loaded, loaded, one, 0, 0, stmt.line);
            emit(Op::StoreGlobal, index, loaded, 0, 0, 0, stmt.line);
        } else {
            emit(Op::Add, index, index, one, 0, 0, stmt.line);
        }
        emit(Op::Jump, condIp, 0, 0, 0, 0, stmt.line);
        uint16_t endIp = here();
        code_[jumpFalse].b = endIp;
        for (uint16_t jump : loops_.back().breaks) {
            code_[jump].a = endIp;
        }
        loops_.pop_back();
    }

    void compileForC(const Stmt& stmt) {
        if (stmt.expr) {
            compileExpr(*stmt.expr);
        }
        uint16_t start = here();
        uint16_t jumpFalse = 0;
        bool hasCond = stmt.expr2 != nullptr;
        if (hasCond) {
            uint16_t cond = compileExpr(*stmt.expr2);
            jumpFalse = emit(Op::JumpIfFalse, cond, 0, 0, 0, 0, stmt.line);
        }
        loops_.push_back(Loop{});
        compileBody(stmt.body);
        uint16_t step = here();
        for (uint16_t jump : loops_.back().continues) {
            code_[jump].a = step;
        }
        if (stmt.expr3) {
            compileExpr(*stmt.expr3);
        }
        emit(Op::Jump, start, 0, 0, 0, 0, stmt.line);
        uint16_t end = here();
        if (hasCond) {
            code_[jumpFalse].b = end;
        }
        for (uint16_t jump : loops_.back().breaks) {
            code_[jump].a = end;
        }
        loops_.pop_back();
    }

    static Op binaryOp(BinaryOp op) {
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
            throw RError("internal operator");
        }
        throw RError("internal operator");
    }
};

}  // анонимное пространство имён

std::vector<Chunk> compile(const Program& program, const CompileEnv& env) {
    std::unordered_map<std::string, uint16_t> functions;
    for (const Function& function : program.functions) {
        if (functions.contains(function.name)) {
            throw RError("duplicate function '" + function.name + "'", function.line);
        }
        functions.emplace(function.name, static_cast<uint16_t>(functions.size()));
    }

    std::vector<Chunk> chunks;
    chunks.reserve(program.functions.size());
    for (const Function& function : program.functions) {
        chunks.push_back(FunctionCompiler(function, functions, env).run());
    }
    return chunks;
}

}  // пространство имён sme
