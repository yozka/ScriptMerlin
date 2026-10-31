#pragma once

#include "sme/sme_filesystem.h"

#include <string>

namespace sme 
{

	// Пути для препроцессора. Файл ищется и читается через IFileSystem,
	// а каталог, имя и канонический ключ считаются здесь.
	std::string NormalizePath(const std::string& path);
	std::string ParentPath(const std::string& path);
	std::string JoinPath(const std::string& base, const std::string& relative);

}
