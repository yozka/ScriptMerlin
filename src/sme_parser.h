#pragma once

#include "sme_ast.h"
#include "sme_lexer.h"

#include <string_view>

namespace sme {

// Рекурсивный спуск: программа = глобалы и функции.
// Перевод строки внутри (...), [...] и {...} в выражениях не заканчивает оператор.
Program parse(std::string_view source, std::string_view name);

}  // пространство имён sme
