#pragma once

#include "sme_ast.h"
#include "sme/sme_error.h"

#include <string_view>

namespace sme {

// Рекурсивный спуск: программа = глобалы и функции.
// Перевод строки внутри (...), [...] и {...} в выражениях не заканчивает оператор.
error::Result<Program> parse(std::string_view source, std::string_view name);

}  // пространство имён sme
