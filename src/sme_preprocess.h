#pragma once

#include "sme/sme_error.h"
#include "sme/sme_filesystem.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sme
{

	// Значение #def для compile-time подстановки. В исходный текст попадает
	// макрос, а пустое #def остаётся null-константой.
	struct Literal
	{
		std::string name;
		enum class Kind { Null, Int, Float, String, Pointer } kind = Kind::Null;
		int integer = 0;
		float real = 0.f;
		std::string text;
	};

	// Результат препроцессора: текст уже без директив и с раскрытыми макросами.
	struct SourceUnit
	{
		std::string source;
		std::vector<Literal> literals;
	};

	error::Result<SourceUnit> PreprocessFile(const files::IFileSystem::Ptr& files, const std::string& path);
	error::Result<SourceUnit> PreprocessSource(const files::IFileSystem::Ptr& files, std::string_view source, std::string_view name, const std::string& includeRoot);

}
