#pragma once

#include "sme_ast.h"
#include "sme/sme_error.h"

#include <string_view>

namespace sme {

struct ParseResult {
    Program program;
    error::Error::Opt error;
};

// Рекурсивный спуск: программа = глобалы и функции.
// Перевод строки внутри (...), [...] и {...} в выражениях не заканчивает оператор.
ParseResult parse(std::string_view source, std::string_view name);

}  // пространство имён sme
