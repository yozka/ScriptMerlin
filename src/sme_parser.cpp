#include "sme_parser.h"

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
        : tokens_(std::move(tokens))
        , name_(std::move(name)) {}

    Program run() {
        Program program;
        while (!check(Token::Kind::End)) {
            skipNewlines();
            if (check(Token::Kind::End)) {
                break;
            }
            if (match(Token::Kind::Semicolon)) {
                continue;
            }
            if (check(Token::Kind::KwFunc)) {
                if (forwardDeclaration()) {
                    continue;
                }
                program.functions.push_back(parseFunction());
                continue;
            }
            if (isDeclKeyword()) {
                parseGlobalDecl(program);
                continue;
            }
            throw RError("expected a function or declaration in " + name_ + ", got " + spell(peek()), peek().line);
        }
        return program;
    }

private:
    std::vector<Token> tokens_;
    std::string name_;
    size_t pos_ = 0;
    // Скобки, внутри которых перевод строки не должен заканчивать оператор.
    // group_ — круглые и квадратные, braceExpr_ — фигурные в выражении (таблица).
    int group_ = 0;
    int braceExpr_ = 0;

    const Token& peek() {
        if (group_ > 0 || braceExpr_ > 0) {
            while (tokens_[pos_].kind == Token::Kind::Newline) {
                ++pos_;
            }
        }
        return tokens_[pos_];
    }

    bool check(Token::Kind kind) { return peek().kind == kind; }

    void account(Token::Kind kind) {
        if (kind == Token::Kind::LParen || kind == Token::Kind::LBracket) {
            ++group_;
        } else if ((kind == Token::Kind::RParen || kind == Token::Kind::RBracket) && group_ > 0) {
            --group_;
        }
    }

    bool match(Token::Kind kind) {
        if (!check(kind)) {
            return false;
        }
        account(kind);
        ++pos_;
        return true;
    }

    Token consume(Token::Kind kind, const std::string& what) {
        if (!check(kind)) {
            throw RError("expected " + what + " in " + name_ + ", got " + spell(peek()), peek().line);
        }
        account(kind);
        return tokens_[pos_++];
    }

    void skipNewlines() {
        while (check(Token::Kind::Newline)) {
            ++pos_;
        }
    }

    bool isDeclKeyword() {
        if (check(Token::Kind::KwVar) || check(Token::Kind::KwInt) || check(Token::Kind::KwFloat) || check(Token::Kind::KwStr)) {
            return true;
        }
        if (!check(Token::Kind::Identifier)) return false;
        const std::string& text = peek().text;
        return text == "tab" || text == "ref" || text == "ptr";
    }

    GlobalKind declKind() const {
        const Token& token = tokens_[pos_];
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
    bool forwardDeclaration() {
        if (pos_ + 1 >= tokens_.size() || tokens_[pos_ + 1].kind != Token::Kind::Identifier) {
            return false;
        }
        Token::Kind after = pos_ + 2 < tokens_.size() ? tokens_[pos_ + 2].kind : Token::Kind::End;
        if (after != Token::Kind::Comma && after != Token::Kind::Semicolon && after != Token::Kind::Newline) {
            return false;
        }
        ++pos_;
        do {
            consume(Token::Kind::Identifier, "function name");
        } while (match(Token::Kind::Comma));
        finishStmt();
        return true;
    }

    void finishStmt() {
        if (match(Token::Kind::Semicolon)) {
            while (match(Token::Kind::Semicolon)) {
            }
            return;
        }
        if (check(Token::Kind::Newline) || check(Token::Kind::RBrace) || check(Token::Kind::End) || check(Token::Kind::KwElse)) {
            return;
        }
        throw RError("expected end of statement in " + name_ + ", got " + spell(peek()), peek().line);
    }

    void rememberGlobal(Program& program, const std::string& global, GlobalKind kind) {
        for (const GlobalDecl& decl : program.globals) {
            if (decl.name == global) {
                return;
            }
        }
        program.globals.push_back(GlobalDecl{global, kind});
    }

    // int x; на уровне файла. «int x = 1» здесь ошибка: значение глобала задаёт #def.
    void parseGlobalDecl(Program& program) {
        GlobalKind kind = declKind();
        ++pos_;
        do {
            Token name = consume(Token::Kind::Identifier, "name");
            rememberGlobal(program, name.text, kind);
            if (check(Token::Kind::Assign)) {
                throw RError("global initializers are not supported; use #def", peek().line);
            }
        } while (match(Token::Kind::Comma));
        finishStmt();
    }

    Function parseFunction() {
        Token functionToken = tokens_[pos_++];
        Token name = consume(Token::Kind::Identifier, "function name");
        consume(Token::Kind::LParen, "'('");
        Function function;
        function.line = functionToken.line;
        function.name = name.text;
        if (!check(Token::Kind::RParen)) {
            do {
                function.params.push_back(consume(Token::Kind::Identifier, "parameter name").text);
            } while (match(Token::Kind::Comma));
        }
        consume(Token::Kind::RParen, "')'");
        skipNewlines();
        function.body = parseBlock();
        return function;
    }

    std::vector<std::unique_ptr<Stmt>> parseBlock() {
        consume(Token::Kind::LBrace, "'{'");
        std::vector<std::unique_ptr<Stmt>> body;
        while (!check(Token::Kind::RBrace) && !check(Token::Kind::End)) {
            skipNewlines();
            if (check(Token::Kind::RBrace) || check(Token::Kind::End)) {
                break;
            }
            if (match(Token::Kind::Semicolon)) {
                continue;
            }
            size_t before = pos_;
            body.push_back(parseStmt());
            if (pos_ == before) {
                throw RError("parser did not advance in " + name_, peek().line);
            }
        }
        consume(Token::Kind::RBrace, "'}'");
        return body;
    }

    std::vector<std::unique_ptr<Stmt>> parseSuite() {
        skipNewlines();
        if (check(Token::Kind::LBrace)) {
            return parseBlock();
        }
        std::vector<std::unique_ptr<Stmt>> body;
        body.push_back(parseStmt());
        return body;
    }

    std::unique_ptr<Stmt> parseStmt() {
        skipNewlines();
        if (check(Token::Kind::LBrace)) {
            auto stmt = makeStmt(Stmt::Kind::Block, peek().line);
            stmt->body = parseBlock();
            return stmt;
        }
        if (check(Token::Kind::KwIf)) {
            return parseIf();
        }
        if (check(Token::Kind::KwWhile)) {
            return parseWhile();
        }
        if (check(Token::Kind::KwDo)) {
            return parseDo();
        }
        if (check(Token::Kind::KwFor)) {
            return parseFor();
        }
        if (check(Token::Kind::Identifier) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == Token::Kind::Colon) {
            auto stmt = makeStmt(Stmt::Kind::Label, peek().line);
            stmt->name = tokens_[pos_++].text;
            ++pos_;
            return stmt;
        }
        if (isDeclKeyword()) {
            if (check(Token::Kind::KwVar) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == Token::Kind::LParen) {
                auto stmt = makeStmt(Stmt::Kind::Expr, peek().line);
                stmt->expr = parseExpr();
                finishStmt();
                return stmt;
            }
            return parseLocalDecl();
        }
        if (match(Token::Kind::KwReturn)) {
            auto stmt = makeStmt(Stmt::Kind::Return, tokens_[pos_ - 1].line);
            if (!check(Token::Kind::Semicolon) && !check(Token::Kind::Newline) && !check(Token::Kind::RBrace) && !check(Token::Kind::End)) {
                stmt->expr = parseExpr();
            }
            finishStmt();
            return stmt;
        }
        if (match(Token::Kind::KwBreak)) {
            auto stmt = makeStmt(Stmt::Kind::Break, tokens_[pos_ - 1].line);
            finishStmt();
            return stmt;
        }
        if (match(Token::Kind::KwContinue)) {
            auto stmt = makeStmt(Stmt::Kind::Continue, tokens_[pos_ - 1].line);
            finishStmt();
            return stmt;
        }
        if (match(Token::Kind::KwGoto)) {
            auto stmt = makeStmt(Stmt::Kind::Goto, tokens_[pos_ - 1].line);
            stmt->name = consume(Token::Kind::Identifier, "label").text;
            finishStmt();
            return stmt;
        }
        if (match(Token::Kind::KwStop)) {
            auto stmt = makeStmt(Stmt::Kind::Stop, tokens_[pos_ - 1].line);
            finishStmt();
            return stmt;
        }
        if (match(Token::Kind::KwBreakpoint)) {
            auto stmt = makeStmt(Stmt::Kind::Expr, tokens_[pos_ - 1].line);
            finishStmt();
            stmt->expr.reset();
            return stmt;
        }
        auto stmt = makeStmt(Stmt::Kind::Expr, peek().line);
        stmt->expr = parseExpr();
        finishStmt();
        return stmt;
    }

    std::unique_ptr<Stmt> parseLocalDecl() {
        int line = peek().line;
        ++pos_;
        auto block = makeStmt(Stmt::Kind::Block, line);
        do {
            auto var = makeStmt(Stmt::Kind::Var, peek().line);
            var->name = consume(Token::Kind::Identifier, "variable name").text;
            if (match(Token::Kind::Assign)) {
                var->expr = parseExpr();
            }
            block->body.push_back(std::move(var));
        } while (match(Token::Kind::Comma));
        finishStmt();
        if (block->body.size() == 1) {
            return std::move(block->body[0]);
        }
        return block;
    }

    std::unique_ptr<Stmt> parseIf() {
        Token token = consume(Token::Kind::KwIf, "'if'");
        auto stmt = makeStmt(Stmt::Kind::If, token.line);
        stmt->expr = parseExpr();
        stmt->body = parseSuite();
        skipNewlines();
        if (match(Token::Kind::KwElse)) {
            stmt->hasElse = true;
            skipNewlines();
            if (check(Token::Kind::KwIf)) {
                stmt->elseBody.push_back(parseIf());
            } else {
                stmt->elseBody = parseSuite();
            }
        }
        return stmt;
    }

    std::unique_ptr<Stmt> parseWhile() {
        Token token = consume(Token::Kind::KwWhile, "'while'");
        auto stmt = makeStmt(Stmt::Kind::While, token.line);
        stmt->expr = parseExpr();
        stmt->body = parseSuite();
        return stmt;
    }

    std::unique_ptr<Stmt> parseDo() {
        Token token = consume(Token::Kind::KwDo, "'do'");
        auto stmt = makeStmt(Stmt::Kind::Do, token.line);
        stmt->body = parseSuite();
        skipNewlines();
        consume(Token::Kind::KwWhile, "'while'");
        stmt->expr = parseExpr();
        finishStmt();
        return stmt;
    }

    std::unique_ptr<Stmt> parseFor() {
        Token token = consume(Token::Kind::KwFor, "'for'");
        if (match(Token::Kind::LParen)) {
            auto stmt = makeStmt(Stmt::Kind::ForC, token.line);
            if (!check(Token::Kind::Semicolon)) {
                stmt->expr = parseExpr();
            }
            consume(Token::Kind::Semicolon, "';'");
            if (!check(Token::Kind::Semicolon)) {
                stmt->expr2 = parseExpr();
            }
            consume(Token::Kind::Semicolon, "';'");
            if (!check(Token::Kind::RParen)) {
                stmt->expr3 = parseExpr();
            }
            consume(Token::Kind::RParen, "')'");
            stmt->body = parseSuite();
            return stmt;
        }
        auto stmt = makeStmt(Stmt::Kind::For, token.line);
        stmt->name = consume(Token::Kind::Identifier, "loop variable").text;
        consume(Token::Kind::Assign, "'='");
        stmt->expr = parseExpr();
        consume(Token::Kind::DotDot, "'..'");
        stmt->expr2 = parseExpr();
        stmt->body = parseSuite();
        return stmt;
    }

    struct AssignForm {
        bool compound = false;
        BinaryOp op = BinaryOp::Add;
    };

    static std::optional<AssignForm> asAssign(Token::Kind kind) {
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

    std::unique_ptr<Expr> parseExpr() {
        auto left = parseTernary();
        if (auto form = asAssign(peek().kind)) {
            if (!assignable(*left)) {
                throw RError("invalid assignment target in " + name_, left->line);
            }
            int line = peek().line;
            ++pos_;
            skipNewlines();
            auto rhs = parseExpr();
            auto expr = makeExpr(form->compound ? Expr::Kind::Modify : Expr::Kind::Assign, line);
            if (form->compound) {
                expr->binary = form->op;
            }
            expr->kids.push_back(std::move(left));
            expr->kids.push_back(std::move(rhs));
            return expr;
        }
        return left;
    }

    std::unique_ptr<Expr> parseTernary() {
        auto cond = parseBinary(0);
        size_t save = pos_;
        skipNewlines();
        if (!match(Token::Kind::Question)) {
            pos_ = save;
            return cond;
        }
        auto expr = makeExpr(Expr::Kind::Ternary, cond->line);
        skipNewlines();
        auto yes = parseExpr();
        skipNewlines();
        consume(Token::Kind::Colon, "':'");
        skipNewlines();
        auto no = parseExpr();
        expr->kids.push_back(std::move(cond));
        expr->kids.push_back(std::move(yes));
        expr->kids.push_back(std::move(no));
        return expr;
    }

    struct Bin {
        BinaryOp op;
        int prec;
    };

    static std::optional<Bin> asBinary(Token::Kind kind) {
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

    std::unique_ptr<Expr> parseBinary(int minPrec) {
        auto left = parseUnary();
        while (true) {
            size_t save = pos_;
            skipNewlines();
            auto op = asBinary(peek().kind);
            if (!op || op->prec < minPrec) {
                pos_ = save;
                break;
            }
            int line = peek().line;
            ++pos_;
            skipNewlines();
            auto right = parseBinary(op->prec + 1);
            auto expr = makeExpr(Expr::Kind::Binary, line);
            expr->binary = op->op;
            expr->kids.push_back(std::move(left));
            expr->kids.push_back(std::move(right));
            left = std::move(expr);
        }
        return left;
    }

    bool lookingAtCast() {
        if (tokens_[pos_].kind != Token::Kind::LParen) {
            return false;
        }
        size_t i = pos_ + 1;
        while (i < tokens_.size() && tokens_[i].kind == Token::Kind::Newline) {
            ++i;
        }
        if (i >= tokens_.size()) {
            return false;
        }
        Token::Kind kind = tokens_[i].kind;
        return kind == Token::Kind::KwInt || kind == Token::Kind::KwFloat || kind == Token::Kind::KwStr;
    }

    std::unique_ptr<Expr> parseUnary() {
        if (lookingAtCast()) {
            int line = peek().line;
            consume(Token::Kind::LParen, "'('");
            std::string type = "str";
            if (match(Token::Kind::KwInt)) type = "int";
            else if (match(Token::Kind::KwFloat)) type = "float";
            else consume(Token::Kind::KwStr, "type");
            std::string format;
            if (check(Token::Kind::String)) {
                format = tokens_[pos_++].text;
            }
            consume(Token::Kind::RParen, "')'");
            auto expr = makeExpr(Expr::Kind::Cast, line);
            expr->text = type;
            expr->format = std::move(format);
            expr->kids.push_back(parseUnary());
            return expr;
        }
        if (match(Token::Kind::Question)) {
            Token name = consume(Token::Kind::Identifier, "name");
            auto expr = makeExpr(Expr::Kind::Query, name.line);
            expr->text = name.text;
            return expr;
        }
        if (match(Token::Kind::PlusPlus) || match(Token::Kind::MinusMinus)) {
            Token op = tokens_[pos_ - 1];
            auto expr = makeExpr(op.kind == Token::Kind::PlusPlus ? Expr::Kind::PostInc : Expr::Kind::PostDec, op.line);
            expr->prefix = true;
            expr->kids.push_back(parseUnary());
            return expr;
        }
        if (check(Token::Kind::Minus) && pos_ + 1 < tokens_.size() &&
            tokens_[pos_ + 1].kind == Token::Kind::Int && tokens_[pos_ + 1].text == "2147483648") {
            int line = peek().line;
            pos_ += 2;
            auto expr = makeExpr(Expr::Kind::Int, line);
            expr->intValue = std::numeric_limits<int>::min();
            return expr;
        }
        if (match(Token::Kind::Bang) || match(Token::Kind::KwNot) || match(Token::Kind::Minus) || match(Token::Kind::Tilde) ||
            match(Token::Kind::Amp) || match(Token::Kind::Star)) {
            Token op = tokens_[pos_ - 1];
            if (op.kind == Token::Kind::Star) {
                auto inner = parseUnary();
                // *n++ — это (*n)++, меняется значение по ссылке, а не «указатель».
                // Запись «сдвинуть указатель» этим синтаксисом сделать нельзя.
                if (inner->kind == Expr::Kind::PostInc || inner->kind == Expr::Kind::PostDec) {
                    auto deref = makeExpr(Expr::Kind::Deref, op.line);
                    deref->kids.push_back(std::move(inner->kids[0]));
                    inner->kids[0] = std::move(deref);
                    return inner;
                }
                auto expr = makeExpr(Expr::Kind::Deref, op.line);
                expr->kids.push_back(std::move(inner));
                return expr;
            }
            if (op.kind == Token::Kind::Amp) {
                auto expr = makeExpr(Expr::Kind::Addr, op.line);
                expr->kids.push_back(parseUnary());
                return expr;
            }
            auto expr = makeExpr(Expr::Kind::Unary, op.line);
            if (op.kind == Token::Kind::Minus) expr->unary = UnaryOp::Neg;
            else if (op.kind == Token::Kind::Tilde) expr->unary = UnaryOp::BitNot;
            else expr->unary = UnaryOp::Not;
            expr->kids.push_back(parseUnary());
            return expr;
        }
        return parsePostfix();
    }

    std::unique_ptr<Expr> parsePostfix() {
        auto expr = parsePrimary();
        while (true) {
            if (match(Token::Kind::LParen)) {
                auto call = makeExpr(Expr::Kind::Call, expr->line);
                call->kids.push_back(std::move(expr));
                if (!check(Token::Kind::RParen)) {
                    do {
                        call->kids.push_back(parseExpr());
                    } while (match(Token::Kind::Comma));
                }
                consume(Token::Kind::RParen, "')'");
                expr = std::move(call);
                continue;
            }
            if (match(Token::Kind::LBracket)) {
                auto index = makeExpr(Expr::Kind::Index, expr->line);
                index->kids.push_back(std::move(expr));
                index->kids.push_back(parseExpr());
                consume(Token::Kind::RBracket, "']'");
                expr = std::move(index);
                continue;
            }
            if (match(Token::Kind::LChar)) {
                auto index = makeExpr(Expr::Kind::CharIndex, expr->line);
                index->kids.push_back(std::move(expr));
                index->kids.push_back(parseExpr());
                consume(Token::Kind::RChar, "']]'");
                expr = std::move(index);
                continue;
            }
            if (match(Token::Kind::Dot)) {
                Token name = consume(Token::Kind::Identifier, "property name");
                auto member = makeExpr(Expr::Kind::Member, name.line);
                member->text = name.text;
                member->kids.push_back(std::move(expr));
                expr = std::move(member);
                continue;
            }
            if (match(Token::Kind::PlusPlus) || match(Token::Kind::MinusMinus)) {
                Token op = tokens_[pos_ - 1];
                auto updated = makeExpr(op.kind == Token::Kind::PlusPlus ? Expr::Kind::PostInc : Expr::Kind::PostDec, op.line);
                updated->kids.push_back(std::move(expr));
                expr = std::move(updated);
                continue;
            }
            break;
        }
        return expr;
    }

    std::unique_ptr<Expr> parsePrimary() {
        Token token = peek();
        if (match(Token::Kind::Int)) {
            if (token.text == "2147483648") {
                throw RError("integer literal out of range in " + name_, token.line);
            }
            auto expr = makeExpr(Expr::Kind::Int, token.line);
            expr->intValue = token.intValue;
            return expr;
        }
        if (match(Token::Kind::Float)) {
            auto expr = makeExpr(Expr::Kind::Float, token.line);
            expr->floatValue = token.floatValue;
            return expr;
        }
        if (match(Token::Kind::String)) {
            auto expr = makeExpr(Expr::Kind::String, token.line);
            expr->text = token.text;
            while (true) {
                size_t look = pos_;
                while (look < tokens_.size() && tokens_[look].kind == Token::Kind::Newline) ++look;
                if (look >= tokens_.size() || tokens_[look].kind != Token::Kind::String) break;
                pos_ = look + 1;
                expr->text += tokens_[look].text;
            }
            return expr;
        }
        if (match(Token::Kind::KwTrue) || match(Token::Kind::KwFalse)) {
            auto expr = makeExpr(Expr::Kind::Int, token.line);
            expr->intValue = token.kind == Token::Kind::KwTrue ? 1 : 0;
            return expr;
        }
        if (match(Token::Kind::KwVoid)) {
            auto expr = makeExpr(Expr::Kind::String, token.line);
            return expr;
        }
        if (match(Token::Kind::KwNull)) {
            return makeExpr(Expr::Kind::Null, token.line);
        }
        if (match(Token::Kind::KwPointer)) {
            auto expr = makeExpr(Expr::Kind::Null, token.line);
            expr->intValue = 1;
            return expr;
        }
        if (check(Token::Kind::Identifier) && token.text == "newobj") {
            size_t look = pos_ + 1;
            while (look < tokens_.size() && tokens_[look].kind == Token::Kind::Newline) {
                ++look;
            }
            if (look < tokens_.size() && tokens_[look].kind == Token::Kind::Lt) {
                ++pos_;
                consume(Token::Kind::Lt, "'<'");
                Token type = consume(Token::Kind::Identifier, "type name");
                consume(Token::Kind::Gt, "'>'");
                consume(Token::Kind::LParen, "'('");
                consume(Token::Kind::RParen, "')'");
                auto expr = makeExpr(Expr::Kind::NewObj, token.line);
                expr->text = type.text;
                return expr;
            }
        }
        if (check(Token::Kind::KwVar) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == Token::Kind::LParen) {
            Token name = tokens_[pos_++];
            auto expr = makeExpr(Expr::Kind::Name, name.line);
            expr->text = "var";
            return expr;
        }
        if ((check(Token::Kind::KwStr) || check(Token::Kind::KwInt) || check(Token::Kind::KwFloat)) &&
            pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == Token::Kind::LParen) {
            Token name = tokens_[pos_++];
            auto expr = makeExpr(Expr::Kind::Name, name.line);
            expr->text = name.kind == Token::Kind::KwInt ? "int" : name.kind == Token::Kind::KwFloat ? "float" : "str";
            return expr;
        }
        if (match(Token::Kind::Identifier)) {
            auto expr = makeExpr(Expr::Kind::Name, token.line);
            expr->text = token.text;
            return expr;
        }
        if (match(Token::Kind::LParen)) {
            auto expr = parseExpr();
            consume(Token::Kind::RParen, "')'");
            return expr;
        }
        if (match(Token::Kind::LBrace)) {
            auto expr = makeExpr(Expr::Kind::List, token.line);
            ++braceExpr_;
            if (!check(Token::Kind::RBrace)) {
                do {
                    if (check(Token::Kind::RBrace)) {
                        break;
                    }
                    expr->kids.push_back(parseExpr());
                } while (match(Token::Kind::Comma));
            }
            consume(Token::Kind::RBrace, "'}'");
            --braceExpr_;
            return expr;
        }
        if (match(Token::Kind::LBracket)) {
            auto expr = makeExpr(Expr::Kind::List, token.line);
            if (!check(Token::Kind::RBracket)) {
                do {
                    expr->kids.push_back(parseExpr());
                } while (match(Token::Kind::Comma));
            }
            consume(Token::Kind::RBracket, "']'");
            return expr;
        }
        throw RError("expected expression in " + name_ + ", got " + spell(token), token.line);
    }

    // Куда можно присваивать. Call в этом списке — не «результат вызова»,
    // а var(индекс): вызов возвращает ссылку, и StoreRef пишет через неё.
    static bool assignable(const Expr& expr) {
        return expr.kind == Expr::Kind::Name || expr.kind == Expr::Kind::Index || expr.kind == Expr::Kind::CharIndex ||
               expr.kind == Expr::Kind::Member || expr.kind == Expr::Kind::Deref || expr.kind == Expr::Kind::Call;
    }

    static std::unique_ptr<Expr> makeExpr(Expr::Kind kind, int line) {
        auto expr = std::make_unique<Expr>();
        expr->kind = kind;
        expr->line = line;
        return expr;
    }

    static std::unique_ptr<Stmt> makeStmt(Stmt::Kind kind, int line) {
        auto stmt = std::make_unique<Stmt>();
        stmt->kind = kind;
        stmt->line = line;
        return stmt;
    }

    static std::string spell(const Token& token) {
        switch (token.kind) {
        case Token::Kind::End: return "end";
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

Program parse(std::string_view source, std::string_view name) {
    return Parser(lex(source, name), std::string(name)).run();
}

}  // пространство имён sme
