#pragma once

#include "sme/sme_filesystem.h"

#include <string>

namespace sme 
{

    // Диск через std::fstream. Движок ставит её, пока хост не вызовет useFileSystem.
    files::IFileSystem::Ptr makeStdFileSystem();

    // Пути для препроцессора. Файл ищется и читается через IFileSystem,
    // а каталог, имя и канонический ключ считаются здесь.
    std::string normalizePath(const std::string& path);
    std::string parentPath(const std::string& path);
    std::string joinPath(const std::string& base, const std::string& relative);

}  // пространство имён sme
