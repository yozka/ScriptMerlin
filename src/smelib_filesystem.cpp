#include "smelib_filesystem.h"

// Реализация IFileSystem поверх fstream и строковая обработка путей.
// load и #include открывают файлы этим объектом, если хост не подменил диск.

#include <filesystem>
#include <fstream>
#include <utility>
#include <vector>

namespace sme
{
	namespace
	{

		std::string ToSlashSeparators(const std::string& path)
		{
			std::string result = path;
			for (char& value : result) {
				if (value == '\\') {
					value = '/';
				}
			}
			return result;
		}

		class StdFile : public files::IFile
		{
		public:
			StdFile(std::fstream stream) : _stream(std::move(stream)) {}
			
			size_t Read(std::span<std::byte> buffer) override
			{
				_stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
				if (_stream.bad()) {
					return 0;
				}
				return static_cast<size_t>(_stream.gcount());
			}
			
			size_t Write(std::span<const std::byte> buffer) override
			{
				_stream.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
				if (!_stream) {
					return 0;
				}
				return buffer.size();
			}
			
			bool Seek(int64_t offset, files::Seek origin) override
			{
				std::ios::seekdir dir = std::ios::beg;
				switch (origin) {
					case files::Seek::Set: dir = std::ios::beg; break;
					case files::Seek::Cur: dir = std::ios::cur; break;
					case files::Seek::End: dir = std::ios::end; break;
				}
				_stream.clear();
				_stream.seekg(static_cast<std::streamoff>(offset), dir);
				_stream.seekp(static_cast<std::streamoff>(offset), dir);
				return static_cast<bool>(_stream);
			}
			
			int64_t Size() override
			{
				auto current = Tell();
				_stream.clear();
				_stream.seekg(0, std::ios::end);
				auto end = _stream.tellg();
				_stream.clear();
				_stream.seekg(static_cast<std::streamoff>(current), std::ios::beg);
				if (end < 0) {
					return 0;
				}
				return static_cast<int64_t>(end);
			}
			
			int64_t Tell() override
			{
				auto pos = _stream.tellg();
				if (pos < 0) {
					return 0;
				}
				return static_cast<int64_t>(pos);
			}
			
		private:
			std::fstream _stream;
		};

		class StdFileSystem : public files::IFileSystem
		{
		public:
			files::IFile::FileInfo FileInfo(const std::string& path) override
			{
				std::error_code ec;
				const std::string normalized = ToSlashSeparators(path);
				if (!std::filesystem::exists(normalized, ec) || ec) {
					return std::nullopt;
				}
				files::FileInfo result;
				auto bytes = std::filesystem::file_size(normalized, ec);
				if (!ec) {
					result.size = static_cast<int64_t>(bytes);
				}
				return result;
			}
			
			files::IFile::UPtr Open(const std::string& path, files::OpenMode mode) override
			{
				std::ios::openmode flags = std::ios::binary;
				switch (mode) {
					case files::OpenMode::Read: flags |= std::ios::in; break;
					case files::OpenMode::Write: flags |= std::ios::out | std::ios::trunc; break;
					case files::OpenMode::ReadWrite: flags |= std::ios::in | std::ios::out; break;
					case files::OpenMode::WriteRead: flags |= std::ios::in | std::ios::out | std::ios::trunc; break;
				}
				std::fstream stream(ToSlashSeparators(path), flags);
				if (!stream.is_open()) {
					return nullptr;
				}
				return std::make_unique<StdFile>(std::move(stream));
			}
			
			bool Remove(const std::string& path) override
			{
				std::error_code ec;
				return std::filesystem::remove(ToSlashSeparators(path), ec) && !ec;
			}
		};

	}


	files::IFileSystem::UPtr MakeStdFileSystem()
	{
		return std::make_unique<StdFileSystem>();
	}

	namespace
	{

		bool IsSeparator(char value)
		{
			return value == '/' || value == '\\';
		}

		bool IsAbsolutePath(const std::string& path)
		{
			return !path.empty() && IsSeparator(path.front());
		}

	}


	// Ключ «этот файл уже включён». Не тот путь, которым файл открывают:
	// виртуальный диск может не знать канонический путь операционной системы.
	std::string NormalizePath(const std::string& path)
	{
		std::string prefix;
		size_t position = 0;
		bool rooted = false;
		if (!path.empty() && IsSeparator(path[0])) {
			prefix = "/";
			position = 1;
			rooted = true;
		}

		std::vector<std::string> components;
		while (position < path.size()) {
			while (position < path.size() && IsSeparator(path[position])) {
				++position;
			}
			const size_t start = position;
			while (position < path.size() && !IsSeparator(path[position])) {
				++position;
			}
			if (start == position) {
				continue;
			}
			std::string component = path.substr(start, position - start);
			if (component == "." || component.empty()) {
				continue;
			}
			if (component == "..") {
				if (!components.empty() && components.back() != "..") {
					components.pop_back();
				} else if (!rooted) {
					components.push_back(std::move(component));
				}
				continue;
			}
			components.push_back(std::move(component));
		}

		std::string result = prefix;
		for (const std::string& component : components) {
			if (!result.empty() && !IsSeparator(result.back())) {
				result.push_back('/');
			}
			result += component;
		}
		return result;
	}


	std::string ParentPath(const std::string& path)
	{
		std::string normalized = NormalizePath(path);
		while (normalized.size() > 1 && IsSeparator(normalized.back())) {
			normalized.pop_back();
		}
		size_t separator = std::string::npos;
		for (size_t index = normalized.size(); index > 0; --index) {
			if (IsSeparator(normalized[index - 1])) {
				separator = index - 1;
				break;
			}
		}
		if (separator == std::string::npos) {
			return {};
		}
		if (separator == 0) {
			return "/";
		}
		return normalized.substr(0, separator);
	}


	std::string JoinPath(const std::string& base, const std::string& relative)
	{
		if (IsAbsolutePath(relative)) {
			return NormalizePath(relative);
		}
		if (base.empty()) {
			return NormalizePath(relative);
		}
		std::string combined = base;
		if (!combined.empty() && !IsSeparator(combined.back())) {
			combined.push_back('/');
		}
		combined += relative;
		return NormalizePath(combined);
	}

}
