#include "smelib_filesystem.h"

// Реализация IFileSystem поверх fstream и разбор путей через std::filesystem.
// load и #include открывают файлы этим объектом, если хост не подменил диск.

#include <filesystem>
#include <fstream>
#include <utility>

namespace sme {
namespace {

class StdFile : public files::File {
public:
    StdFile(std::fstream stream) : stream_(std::move(stream)) {}

    size_t read(std::span<std::byte> buffer) override {
        stream_.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (stream_.bad()) {
            return 0;
        }
        return static_cast<size_t>(stream_.gcount());
    }

    size_t write(std::span<const std::byte> buffer) override {
        stream_.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (!stream_) {
            return 0;
        }
        return buffer.size();
    }

    bool seek(int64_t offset, files::Seek origin) override {
        std::ios::seekdir dir = std::ios::beg;
        switch (origin) {
        case files::Seek::Set: dir = std::ios::beg; break;
        case files::Seek::Cur: dir = std::ios::cur; break;
        case files::Seek::End: dir = std::ios::end; break;
        }
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset), dir);
        stream_.seekp(static_cast<std::streamoff>(offset), dir);
        return static_cast<bool>(stream_);
    }

    int64_t size() override {
        auto current = tell();
        stream_.clear();
        stream_.seekg(0, std::ios::end);
        auto end = stream_.tellg();
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(current), std::ios::beg);
        if (end < 0) {
            return 0;
        }
        return static_cast<int64_t>(end);
    }

    int64_t tell() override {
        auto pos = stream_.tellg();
        if (pos < 0) {
            return 0;
        }
        return static_cast<int64_t>(pos);
    }

private:
    std::fstream stream_;
};

class StdFileSystem : public files::IFileSystem {
public:
    std::optional<files::FileInfo> info(const std::string& path) override {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) {
            return std::nullopt;
        }
        files::FileInfo result;
        auto bytes = std::filesystem::file_size(path, ec);
        if (!ec) {
            result.size = static_cast<int64_t>(bytes);
        }
        return result;
    }

    std::unique_ptr<files::File> open(const std::string& path, files::OpenMode mode) override {
        std::ios::openmode flags = std::ios::binary;
        switch (mode) {
        case files::OpenMode::Read: flags |= std::ios::in; break;
        case files::OpenMode::Write: flags |= std::ios::out | std::ios::trunc; break;
        case files::OpenMode::ReadWrite: flags |= std::ios::in | std::ios::out; break;
        case files::OpenMode::WriteRead: flags |= std::ios::in | std::ios::out | std::ios::trunc; break;
        }
        std::fstream stream(path, flags);
        if (!stream.is_open()) {
            return nullptr;
        }
        return std::make_unique<StdFile>(std::move(stream));
    }

    bool remove(const std::string& path) override {
        std::error_code ec;
        return std::filesystem::remove(path, ec) && !ec;
    }
};

}  // анонимное пространство имён

files::IFileSystem::Ptr makeStdFileSystem() {
    return std::make_shared<StdFileSystem>();
}

// Ключ «этот файл уже включён». Не тот путь, которым файл открывают:
// виртуальный диск может не знать канонический путь операционной системы.
std::string normalizePath(const std::string& path) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec) {
        canonical = std::filesystem::absolute(path);
    }
    return canonical.string();
}

std::string parentPath(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

std::string joinPath(const std::string& base, const std::string& relative) {
    std::filesystem::path rel(relative);
    if (base.empty() || rel.is_absolute()) {
        return rel.string();
    }
    return (std::filesystem::path(base) / rel).string();
}

}  // пространство имён sme
