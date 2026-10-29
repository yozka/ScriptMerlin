#pragma once

#include <cstdint>
#include <ctime>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace sme::files {

// Режим открытия. load и #include читают через IFileSystem,
// чтобы хост мог подменить диск.
enum class OpenMode {
    Read,
    Write,
    ReadWrite,
    WriteRead
};

// Куда отсчитывать seek: от начала, от текущей позиции или от конца.
enum class Seek {
    Set,
    Cur,
    End
};

struct FileInfo {
    std::time_t writeTime = 0;
    int64_t size = 0;
};

class File {
public:
    virtual ~File() = default;

    virtual size_t read(std::span<std::byte> buffer) = 0;
    virtual size_t write(std::span<const std::byte> buffer) = 0;
    virtual bool seek(int64_t offset, Seek origin) = 0;
    virtual int64_t size() = 0;
    virtual int64_t tell() = 0;
};

class IFileSystem {
public:
    using Ptr = std::shared_ptr<IFileSystem>;

    virtual ~IFileSystem() = default;

    virtual std::optional<FileInfo> info(const std::string& path) = 0;
    virtual std::unique_ptr<File> open(const std::string& path, OpenMode mode) = 0;
    virtual bool remove(const std::string& path) = 0;
};

}  // пространство имён sme::files
