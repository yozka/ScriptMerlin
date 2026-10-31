#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sme {

// Токен после лексера. Перевод строки — отдельный токен: им заканчиваются
// return, break и присваивание, если нет точки с запятой.
// LChar/RChar — это скобки [[ и ]] для байта строки.
struct Token {
    enum class Kind {
        End,
        Newline,
        Identifier,
        Int,
        Float,
        String,
        LParen, RParen, LBrace, RBrace, LBracket, RBracket, LChar, RChar,
        Comma, Semicolon, Dot, DotDot, Colon, Question,
        Plus, Minus, Star, Slash, Percent,
        PlusPlus, MinusMinus,
        PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
        Eq, Ne, Lt, Le, Gt, Ge, Assign,
        AndAnd, OrOr, Bang, Tilde,
        Amp, Pipe, Caret, Shl, Shr,
        AmpEq, PipeEq, CaretEq, ShlEq, ShrEq,
        KwFunc, KwVar, KwIf, KwElse, KwWhile, KwFor, KwDo,
        KwReturn, KwBreak, KwContinue, KwGoto, KwStop,
        KwTrue, KwFalse, KwNull, KwPointer, KwAnd, KwOr, KwNot,
        KwInt, KwFloat, KwStr, KwVoid, KwBreakpoint
    };

    Kind kind = Kind::End;
    int line = 1;
    std::string text;
    int intValue = 0;
    float floatValue = 0.f;
};

// Разобрать исходник в токены. Комментарии здесь уже не режутся,
// если текст прошёл препроцессор: тот снимает их сам.
std::vector<Token> lex(std::string_view source, std::string_view name);

}  // пространство имён sme
