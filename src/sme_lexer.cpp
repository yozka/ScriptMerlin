#include "sme_lexer.h"

#include "sme/sme_error.h"

// Лексер. На входе текст после препроцессора, на выходе поток токенов.
// Перевод строки значим, поэтому он не схлопывается в пробел.
// Внутри строки, символа и обратных кавычек комментарий не начинается.
// Если на границе токена уже был перевод строки, следующий токен откладывается
// в _held: один next() отдаёт Newline, повторный — сам токен.

#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace sme {
namespace {

// Ключевые слова. tab/ref/ptr намеренно не здесь: tab(...) — вызов функции,
// а не объявление. int/float/str остаются словами и отдельно распознаются как вызов.
const std::unordered_map<std::string, Token::Kind> kKeywords = {
    {"func", Token::Kind::KwFunc},
    {"var", Token::Kind::KwVar},
    {"if", Token::Kind::KwIf},
    {"else", Token::Kind::KwElse},
    {"while", Token::Kind::KwWhile},
    {"for", Token::Kind::KwFor},
    {"do", Token::Kind::KwDo},
    {"return", Token::Kind::KwReturn},
    {"break", Token::Kind::KwBreak},
    {"continue", Token::Kind::KwContinue},
    {"goto", Token::Kind::KwGoto},
    {"stop", Token::Kind::KwStop},
    {"true", Token::Kind::KwTrue},
    {"false", Token::Kind::KwFalse},
    {"null", Token::Kind::KwNull},
    {"NUL", Token::Kind::KwNull},
    {"VOID", Token::Kind::KwVoid},
    {"NULL", Token::Kind::KwPointer},
    {"breakpoint", Token::Kind::KwBreakpoint},
    {"and", Token::Kind::KwAnd},
    {"or", Token::Kind::KwOr},
    {"not", Token::Kind::KwNot},
    {"int", Token::Kind::KwInt},
    {"float", Token::Kind::KwFloat},
    {"str", Token::Kind::KwStr},
};

class Lexer {
public:
    Lexer(std::string_view source, std::string_view name)
        : _source(source)
        , _name(name) {}

    error::Result<std::vector<Token>> Run() {
        if (_source.size() >= 3 &&
            static_cast<unsigned char>(_source[0]) == 0xEF &&
            static_cast<unsigned char>(_source[1]) == 0xBB &&
            static_cast<unsigned char>(_source[2]) == 0xBF) {
            _pos = 3; // BOM UTF-8, на токены не влияет
        }
        std::vector<Token> tokens;
        while (!_error) {
            Token token = Next();
            if (token.kind != Token::Kind::Invalid) {
                tokens.push_back(token);
            }
            if (token.kind == Token::Kind::End) {
                break;
            }
        }
        if (tokens.empty() || tokens.back().kind != Token::Kind::End) {
            tokens.push_back(make(Token::Kind::End));
        }
        return {std::move(tokens), std::move(_error)};
    }

private:
    std::string_view _source;
    std::string _name;
    size_t _pos = 0;
    int _line = 1;
    bool _hasHeld = false;
    Token _held; // токен, отложенный, потому что перед ним стоял перевод строки
    error::Error::Opt _error;

    void Report(error::Code code, std::string message, int line) {
        if (!_error) {
            error::Location location;
            location.source = _name;
            location.line = line;
            _error.emplace(code, std::move(message), std::move(location));
        }
    }

    Token Invalid() const { return make(Token::Kind::Invalid); }

    bool Eof() const { return _pos >= _source.size(); }

    char Peek(size_t ahead = 0) const {
        if (_pos + ahead >= _source.size()) {
            return '\0';
        }
        return _source[_pos + ahead];
    }

    char Get() {
        if (Eof()) {
            return '\0';
        }
        char c = _source[_pos++];
        if (c == '\n') {
            ++_line;
        }
        return c;
    }

    // Пробелы и комментарии. Незакрытый /* без кода до конца файла молча
    // съедается: так устроен хвост некоторых старых скриптов. Если после /*
    // ещё есть код, это ошибка — иначе файл компилировался бы без хвоста.
    bool SkipTrivia(int& newlineLine) {
        bool newline = false;
        while (!Eof()) {
            char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r') {
                Get();
                continue;
            }
            if (c == '\n') {
                newline = true;
                newlineLine = _line;
                Get();
                continue;
            }
            if (c == '/' && Peek(1) == '/') {
                while (!Eof() && Peek() != '\n') {
                    Get();
                }
                continue;
            }
            if (c == '/' && Peek(1) == '*') {
                int start = _line;
                Get();
                Get();
                bool ateCode = false;
                bool closed = false;
                while (!Eof()) {
                    if (Peek() == '*' && Peek(1) == '/') {
                        Get();
                        Get();
                        closed = true;
                        break;
                    }
                    char body = Get();
                    if (body != ' ' && body != '\t' && body != '\r' && body != '\n') {
                        ateCode = true;
                    }
                }
                if (!closed && ateCode) {
                    Report(error::Code::LEXING_UNTERMINATED_COMMENT,
                        "unterminated comment in " + _name, start);
                }
                continue;
            }
            break;
        }
        return newline;
    }

    Token make(Token::Kind kind, std::string text = {}) const {
        Token token;
        token.kind = kind;
        token.line = _line;
        token.text = std::move(text);
        return token;
    }

    Token At(int line, Token::Kind kind) const {
        Token token = make(kind);
        token.line = line;
        return token;
    }

    Token NewlineToken(int line) const {
        Token token = make(Token::Kind::Newline);
        token.line = line;
        return token;
    }

    Token Next() {
        if (_hasHeld) {
            _hasHeld = false;
            return _held;
        }
        int newlineLine = _line;
        bool sawNewline = SkipTrivia(newlineLine);
        if (Eof()) {
            Token end = make(Token::Kind::End);
            if (sawNewline) {
                _held = end;
                _hasHeld = true;
                return NewlineToken(newlineLine);
            }
            return end;
        }
        Token token = ScanToken();
        if (sawNewline) {
            _held = token;
            _hasHeld = true;
            return NewlineToken(newlineLine);
        }
        return token;
    }

    Token ScanToken() {
        int line = _line;
        char c = Peek();
        if (c == '"') {
            return ScanString(line);
        }
        if (c == '\'' || c == '`') {
            return c == '\'' ? ScanChar(line) : ScanString(line, '`');
        }
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(Peek(1))))) {
            return ScanNumber(line);
        }
        if (IsIdentStart(static_cast<unsigned char>(c))) {
            return ScanIdent(line);
        }

        Get();
        auto eqSuffix = [&](Token::Kind plain, Token::Kind withEq) {
            if (Peek() == '=') {
                Get();
                return At(line, withEq);
            }
            return At(line, plain);
        };
        switch (c) {
        case '(': return At(line, Token::Kind::LParen);
        case ')': return At(line, Token::Kind::RParen);
        case '{': return At(line, Token::Kind::LBrace);
        case '}': return At(line, Token::Kind::RBrace);
        case '[':
            if (Peek() == '[') {
                Get();
                return At(line, Token::Kind::LChar);
            }
            return At(line, Token::Kind::LBracket);
        case ']':
            if (Peek() == ']') {
                Get();
                return At(line, Token::Kind::RChar);
            }
            return At(line, Token::Kind::RBracket);
        case ',': return At(line, Token::Kind::Comma);
        case ';': return At(line, Token::Kind::Semicolon);
        case '~': return At(line, Token::Kind::Tilde);
        case '?': return At(line, Token::Kind::Question);
        case ':': return At(line, Token::Kind::Colon);
        case '.':
            if (Peek() == '.') {
                Get();
                return At(line, Token::Kind::DotDot);
            }
            return At(line, Token::Kind::Dot);
        case '+':
            if (Peek() == '+') { Get(); return At(line, Token::Kind::PlusPlus); }
            return eqSuffix(Token::Kind::Plus, Token::Kind::PlusEq);
        case '-':
            if (Peek() == '-') { Get(); return At(line, Token::Kind::MinusMinus); }
            return eqSuffix(Token::Kind::Minus, Token::Kind::MinusEq);
        case '*': return eqSuffix(Token::Kind::Star, Token::Kind::StarEq);
        case '/': return eqSuffix(Token::Kind::Slash, Token::Kind::SlashEq);
        case '%': return eqSuffix(Token::Kind::Percent, Token::Kind::PercentEq);
        case '^': return eqSuffix(Token::Kind::Caret, Token::Kind::CaretEq);
        case '!':
            if (Peek() == '=') { Get(); return At(line, Token::Kind::Ne); }
            return At(line, Token::Kind::Bang);
        case '=':
            if (Peek() == '=') { Get(); return At(line, Token::Kind::Eq); }
            return At(line, Token::Kind::Assign);
        case '<':
            if (Peek() == '=') { Get(); return At(line, Token::Kind::Le); }
            if (Peek() == '<') {
                Get();
                if (Peek() == '=') { Get(); return At(line, Token::Kind::ShlEq); }
                return At(line, Token::Kind::Shl);
            }
            return At(line, Token::Kind::Lt);
        case '>':
            if (Peek() == '=') { Get(); return At(line, Token::Kind::Ge); }
            if (Peek() == '>') {
                Get();
                if (Peek() == '=') { Get(); return At(line, Token::Kind::ShrEq); }
                return At(line, Token::Kind::Shr);
            }
            return At(line, Token::Kind::Gt);
        case '&':
            if (Peek() == '&') { Get(); return At(line, Token::Kind::AndAnd); }
            return eqSuffix(Token::Kind::Amp, Token::Kind::AmpEq);
        case '|':
            if (Peek() == '|') { Get(); return At(line, Token::Kind::OrOr); }
            return eqSuffix(Token::Kind::Pipe, Token::Kind::PipeEq);
        default:
            Report(error::Code::LEXING_UNEXPECTED_CHARACTER,
                std::string("unexpected character '") + c + "' in " + _name, line);
            return Invalid();
        }
    }

    static bool IsIdentStart(unsigned char c) {
        return std::isalpha(c) || c == '_' || c >= 128;
    }

    static bool IsIdentCont(unsigned char c) {
        return std::isalnum(c) || c == '_' || c >= 128;
    }

    Token ScanIdent(int line) {
        std::string text;
        while (IsIdentCont(static_cast<unsigned char>(Peek()))) {
            text.push_back(Get());
        }
        Token token = At(line, Token::Kind::Identifier);
        token.text = text;
        if (auto it = kKeywords.find(text); it != kKeywords.end()) {
            token.kind = it->second;
        }
        return token;
    }

    // 0x и 0b укладываются в 32 бита и читаются как знаковый int:
    // 0xFFFFFFFF — это -1, значение длиннее 32 бит — ошибка.
    // Десятичное 2147483648 запоминается текстом: унарный минус перед ним
    // в парсере становится INT_MIN, само по себе число вне диапазона.
    Token ScanNumber(int line) {
        if (Peek() == '0' && (Peek(1) == 'x' || Peek(1) == 'X')) {
            Get();
            Get();
            std::string digits;
            while (std::isxdigit(static_cast<unsigned char>(Peek()))) {
                digits.push_back(Get());
            }
            if (digits.empty()) {
                Report(error::Code::LEXING_HEX_LITERAL_WITHOUT_DIGITS,
                    "hex literal has no digits in " + _name, line);
                return Invalid();
            }
            uint32_t value = 0;
            bool outOfRange = false;
            for (char digit : digits) {
                uint32_t nibble = 0;
                if (digit >= '0' && digit <= '9') nibble = static_cast<uint32_t>(digit - '0');
                else if (digit >= 'a' && digit <= 'f') nibble = static_cast<uint32_t>(digit - 'a' + 10);
                else nibble = static_cast<uint32_t>(digit - 'A' + 10);
                if (value > (std::numeric_limits<uint32_t>::max() >> 4)) {
                    outOfRange = true;
                    continue;
                }
                value = (value << 4) | nibble;
            }
            if (outOfRange) {
                Report(error::Code::LEXING_INTEGER_OUT_OF_RANGE,
                    "integer literal out of range in " + _name, line);
                return Invalid();
            }
            Token token = At(line, Token::Kind::Int);
            token.intValue = static_cast<int>(value);
            return token;
        }
        if (Peek() == '0' && (Peek(1) == 'b' || Peek(1) == 'B')) {
            Get();
            Get();
            uint32_t value = 0;
            int bits = 0;
            bool outOfRange = false;
            while (Peek() == '0' || Peek() == '1') {
                uint32_t bit = Get() == '1' ? 1u : 0u;
                if (value > (std::numeric_limits<uint32_t>::max() >> 1)) {
                    outOfRange = true;
                    continue;
                }
                value = (value << 1) | bit;
                ++bits;
            }
            if (bits == 0) {
                Report(error::Code::LEXING_BINARY_LITERAL_WITHOUT_DIGITS,
                    "binary literal has no digits in " + _name, line);
                return Invalid();
            }
            if (outOfRange) {
                Report(error::Code::LEXING_INTEGER_OUT_OF_RANGE,
                    "integer literal out of range in " + _name, line);
                return Invalid();
            }
            Token token = At(line, Token::Kind::Int);
            token.intValue = static_cast<int>(value);
            return token;
        }

        std::string text;
        bool isFloat = false;
        while (std::isdigit(static_cast<unsigned char>(Peek()))) {
            text.push_back(Get());
        }
        if (Peek() == '.' && Peek(1) != '.') {
            isFloat = true;
            text.push_back(Get());
            while (std::isdigit(static_cast<unsigned char>(Peek()))) {
                text.push_back(Get());
            }
        }
        if (Peek() == 'e' || Peek() == 'E') {
            isFloat = true;
            text.push_back(Get());
            if (Peek() == '+' || Peek() == '-') text.push_back(Get());
            if (!std::isdigit(static_cast<unsigned char>(Peek()))) {
                Report(error::Code::LEXING_EXPONENT_WITHOUT_DIGITS,
                    "exponent has no digits in " + _name, line);
                return Invalid();
            }
            while (std::isdigit(static_cast<unsigned char>(Peek()))) {
                text.push_back(Get());
            }
        }
        Token token = At(line, isFloat ? Token::Kind::Float : Token::Kind::Int);
        token.text = text;
        if (isFloat) {
            const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(),
                token.floatValue, std::chars_format::general);
            if (status != std::errc{} || end != text.data() + text.size()) {
                Report(error::Code::LEXING_NUMERIC_LITERAL_OUT_OF_RANGE,
                    "numeric literal out of range in " + _name, line);
                return Invalid();
            }
        } else {
            long long value = 0;
            const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
            if (status != std::errc{} || end != text.data() + text.size()) {
                Report(error::Code::LEXING_NUMERIC_LITERAL_OUT_OF_RANGE,
                    "numeric literal out of range in " + _name, line);
                return Invalid();
            }
            if (value == 2147483648LL) {
                token.intValue = 0;
                token.text = "2147483648";
            } else if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
                Report(error::Code::LEXING_INTEGER_OUT_OF_RANGE,
                    "integer literal out of range in " + _name, line);
                return Invalid();
            } else {
                token.intValue = static_cast<int>(value);
            }
        }
        return token;
    }

    Token ScanChar(int line) {
        Get();
        if (Eof() || Peek() == '\n') {
            Report(error::Code::LEXING_UNTERMINATED_CHARACTER,
                "unterminated character in " + _name, line);
            return Invalid();
        }
        char value = Get();
        if (value == '\\') {
            if (Eof()) {
                Report(error::Code::LEXING_UNTERMINATED_CHARACTER,
                    "unterminated character in " + _name, line);
                return Invalid();
            }
            char esc = Get();
            switch (esc) {
            case 'n': value = '\n'; break;
            case 't': value = '\t'; break;
            case 'r': value = '\r'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            case '0': value = '\0'; break;
            default: value = esc; break;
            }
        }
        if (Peek() != '\'') {
            Report(error::Code::LEXING_UNTERMINATED_CHARACTER,
                "unterminated character in " + _name, line);
            return Invalid();
        }
        Get();
        Token token = At(line, Token::Kind::Int);
        token.intValue = static_cast<unsigned char>(value);
        return token;
    }

    Token ScanString(int line, char quote = '"') {
        Get();
        std::string text;
        while (!Eof() && Peek() != quote) {
            char c = Get();
            if (c == '\n') {
                Report(error::Code::LEXING_UNTERMINATED_STRING,
                    "unterminated string in " + _name, line);
                return Invalid();
            }
            if (c == '\\') {
                if (Eof()) {
                    Report(error::Code::LEXING_UNTERMINATED_STRING,
                        "unterminated string in " + _name, line);
                    return Invalid();
                }
                char esc = Get();
                switch (esc) {
                case 'n': text.push_back('\n'); break;
                case 't': text.push_back('\t'); break;
                case 'r': text.push_back('\r'); break;
                case '\\': text.push_back('\\'); break;
                case '"': text.push_back('"'); break;
                case '0': text.push_back('\0'); break;
                default: text.push_back(esc); break;
                }
            } else {
                text.push_back(c);
            }
        }
        if (Peek() != quote) {
            Report(error::Code::LEXING_UNTERMINATED_STRING,
                "unterminated string in " + _name, line);
            return Invalid();
        }
        Get();
        Token token = At(line, Token::Kind::String);
        token.text = std::move(text);
        return token;
    }
};

}  // анонимное пространство имён

error::Result<std::vector<Token>> lex(std::string_view source, std::string_view name) {
    return Lexer(source, name).Run();
}

}  // пространство имён sme
