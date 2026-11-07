#include "sme_parser.h"

#include "sme_lexer.h"
#include "sme/sme_error.h"

// Парсер. Рекурсивный спуск от программы к выражениям.
// Приоритет операторов — в parseBinary. Присваивание правоассоциативно,
// как в C: a = b = c. Перевод строки заканчивает return/break/continue/goto,
// поэтому «return» и «1;» на следующей строке возвращает null, а единица
// становится отдельным выражением и выбрасывается.

#include <limits>
#include <optional>
#include <utility>

namespace sme {
namespace {

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string name)
        : _tokens(std::move(tokens))
        , _name(std::move(name)) {}

    error::Result<Program> Run() {
        Program program;
        while (!Check(Token::Kind::End) && !_error) {
            SkipNewlines();
            if (Check(Token::Kind::End)) {
                break;
            }
            if (Match(Token::Kind::Semicolon)) {
                continue;
            }
            if (Check(Token::Kind::KwFunc)) {
                if (ForwardDeclaration()) continue;
                program.functions.push_back(ParseFunction());
                continue;
            }
            if (IsDeclKeyword()) {
                ParseGlobalDecl(program);
                continue;
            }
            Report(error::Code::PARSE_EXPECTED_FUNCTION_OR_DECLARATION,
                "expected a function or declaration in " + _name + ", got " + Spell(Peek()), Peek().line);
        }
        return {std::move(program), std::move(_error)};
    }

private:
    std::vector<Token> _tokens;
    std::string _name;
    size_t _pos = 0;
    // Скобки, внутри которых перевод строки не должен заканчивать оператор.
    // _group — круглые и квадратные, _braceExpr — фигурные в выражении (таблица).
    int _group = 0;
    int _braceExpr = 0;
    error::Error::Opt _error;

    void Report(error::Code code, std::string message, int line) {
        if (_error) return;
        error::Location location;
        location.source = _name;
        location.line = line;
        _error.emplace(code, std::move(message), std::move(location));
    }

    const Token& Peek() {
        if (_group > 0 || _braceExpr > 0) {
            while (_tokens[_pos].kind == Token::Kind::Newline) {
                ++_pos;
            }
        }
        return _tokens[_pos];
    }

    bool Check(Token::Kind kind) { return Peek().kind == kind; }

    void Account(Token::Kind kind) {
        if (kind == Token::Kind::LParen || kind == Token::Kind::LBracket) {
            ++_group;
        } else if ((kind == Token::Kind::RParen || kind == Token::Kind::RBracket) && _group > 0) {
            --_group;
        }
    }

    bool Match(Token::Kind kind) {
        if (!Check(kind)) {
            return false;
        }
        Account(kind);
        ++_pos;
        return true;
    }

    Token Consume(Token::Kind kind, const std::string& what) {
        if (!Check(kind)) {
            Report(error::Code::PARSE_EXPECTED_TOKEN,
                "expected " + what + " in " + _name + ", got " + Spell(Peek()), Peek().line);
            Token missing;
            missing.kind = kind;
            missing.line = Peek().line;
            return missing;
        }
        Account(kind);
        return _tokens[_pos++];
    }

    void SkipNewlines() {
        while (Check(Token::Kind::Newline)) {
            ++_pos;
        }
    }

    bool IsDeclKeyword() {
        if (Check(Token::Kind::KwVar) || Check(Token::Kind::KwInt) || Check(Token::Kind::KwFloat) || Check(Token::Kind::KwStr)) {
            return true;
        }
        if (!Check(Token::Kind::Identifier)) return false;
        const std::string& text = Peek().text;
        return text == "tab" || text == "ref" || text == "ptr";
    }

    GlobalKind DeclKind() const {
        const Token& token = _tokens[_pos];
        if (token.kind == Token::Kind::KwInt) return GlobalKind::Int;
        if (token.kind == Token::Kind::KwFloat) return GlobalKind::Float;
        if (token.kind == Token::Kind::KwStr) return GlobalKind::Str;
        if (token.text == "tab") return GlobalKind::Tab;
        if (token.text == "ref") return GlobalKind::Ref;
        if (token.text == "ptr") return GlobalKind::Ptr;
        return GlobalKind::Dynamic;
    }

    // func a, b; — разрешение имени до определения. В таблицу функций
    // заглушка не пишется: компилятор и так не требует, чтобы символ уже существовал.
    bool ForwardDeclaration() {
        if (_pos + 1 >= _tokens.size() || _tokens[_pos + 1].kind != Token::Kind::Identifier) {
            return false;
        }
        Token::Kind after = _pos + 2 < _tokens.size() ? _tokens[_pos + 2].kind : Token::Kind::End;
        if (after != Token::Kind::Comma && after != Token::Kind::Semicolon && after != Token::Kind::Newline) {
            return false;
        }
        ++_pos;
        do {
            Consume(Token::Kind::Identifier, "function name");
        } while (Match(Token::Kind::Comma));
        FinishStmt();
        return true;
    }

    void FinishStmt() {
        if (Match(Token::Kind::Semicolon)) {
            while (Match(Token::Kind::Semicolon)) {
            }
            return;
        }
        if (Check(Token::Kind::Newline) || Check(Token::Kind::RBrace) || Check(Token::Kind::End) || Check(Token::Kind::KwElse)) {
            return;
        }
        Report(error::Code::PARSE_EXPECTED_END_OF_STATEMENT,
            "expected end of statement in " + _name + ", got " + Spell(Peek()), Peek().line);
    }

    void RememberGlobal(Program& program, const std::string& global, GlobalKind kind) {
        for (const GlobalDecl& decl : program.globals) {
            if (decl.name == global) {
                return;
            }
        }
        program.globals.push_back(GlobalDecl{global, kind});
    }

    // int x; на уровне файла. «int x = 1» здесь ошибка: значение глобала задаёт #def.
    void ParseGlobalDecl(Program& program) {
        GlobalKind kind = DeclKind();
        ++_pos;
        do {
            Token name = Consume(Token::Kind::Identifier, "name");
            RememberGlobal(program, name.text, kind);
            if (Check(Token::Kind::Assign)) {
                Report(error::Code::PARSE_GLOBAL_INITIALIZER_UNSUPPORTED,
                    "global initializers are not supported; use #def", Peek().line);
                while (!Check(Token::Kind::Newline) && !Check(Token::Kind::Semicolon) &&
                       !Check(Token::Kind::End)) {
                    ++_pos;
                }
                break;
            }
        } while (Match(Token::Kind::Comma));
        FinishStmt();
    }

    Function ParseFunction() {
        Token functionToken = _tokens[_pos++];
        Token name = Consume(Token::Kind::Identifier, "function name");
        Consume(Token::Kind::LParen, "'('");
        Function function;
        function.line = functionToken.line;
        function.name = name.text;
        if (!Check(Token::Kind::RParen)) {
            do {
                function.params.push_back(Consume(Token::Kind::Identifier, "parameter name").text);
            } while (Match(Token::Kind::Comma));
        }
        Consume(Token::Kind::RParen, "')'");
        SkipNewlines();
        function.body = ParseBlock();
        return function;
    }

    std::vector<std::unique_ptr<Stmt>> ParseBlock() {
        Consume(Token::Kind::LBrace, "'{'");
        std::vector<std::unique_ptr<Stmt>> body;
        while (!Check(Token::Kind::RBrace) && !Check(Token::Kind::End) && !_error) {
            SkipNewlines();
            if (Check(Token::Kind::RBrace) || Check(Token::Kind::End)) {
                break;
            }
            if (Match(Token::Kind::Semicolon)) {
                continue;
            }
            size_t before = _pos;
            body.push_back(ParseStmt());
            if (_pos == before) {
                Report(error::Code::PARSE_NO_PROGRESS, "parser did not advance in " + _name, Peek().line);
                if (!Check(Token::Kind::End)) ++_pos;
            }
        }
        Consume(Token::Kind::RBrace, "'}'");
        return body;
    }

    std::vector<std::unique_ptr<Stmt>> ParseSuite() {
        SkipNewlines();
        if (Check(Token::Kind::LBrace)) {
            return ParseBlock();
        }
        std::vector<std::unique_ptr<Stmt>> body;
        body.push_back(ParseStmt());
        return body;
    }

    std::unique_ptr<Stmt> ParseStmt() {
        SkipNewlines();
        if (Check(Token::Kind::LBrace)) {
            auto stmt = MakeStmt(Stmt::Kind::Block, Peek().line);
            stmt->body = ParseBlock();
            return stmt;
        }
        if (Check(Token::Kind::KwIf)) {
            return ParseIf();
        }
        if (Check(Token::Kind::KwWhile)) {
            return ParseWhile();
        }
        if (Check(Token::Kind::KwDo)) {
            return ParseDo();
        }
        if (Check(Token::Kind::KwFor)) {
            return ParseFor();
        }
        if (Check(Token::Kind::Identifier) && _pos + 1 < _tokens.size() && _tokens[_pos + 1].kind == Token::Kind::Colon) {
            auto stmt = MakeStmt(Stmt::Kind::Label, Peek().line);
            stmt->name = _tokens[_pos++].text;
            ++_pos;
            return stmt;
        }
        if (IsDeclKeyword()) {
            if (Check(Token::Kind::KwVar) && _pos + 1 < _tokens.size() && _tokens[_pos + 1].kind == Token::Kind::LParen) {
                auto stmt = MakeStmt(Stmt::Kind::Expr, Peek().line);
                stmt->expr = ParseExpr();
                FinishStmt();
                return stmt;
            }
            return ParseLocalDecl();
        }
        if (Match(Token::Kind::KwReturn)) {
            auto stmt = MakeStmt(Stmt::Kind::Return, _tokens[_pos - 1].line);
            if (!Check(Token::Kind::Semicolon) && !Check(Token::Kind::Newline) && !Check(Token::Kind::RBrace) && !Check(Token::Kind::End)) {
                stmt->expr = ParseExpr();
            }
            FinishStmt();
            return stmt;
        }
        if (Match(Token::Kind::KwBreak)) {
            auto stmt = MakeStmt(Stmt::Kind::Break, _tokens[_pos - 1].line);
            FinishStmt();
            return stmt;
        }
        if (Match(Token::Kind::KwContinue)) {
            auto stmt = MakeStmt(Stmt::Kind::Continue, _tokens[_pos - 1].line);
            FinishStmt();
            return stmt;
        }
        if (Match(Token::Kind::KwGoto)) {
            auto stmt = MakeStmt(Stmt::Kind::Goto, _tokens[_pos - 1].line);
            stmt->name = Consume(Token::Kind::Identifier, "label").text;
            FinishStmt();
            return stmt;
        }
        if (Match(Token::Kind::KwStop)) {
            auto stmt = MakeStmt(Stmt::Kind::Stop, _tokens[_pos - 1].line);
            FinishStmt();
            return stmt;
        }
        if (Match(Token::Kind::KwBreakpoint)) {
            auto stmt = MakeStmt(Stmt::Kind::Expr, _tokens[_pos - 1].line);
            FinishStmt();
            stmt->expr.reset();
            return stmt;
        }
        auto stmt = MakeStmt(Stmt::Kind::Expr, Peek().line);
        stmt->expr = ParseExpr();
        FinishStmt();
        return stmt;
    }

    std::unique_ptr<Stmt> ParseLocalDecl() {
        int line = Peek().line;
        ++_pos;
        auto block = MakeStmt(Stmt::Kind::Block, line);
        do {
            auto var = MakeStmt(Stmt::Kind::Var, Peek().line);
            var->name = Consume(Token::Kind::Identifier, "variable name").text;
            if (Match(Token::Kind::Assign)) {
                var->expr = ParseExpr();
            }
            block->body.push_back(std::move(var));
        } while (Match(Token::Kind::Comma));
        FinishStmt();
        if (block->body.size() == 1) {
            return std::move(block->body[0]);
        }
        return block;
    }

    std::unique_ptr<Stmt> ParseIf() {
        Token token = Consume(Token::Kind::KwIf, "'if'");
        auto stmt = MakeStmt(Stmt::Kind::If, token.line);
        stmt->expr = ParseExpr();
        stmt->body = ParseSuite();
        SkipNewlines();
        if (Match(Token::Kind::KwElse)) {
            stmt->hasElse = true;
            SkipNewlines();
            if (Check(Token::Kind::KwIf)) {
                stmt->elseBody.push_back(ParseIf());
            } else {
                stmt->elseBody = ParseSuite();
            }
        }
        return stmt;
    }

    std::unique_ptr<Stmt> ParseWhile() {
        Token token = Consume(Token::Kind::KwWhile, "'while'");
        auto stmt = MakeStmt(Stmt::Kind::While, token.line);
        stmt->expr = ParseExpr();
        stmt->body = ParseSuite();
        return stmt;
    }

    std::unique_ptr<Stmt> ParseDo() {
        Token token = Consume(Token::Kind::KwDo, "'do'");
        auto stmt = MakeStmt(Stmt::Kind::Do, token.line);
        stmt->body = ParseSuite();
        SkipNewlines();
        Consume(Token::Kind::KwWhile, "'while'");
        stmt->expr = ParseExpr();
        FinishStmt();
        return stmt;
    }

    std::unique_ptr<Stmt> ParseFor() {
        Token token = Consume(Token::Kind::KwFor, "'for'");
        if (Match(Token::Kind::LParen)) {
            auto stmt = MakeStmt(Stmt::Kind::ForC, token.line);
            if (!Check(Token::Kind::Semicolon)) {
                stmt->expr = ParseExpr();
            }
            Consume(Token::Kind::Semicolon, "';'");
            if (!Check(Token::Kind::Semicolon)) {
                stmt->expr2 = ParseExpr();
            }
            Consume(Token::Kind::Semicolon, "';'");
            if (!Check(Token::Kind::RParen)) {
                stmt->expr3 = ParseExpr();
            }
            Consume(Token::Kind::RParen, "')'");
            stmt->body = ParseSuite();
            return stmt;
        }
        auto stmt = MakeStmt(Stmt::Kind::For, token.line);
        stmt->name = Consume(Token::Kind::Identifier, "loop variable").text;
        Consume(Token::Kind::Assign, "'='");
        stmt->expr = ParseExpr();
        Consume(Token::Kind::DotDot, "'..'");
        stmt->expr2 = ParseExpr();
        stmt->body = ParseSuite();
        return stmt;
    }

    struct AssignForm {
        bool compound = false;
        BinaryOp op = BinaryOp::Add;
    };

    static std::optional<AssignForm> AsAssign(Token::Kind kind) {
        AssignForm form;
        switch (kind) {
        case Token::Kind::Assign: return form;
        case Token::Kind::PlusEq: form.compound = true; form.op = BinaryOp::Add; return form;
        case Token::Kind::MinusEq: form.compound = true; form.op = BinaryOp::Sub; return form;
        case Token::Kind::StarEq: form.compound = true; form.op = BinaryOp::Mul; return form;
        case Token::Kind::SlashEq: form.compound = true; form.op = BinaryOp::Div; return form;
        case Token::Kind::PercentEq: form.compound = true; form.op = BinaryOp::Mod; return form;
        case Token::Kind::AmpEq: form.compound = true; form.op = BinaryOp::BitAnd; return form;
        case Token::Kind::PipeEq: form.compound = true; form.op = BinaryOp::BitOr; return form;
        case Token::Kind::CaretEq: form.compound = true; form.op = BinaryOp::BitXor; return form;
        case Token::Kind::ShlEq: form.compound = true; form.op = BinaryOp::Shl; return form;
        case Token::Kind::ShrEq: form.compound = true; form.op = BinaryOp::Shr; return form;
        default: return std::nullopt;
        }
    }

    std::unique_ptr<Expr> ParseExpr() {
        auto left = ParseTernary();
        if (auto form = AsAssign(Peek().kind)) {
            if (!Assignable(*left)) {
                Report(error::Code::PARSE_INVALID_ASSIGNMENT_TARGET,
                    "invalid assignment target in " + _name, left->line);
            }
            int line = Peek().line;
            ++_pos;
            SkipNewlines();
            auto rhs = ParseExpr();
            auto expr = MakeExpr(form->compound ? Expr::Kind::Modify : Expr::Kind::Assign, line);
            if (form->compound) {
                expr->binary = form->op;
            }
            expr->kids.push_back(std::move(left));
            expr->kids.push_back(std::move(rhs));
            return expr;
        }
        return left;
    }

    std::unique_ptr<Expr> ParseTernary() {
        auto cond = ParseBinary(0);
        size_t save = _pos;
        SkipNewlines();
        if (!Match(Token::Kind::Question)) {
            _pos = save;
            return cond;
        }
        auto expr = MakeExpr(Expr::Kind::Ternary, cond->line);
        SkipNewlines();
        auto yes = ParseExpr();
        SkipNewlines();
        Consume(Token::Kind::Colon, "':'");
        SkipNewlines();
        auto no = ParseExpr();
        expr->kids.push_back(std::move(cond));
        expr->kids.push_back(std::move(yes));
        expr->kids.push_back(std::move(no));
        return expr;
    }

    struct Bin {
        BinaryOp op;
        int prec;
    };

    static std::optional<Bin> AsBinary(Token::Kind kind) {
        switch (kind) {
        case Token::Kind::OrOr:
        case Token::Kind::KwOr: return Bin{BinaryOp::Or, 2};
        case Token::Kind::AndAnd:
        case Token::Kind::KwAnd: return Bin{BinaryOp::And, 3};
        case Token::Kind::Pipe: return Bin{BinaryOp::BitOr, 4};
        case Token::Kind::Caret: return Bin{BinaryOp::BitXor, 5};
        case Token::Kind::Amp: return Bin{BinaryOp::BitAnd, 6};
        case Token::Kind::Eq: return Bin{BinaryOp::Eq, 7};
        case Token::Kind::Ne: return Bin{BinaryOp::Ne, 7};
        case Token::Kind::Lt: return Bin{BinaryOp::Lt, 8};
        case Token::Kind::Gt: return Bin{BinaryOp::Gt, 8};
        case Token::Kind::Le: return Bin{BinaryOp::Le, 8};
        case Token::Kind::Ge: return Bin{BinaryOp::Ge, 8};
        case Token::Kind::Shl: return Bin{BinaryOp::Shl, 9};
        case Token::Kind::Shr: return Bin{BinaryOp::Shr, 9};
        case Token::Kind::Plus: return Bin{BinaryOp::Add, 10};
        case Token::Kind::Minus: return Bin{BinaryOp::Sub, 10};
        case Token::Kind::Star: return Bin{BinaryOp::Mul, 11};
        case Token::Kind::Slash: return Bin{BinaryOp::Div, 11};
        case Token::Kind::Percent: return Bin{BinaryOp::Mod, 11};
        default: return std::nullopt;
        }
    }

    std::unique_ptr<Expr> ParseBinary(int minPrec) {
        auto left = ParseUnary();
        while (true) {
            size_t save = _pos;
            SkipNewlines();
            auto op = AsBinary(Peek().kind);
            if (!op || op->prec < minPrec) {
                _pos = save;
                break;
            }
            int line = Peek().line;
            ++_pos;
            SkipNewlines();
            auto right = ParseBinary(op->prec + 1);
            auto expr = MakeExpr(Expr::Kind::Binary, line);
            expr->binary = op->op;
            expr->kids.push_back(std::move(left));
            expr->kids.push_back(std::move(right));
            left = std::move(expr);
        }
        return left;
    }

    bool LookingAtCast() {
        if (_tokens[_pos].kind != Token::Kind::LParen) {
            return false;
        }
        size_t i = _pos + 1;
        while (i < _tokens.size() && _tokens[i].kind == Token::Kind::Newline) {
            ++i;
        }
        if (i >= _tokens.size()) {
            return false;
        }
        Token::Kind kind = _tokens[i].kind;
        return kind == Token::Kind::KwInt || kind == Token::Kind::KwFloat || kind == Token::Kind::KwStr;
    }

    std::unique_ptr<Expr> ParseUnary() {
        if (LookingAtCast()) {
            int line = Peek().line;
            Consume(Token::Kind::LParen, "'('");
            std::string type = "str";
            if (Match(Token::Kind::KwInt)) type = "int";
            else if (Match(Token::Kind::KwFloat)) type = "float";
            else Consume(Token::Kind::KwStr, "type");
            std::string format;
            if (Check(Token::Kind::String)) {
                format = _tokens[_pos++].text;
            }
            Consume(Token::Kind::RParen, "')'");
            auto expr = MakeExpr(Expr::Kind::Cast, line);
            expr->text = type;
            expr->format = std::move(format);
            expr->kids.push_back(ParseUnary());
            return expr;
        }
        if (Match(Token::Kind::Question)) {
            Token name = Consume(Token::Kind::Identifier, "name");
            auto expr = MakeExpr(Expr::Kind::Query, name.line);
            expr->text = name.text;
            return expr;
        }
        if (Match(Token::Kind::PlusPlus) || Match(Token::Kind::MinusMinus)) {
            Token op = _tokens[_pos - 1];
            auto expr = MakeExpr(op.kind == Token::Kind::PlusPlus ? Expr::Kind::PostInc : Expr::Kind::PostDec, op.line);
            expr->prefix = true;
            expr->kids.push_back(ParseUnary());
            return expr;
        }
        if (Check(Token::Kind::Minus) && _pos + 1 < _tokens.size() &&
            _tokens[_pos + 1].kind == Token::Kind::Int && _tokens[_pos + 1].text == "2147483648") {
            int line = Peek().line;
            _pos += 2;
            auto expr = MakeExpr(Expr::Kind::Int, line);
            expr->intValue = std::numeric_limits<int>::min();
            return expr;
        }
        if (Match(Token::Kind::Bang) || Match(Token::Kind::KwNot) || Match(Token::Kind::Minus) || Match(Token::Kind::Tilde) ||
            Match(Token::Kind::Amp) || Match(Token::Kind::Star)) {
            Token op = _tokens[_pos - 1];
            if (op.kind == Token::Kind::Star) {
                auto inner = ParseUnary();
                // *n++ — это (*n)++, меняется значение по ссылке, а не «указатель».
                // Запись «сдвинуть указатель» этим синтаксисом сделать нельзя.
                if (inner->kind == Expr::Kind::PostInc || inner->kind == Expr::Kind::PostDec) {
                    auto deref = MakeExpr(Expr::Kind::Deref, op.line);
                    deref->kids.push_back(std::move(inner->kids[0]));
                    inner->kids[0] = std::move(deref);
                    return inner;
                }
                auto expr = MakeExpr(Expr::Kind::Deref, op.line);
                expr->kids.push_back(std::move(inner));
                return expr;
            }
            if (op.kind == Token::Kind::Amp) {
                auto expr = MakeExpr(Expr::Kind::Addr, op.line);
                expr->kids.push_back(ParseUnary());
                return expr;
            }
            auto expr = MakeExpr(Expr::Kind::Unary, op.line);
            if (op.kind == Token::Kind::Minus) expr->unary = UnaryOp::Neg;
            else if (op.kind == Token::Kind::Tilde) expr->unary = UnaryOp::BitNot;
            else expr->unary = UnaryOp::Not;
            expr->kids.push_back(ParseUnary());
            return expr;
        }
        return ParsePostfix();
    }

    std::unique_ptr<Expr> ParsePostfix() {
        auto expr = ParsePrimary();
        while (true) {
            if (Match(Token::Kind::LParen)) {
                auto call = MakeExpr(Expr::Kind::Call, expr->line);
                call->kids.push_back(std::move(expr));
                if (!Check(Token::Kind::RParen)) {
                    do {
                        call->kids.push_back(ParseExpr());
                    } while (Match(Token::Kind::Comma));
                }
                Consume(Token::Kind::RParen, "')'");
                expr = std::move(call);
                continue;
            }
            if (Match(Token::Kind::LBracket)) {
                auto index = MakeExpr(Expr::Kind::Index, expr->line);
                index->kids.push_back(std::move(expr));
                index->kids.push_back(ParseExpr());
                Consume(Token::Kind::RBracket, "']'");
                expr = std::move(index);
                continue;
            }
            if (Match(Token::Kind::LChar)) {
                auto index = MakeExpr(Expr::Kind::CharIndex, expr->line);
                index->kids.push_back(std::move(expr));
                index->kids.push_back(ParseExpr());
                Consume(Token::Kind::RChar, "']]'");
                expr = std::move(index);
                continue;
            }
            if (Match(Token::Kind::Dot)) {
                Token name = Consume(Token::Kind::Identifier, "property name");
                auto member = MakeExpr(Expr::Kind::Member, name.line);
                member->text = name.text;
                member->kids.push_back(std::move(expr));
                expr = std::move(member);
                continue;
            }
            if (Match(Token::Kind::PlusPlus) || Match(Token::Kind::MinusMinus)) {
                Token op = _tokens[_pos - 1];
                auto updated = MakeExpr(op.kind == Token::Kind::PlusPlus ? Expr::Kind::PostInc : Expr::Kind::PostDec, op.line);
                updated->kids.push_back(std::move(expr));
                expr = std::move(updated);
                continue;
            }
            break;
        }
        return expr;
    }

    std::unique_ptr<Expr> ParsePrimary() {
        Token token = Peek();
        if (Match(Token::Kind::Int)) {
            if (token.text == "2147483648") {
                Report(error::Code::PARSE_INTEGER_OUT_OF_RANGE,
                    "integer literal out of range in " + _name, token.line);
            }
            auto expr = MakeExpr(Expr::Kind::Int, token.line);
            expr->intValue = token.intValue;
            return expr;
        }
        if (Match(Token::Kind::Float)) {
            auto expr = MakeExpr(Expr::Kind::Float, token.line);
            expr->floatValue = token.floatValue;
            return expr;
        }
        if (Match(Token::Kind::String)) {
            auto expr = MakeExpr(Expr::Kind::String, token.line);
            expr->text = token.text;
            while (true) {
                size_t look = _pos;
                while (look < _tokens.size() && _tokens[look].kind == Token::Kind::Newline) ++look;
                if (look >= _tokens.size() || _tokens[look].kind != Token::Kind::String) break;
                _pos = look + 1;
                expr->text += _tokens[look].text;
            }
            return expr;
        }
        if (Match(Token::Kind::KwTrue) || Match(Token::Kind::KwFalse)) {
            auto expr = MakeExpr(Expr::Kind::Int, token.line);
            expr->intValue = token.kind == Token::Kind::KwTrue ? 1 : 0;
            return expr;
        }
        if (Match(Token::Kind::KwVoid)) {
            auto expr = MakeExpr(Expr::Kind::String, token.line);
            return expr;
        }
        if (Match(Token::Kind::KwNull)) {
            return MakeExpr(Expr::Kind::Null, token.line);
        }
        if (Match(Token::Kind::KwPointer)) {
            auto expr = MakeExpr(Expr::Kind::Null, token.line);
            expr->intValue = 1;
            return expr;
        }
        if (Check(Token::Kind::Identifier) && token.text == "newobj") {
            size_t look = _pos + 1;
            while (look < _tokens.size() && _tokens[look].kind == Token::Kind::Newline) {
                ++look;
            }
            if (look < _tokens.size() && _tokens[look].kind == Token::Kind::Lt) {
                ++_pos;
                Consume(Token::Kind::Lt, "'<'");
                Token type = Consume(Token::Kind::Identifier, "type name");
                Consume(Token::Kind::Gt, "'>'");
                Consume(Token::Kind::LParen, "'('");
                auto expr = MakeExpr(Expr::Kind::NewObj, token.line);
                expr->text = type.text;
                if (!Check(Token::Kind::RParen)) {
                    do {
                        expr->kids.push_back(ParseExpr());
                    } while (Match(Token::Kind::Comma));
                }
                Consume(Token::Kind::RParen, "')'");
                return expr;
            }
        }
        if (Check(Token::Kind::KwVar) && _pos + 1 < _tokens.size() && _tokens[_pos + 1].kind == Token::Kind::LParen) {
            Token name = _tokens[_pos++];
            auto expr = MakeExpr(Expr::Kind::Name, name.line);
            expr->text = "var";
            return expr;
        }
        if ((Check(Token::Kind::KwStr) || Check(Token::Kind::KwInt) || Check(Token::Kind::KwFloat)) &&
            _pos + 1 < _tokens.size() && _tokens[_pos + 1].kind == Token::Kind::LParen) {
            Token name = _tokens[_pos++];
            auto expr = MakeExpr(Expr::Kind::Name, name.line);
            expr->text = name.kind == Token::Kind::KwInt ? "int" : name.kind == Token::Kind::KwFloat ? "float" : "str";
            return expr;
        }
        if (Match(Token::Kind::Identifier)) {
            auto expr = MakeExpr(Expr::Kind::Name, token.line);
            expr->text = token.text;
            return expr;
        }
        if (Match(Token::Kind::LParen)) {
            auto expr = ParseExpr();
            Consume(Token::Kind::RParen, "')'");
            return expr;
        }
        if (Match(Token::Kind::LBrace)) {
            auto expr = MakeExpr(Expr::Kind::List, token.line);
            ++_braceExpr;
            if (!Check(Token::Kind::RBrace)) {
                do {
                    if (Check(Token::Kind::RBrace)) {
                        break;
                    }
                    expr->kids.push_back(ParseExpr());
                } while (Match(Token::Kind::Comma));
            }
            Consume(Token::Kind::RBrace, "'}'");
            --_braceExpr;
            return expr;
        }
        if (Match(Token::Kind::LBracket)) {
            auto expr = MakeExpr(Expr::Kind::List, token.line);
            if (!Check(Token::Kind::RBracket)) {
                do {
                    expr->kids.push_back(ParseExpr());
                } while (Match(Token::Kind::Comma));
            }
            Consume(Token::Kind::RBracket, "']'");
            return expr;
        }
        Report(error::Code::PARSE_EXPECTED_EXPRESSION,
            "expected expression in " + _name + ", got " + Spell(token), token.line);
        if (!Check(Token::Kind::End) && !Check(Token::Kind::Newline) && !Check(Token::Kind::RBrace)) {
            ++_pos;
        }
        return MakeExpr(Expr::Kind::Null, token.line);
    }

    // Куда можно присваивать. Call в этом списке — не «результат вызова»,
    // а var(индекс): вызов возвращает ссылку, и StoreRef пишет через неё.
    static bool Assignable(const Expr& expr) {
        return expr.kind == Expr::Kind::Name || expr.kind == Expr::Kind::Index || expr.kind == Expr::Kind::CharIndex ||
               expr.kind == Expr::Kind::Member || expr.kind == Expr::Kind::Deref || expr.kind == Expr::Kind::Call;
    }

    static std::unique_ptr<Expr> MakeExpr(Expr::Kind kind, int line) {
        auto expr = std::make_unique<Expr>();
        expr->kind = kind;
        expr->line = line;
        return expr;
    }

    static std::unique_ptr<Stmt> MakeStmt(Stmt::Kind kind, int line) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = kind;
        stmt->line = line;
        return stmt;
    }

    static std::string Spell(const Token& token) {
        switch (token.kind) {
        case Token::Kind::End: return "end";
        case Token::Kind::Invalid: return "invalid token";
        case Token::Kind::Newline: return "newline";
        case Token::Kind::Identifier: return "identifier '" + token.text + "'";
        case Token::Kind::Int: return "integer";
        case Token::Kind::Float: return "float";
        case Token::Kind::String: return "string";
        case Token::Kind::LParen: return "'('";
        case Token::Kind::RParen: return "')'";
        case Token::Kind::LBrace: return "'{'";
        case Token::Kind::RBrace: return "'}'";
        case Token::Kind::LBracket: return "'['";
        case Token::Kind::RBracket: return "']'";
        case Token::Kind::LChar: return "'[['";
        case Token::Kind::RChar: return "']]'";
        case Token::Kind::Comma: return "','";
        case Token::Kind::Semicolon: return "';'";
        case Token::Kind::Dot: return "'.'";
        case Token::Kind::DotDot: return "'..'";
        case Token::Kind::Colon: return "':'";
        case Token::Kind::Question: return "'?'";
        case Token::Kind::Plus: return "'+'";
        case Token::Kind::Minus: return "'-'";
        case Token::Kind::Star: return "'*'";
        case Token::Kind::Slash: return "'/'";
        case Token::Kind::Percent: return "'%'";
        case Token::Kind::PlusPlus: return "'++'";
        case Token::Kind::MinusMinus: return "'--'";
        case Token::Kind::PlusEq: return "'+='";
        case Token::Kind::MinusEq: return "'-='";
        case Token::Kind::StarEq: return "'*='";
        case Token::Kind::SlashEq: return "'/='";
        case Token::Kind::PercentEq: return "'%='";
        case Token::Kind::Eq: return "'=='";
        case Token::Kind::Ne: return "'!='";
        case Token::Kind::Lt: return "'<'";
        case Token::Kind::Le: return "'<='";
        case Token::Kind::Gt: return "'>'";
        case Token::Kind::Ge: return "'>='";
        case Token::Kind::Assign: return "'='";
        case Token::Kind::AndAnd: return "'&&'";
        case Token::Kind::OrOr: return "'||'";
        case Token::Kind::Bang: return "'!'";
        case Token::Kind::Tilde: return "'~'";
        case Token::Kind::Amp: return "'&'";
        case Token::Kind::Pipe: return "'|'";
        case Token::Kind::Caret: return "'^'";
        case Token::Kind::Shl: return "'<<'";
        case Token::Kind::Shr: return "'>>'";
        case Token::Kind::AmpEq: return "'&='";
        case Token::Kind::PipeEq: return "'|='";
        case Token::Kind::CaretEq: return "'^='";
        case Token::Kind::ShlEq: return "'<<='";
        case Token::Kind::ShrEq: return "'>>='";
        case Token::Kind::KwFunc: return "'func'";
        case Token::Kind::KwVar: return "'var'";
        case Token::Kind::KwIf: return "'if'";
        case Token::Kind::KwElse: return "'else'";
        case Token::Kind::KwWhile: return "'while'";
        case Token::Kind::KwFor: return "'for'";
        case Token::Kind::KwDo: return "'do'";
        case Token::Kind::KwReturn: return "'return'";
        case Token::Kind::KwBreak: return "'break'";
        case Token::Kind::KwContinue: return "'continue'";
        case Token::Kind::KwGoto: return "'goto'";
        case Token::Kind::KwStop: return "'stop'";
        case Token::Kind::KwTrue: return "'true'";
        case Token::Kind::KwFalse: return "'false'";
        case Token::Kind::KwNull: return "'null'";
        case Token::Kind::KwPointer: return "'NULL'";
        case Token::Kind::KwAnd: return "'and'";
        case Token::Kind::KwOr: return "'or'";
        case Token::Kind::KwNot: return "'not'";
        case Token::Kind::KwInt: return "'int'";
        case Token::Kind::KwFloat: return "'float'";
        case Token::Kind::KwStr: return "'str'";
        case Token::Kind::KwVoid: return "'VOID'";
        case Token::Kind::KwBreakpoint: return "'breakpoint'";
        }
        return "token";
    }
};

}  // анонимное пространство имён

error::Result<Program> parse(std::string_view source, std::string_view name) {
    auto lexed = lex(source, name);
    if (lexed.error) {
        return {Program{}, std::move(lexed.error)};
    }
    return Parser(std::move(lexed), std::string(name)).Run();
}

}  // пространство имён sme
