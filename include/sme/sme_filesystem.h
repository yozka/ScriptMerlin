#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>

namespace sme::files
{

	// Режим открытия. load и #include читают через IFileSystem,
	// чтобы хост мог подменить диск.
	enum class OpenMode
	{
		Read,
		Write,
		ReadWrite,
		WriteRead
	};


	// Куда отсчитывать seek: от начала, от текущей позиции или от конца.
	enum class Seek
	{
		Set,
		Cur,
		End
	};


	struct FileInfo
	{
		std::time_t writeTime = 0;
		int64_t size = 0;
	};


	class IFile
	{
	public:
		using UPtr = std::unique_ptr<IFile>;
		using Ptr = std::shared_ptr<IFile>;
		using FileInfo = std::optional<FileInfo>;
		
	public:
		virtual ~IFile() = default;
		
		virtual size_t Read(std::span<std::byte> buffer) = 0;
		virtual size_t Write(std::span<const std::byte> buffer) = 0;
		virtual bool Seek(int64_t offset, Seek origin) = 0;
		virtual int64_t Size() = 0;
		virtual int64_t Tell() = 0;
	};


	class IFileSystem
	{
	public:
		using UPtr = std::unique_ptr<IFileSystem>;
		using Ptr = std::shared_ptr<IFileSystem>;
	
	public:
		virtual ~IFileSystem() = default;
		
		virtual IFile::FileInfo FileInfo(const std::string& path) = 0;
		virtual IFile::UPtr Open(const std::string& path, OpenMode mode) = 0;
		virtual bool Remove(const std::string& path) = 0;
	};

}


namespace sme
{
	files::IFileSystem::UPtr MakeStdFileSystem();
}
