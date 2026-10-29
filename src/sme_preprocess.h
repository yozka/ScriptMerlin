#pragma once

#include "sme/sme_filesystem.h"

#include <string>
#include <string_view>
#include <vector>

namespace sme {

// Значение #def, которое видит хост через globalAt.
// В сам текст скрипта подставляется исходная строка макроса, и лексер
// читает её тем же разбором, которым получено это значение.
struct Literal {
    std::string name;
    enum class Kind { Null, Int, Float, String, Pointer } kind = Kind::Null;
    int integer = 0;
    float real = 0.f;
    std::string text;
};

// Результат препроцессора: текст уже без директив и с раскрытыми макросами.
struct SourceUnit {
    std::string source;
    std::vector<std::string> globals; // имена из пустого #def, значение null
    std::vector<Literal> literals;
    size_t scope = 0; // #scope, одно число на файл
};

SourceUnit preprocessFile(const files::IFileSystem::Ptr& fileSystem, const std::string& path);
SourceUnit preprocessSource(const files::IFileSystem::Ptr& fileSystem, std::string_view source, std::string_view name, const std::string& includeRoot);

}  // пространство имён sme
