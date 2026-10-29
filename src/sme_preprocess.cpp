#include "sme_preprocess.h"

#include "sme_lexer.h"
#include "smelib_filesystem.h"
#include "sme/sme_error.h"

// Препроцессор. Директивы — отдельные строки: #include, #def, #ifdef/#ifndef,
// #scope, #abort. #ifdef однострочный: берётся следующая непустая строка,
// блока и #else нет. #def одновременно макрос для подстановки и начальное
// значение глобала. Текст значения разбирает тот же лексер, что и скрипт,
// чтобы 0b, 1e2 и ведущий ноль не разъехались.
// #include ищет файл в переданной IFileSystem: рядом с текущим и в корне юнита.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace sme {
namespace {

bool isIdentStart(unsigned char c) {
    return std::isalpha(c) || c == '_' || c >= 128;
}

bool isIdentCont(unsigned char c) {
    return std::isalnum(c) || c == '_' || c >= 128;
}

// Снимает // и /* */ до разбора директив. Строками считаются ", ' и `.
// Иначе URL в обратных кавычках `http://...` обрежется комментарием.
std::string stripComments(std::string_view source) {
    std::string out;
    out.reserve(source.size());
    int line = 1;
    for (size_t i = 0; i < source.size();) {
        char c = source[i];
        if (c == '"' || c == '\'' || c == '`') {
            char quote = c;
            out.push_back(c);
            ++i;
            while (i < source.size()) {
                char body = source[i];
                out.push_back(body);
                ++i;
                if (body == '\n') {
                    ++line;
                    break;
                }
                if (body == '\\' && i < source.size()) {
                    if (source[i] == '\n') ++line;
                    out.push_back(source[i]);
                    ++i;
                    continue;
                }
                if (body == quote) break;
            }
            continue;
        }
        if (c == '\n') {
            ++line;
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            int start = line;
            i += 2;
            bool ateCode = false;
            bool closed = false;
            while (i < source.size()) {
                if (source[i] == '*' && i + 1 < source.size() && source[i + 1] == '/') {
                    i += 2;
                    closed = true;
                    break;
                }
                if (source[i] == '\n') {
                    out.push_back('\n');
                    ++line;
                } else if (source[i] != ' ' && source[i] != '\t' && source[i] != '\r') {
                    ateCode = true;
                }
                ++i;
            }
            if (!closed && ateCode) {
                throw RError("unterminated comment", start);
            }
            continue;
        }
        out.push_back(c);
        ++i;
    }
    return out;
}

std::string readText(const files::IFileSystem::Ptr& fileSystem, const std::string& path) {
    if (!fileSystem) {
        throw RError("file system is empty");
    }
    std::unique_ptr<files::File> file = fileSystem->open(path, files::OpenMode::Read);
    if (!file) {
        throw RError("cannot open '" + path + "'");
    }
    std::string text;
    std::byte buffer[4096];
    for (;;) {
        const size_t n = file->read(std::span<std::byte>(buffer, sizeof buffer));
        if (n == 0) {
            break;
        }
        text.append(reinterpret_cast<const char*>(buffer), n);
    }
    return text;
}

bool startsWithWord(const std::string& text, const char* word) {
    const size_t n = std::char_traits<char>::length(word);
    if (text.compare(0, n, word) != 0) {
        return false;
    }
    return text.size() == n || std::isspace(static_cast<unsigned char>(text[n]));
}

std::string trim(std::string text) {
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

class Preprocessor {
public:
    Preprocessor(const files::IFileSystem::Ptr& fileSystem, std::string root)
        : files_(fileSystem), root_(std::move(root)) {
        defines_.emplace("TYPE_NUL", "0");
        defines_.emplace("TYPE_INT", "1");
        defines_.emplace("TYPE_FLT", "2");
        defines_.emplace("TYPE_STR", "3");
        defines_.emplace("TYPE_TAB", "4");
    }

    SourceUnit run(const std::string& file) {
        process(file);
        return finish();
    }

    SourceUnit runText(std::string_view source, std::string_view name) {
        expandStream(stripComments(source), std::string(name));
        return finish();
    }

private:
    const files::IFileSystem::Ptr& files_;
    std::string root_;
    std::string current_;
    std::unordered_map<std::string, std::string> defines_;
    std::unordered_set<std::string> included_;
    std::vector<std::string> globals_;
    std::unordered_set<std::string> globalNames_;
    std::vector<Literal> literals_;
    size_t scope_ = 0;
    bool abort_ = false;
    std::string out_;

    SourceUnit finish() const {
        return SourceUnit{out_, globals_, literals_, scope_};
    }

    bool defined(const std::string& name) const {
        return defines_.contains(name) || globalNames_.contains(name);
    }

    void addGlobal(const std::string& name) {
        if (globalNames_.insert(name).second) {
            globals_.push_back(name);
        }
    }

    std::string resolve(const std::string& current, const std::string& spec) const {
        const std::string candidates[] = {
            joinPath(parentPath(current), spec),
            joinPath(root_, spec),
        };
        for (const std::string& candidate : candidates) {
            if (files_->info(candidate)) {
                return candidate;
            }
        }
        throw RError("cannot include '" + spec + "' from " + current);
    }

    void process(const std::string& file) {
        if (!included_.insert(normalizePath(file)).second) {
            return;
        }
        const std::string previous = current_;
        current_ = file;
        expandStream(stripComments(readText(files_, file)), file);
        current_ = previous;
    }

    // lineBase нужен, чтобы строка из #ifdef не начинала нумерацию заново с 1.
    void expandStream(const std::string& text, const std::string& name, int lineBase = 0) {
        std::stringstream stream(text);
        std::string line;
        int lineNo = lineBase;
        while (std::getline(stream, line)) {
            ++lineNo;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            std::string trimmed = trim(line);
            if (abort_) {
                break;
            }
            if (startsWithWord(trimmed, "#include")) {
                if (current_.empty()) {
                    throw RError("include is not available in " + name, lineNo);
                }
                includeLine(current_, trimmed, lineNo);
                continue;
            }
            if (startsWithWord(trimmed, "#def")) {
                defineLine(trimmed, name, lineNo);
                continue;
            }
            if (startsWithWord(trimmed, "#ifdef") || startsWithWord(trimmed, "#ifndef")) {
                conditionalLine(stream, trimmed, name, lineNo);
                if (abort_) break;
                continue;
            }
            if (startsWithWord(trimmed, "#abort")) {
                abort_ = true;
                break;
            }
            if (startsWithWord(trimmed, "#scope")) {
                scopeLine(trimmed, name, lineNo);
                continue;
            }
            if (!trimmed.empty() && trimmed[0] == '#') {
                throw RError("unknown directive in " + name, lineNo);
            }
            out_ += expand(line, 0);
            out_.push_back('\n');
        }
    }

    void includeLine(const std::string& current, const std::string& line, int lineNo) {
        std::string rest = trim(line.substr(8));
        if (rest.size() < 2 || rest.front() != '"') {
            throw RError("include expects a quoted path", lineNo);
        }
        size_t end = rest.find('"', 1);
        if (end == std::string::npos) {
            throw RError("unterminated include path", lineNo);
        }
        const std::string path = resolve(current, rest.substr(1, end - 1));
        process(path);
    }

    void defineLine(const std::string& line, const std::string& name, int lineNo) {
        std::string rest = trim(line.substr(4));
        if (rest.empty() || !isIdentStart(static_cast<unsigned char>(rest[0]))) {
            throw RError("define expects a name in " + name, lineNo);
        }
        size_t i = 1;
        while (i < rest.size() && isIdentCont(static_cast<unsigned char>(rest[i]))) {
            ++i;
        }
        std::string ident = rest.substr(0, i);
        std::string value = trim(rest.substr(i));
        if (value.empty()) {
            defines_.erase(ident);
            literals_.erase(std::remove_if(literals_.begin(), literals_.end(), [&](const Literal& item) {
                return item.name == ident;
            }), literals_.end());
            addGlobal(ident);
            return;
        }
        defines_[ident] = value;
        rememberLiteral(ident, value, name, lineNo);
    }

    // Один литерал — и в глобал, и то, что потом увидит лексер в подстановке.
    // Выражение вроде 1+2 литералом не является: глобал остаётся null,
    // а в текст скрипта по-прежнему вставляется исходная строка.
    void rememberLiteral(const std::string& name, const std::string& value, const std::string& file, int lineNo) {
        std::vector<Token> tokens;
        try {
            tokens = lex(value, file);
        } catch (const RError&) {
            throw RError("invalid define '" + name + "' in " + file, lineNo);
        }
        Literal literal;
        literal.name = name;
        literal.kind = Literal::Kind::Null;
        size_t index = 0;
        bool negative = false;
        if (!tokens.empty() && tokens[index].kind == Token::Kind::Minus) {
            negative = true;
            ++index;
        }
        const bool single = index < tokens.size() && tokens[index].kind != Token::Kind::End &&
                            index + 1 < tokens.size() && tokens[index + 1].kind == Token::Kind::End;
        if (single) {
            const Token& token = tokens[index];
            if (negative && token.kind != Token::Kind::Int && token.kind != Token::Kind::Float) {
                throw RError("invalid define '" + name + "' in " + file, lineNo);
            }
            switch (token.kind) {
            case Token::Kind::Int:
                literal.kind = Literal::Kind::Int;
                if (token.text == "2147483648") {
                    if (!negative) throw RError("integer literal out of range in " + file, lineNo);
                    literal.integer = std::numeric_limits<int>::min();
                } else if (negative) {
                    if (token.intValue == std::numeric_limits<int>::min()) {
                        throw RError("integer literal out of range in " + file, lineNo);
                    }
                    literal.integer = -token.intValue;
                } else {
                    literal.integer = token.intValue;
                }
                break;
            case Token::Kind::Float:
                literal.kind = Literal::Kind::Float;
                literal.real = negative ? -token.floatValue : token.floatValue;
                break;
            case Token::Kind::String:
                literal.kind = Literal::Kind::String;
                literal.text = token.text;
                break;
            case Token::Kind::KwTrue:
                literal.kind = Literal::Kind::Int;
                literal.integer = 1;
                break;
            case Token::Kind::KwFalse:
                literal.kind = Literal::Kind::Int;
                literal.integer = 0;
                break;
            case Token::Kind::KwNull:
                literal.kind = Literal::Kind::Null;
                break;
            case Token::Kind::KwPointer:
                literal.kind = Literal::Kind::Pointer;
                break;
            case Token::Kind::KwVoid:
                literal.kind = Literal::Kind::String;
                break;
            default:
                break;
            }
        }
        literals_.erase(std::remove_if(literals_.begin(), literals_.end(), [&](const Literal& item) {
            return item.name == name;
        }), literals_.end());
        literals_.push_back(std::move(literal));
    }

    void scopeLine(const std::string& line, const std::string& name, int lineNo) {
        std::string rest = trim(line.substr(6));
        try {
            const long value = std::stol(rest);
            if (value < 0) throw RError("scope expects an integer in " + name, lineNo);
            scope_ = static_cast<size_t>(value);
        } catch (...) {
            throw RError("scope expects an integer in " + name, lineNo);
        }
    }

    // Следующая непустая строка — вся ветка. Вложенный #ifdef на этой строке
    // читается тем же expandStream, с номером строки исходного файла.
    void conditionalLine(std::stringstream& stream, const std::string& line, const std::string& name, int& lineNo) {
        const bool negate = startsWithWord(line, "#ifndef");
        const size_t skip = negate ? 7 : 6;
        std::string ident = trim(line.substr(skip));
        const bool take = negate ? !defined(ident) : defined(ident);
        std::string next;
        while (std::getline(stream, next)) {
            ++lineNo;
            if (!next.empty() && next.back() == '\r') next.pop_back();
            if (!trim(next).empty()) break;
        }
        if (!stream && next.empty()) return;
        if (take) {
            expandStream(next, name, lineNo - 1);
        } else {
            skipDirective(stream, next, lineNo);
        }
    }

    void skipDirective(std::stringstream& stream, const std::string& line, int& lineNo) {
        std::string trimmed = trim(line);
        if (!startsWithWord(trimmed, "#ifdef") && !startsWithWord(trimmed, "#ifndef")) {
            return;
        }
        std::string next;
        while (std::getline(stream, next)) {
            ++lineNo;
            if (!next.empty() && next.back() == '\r') next.pop_back();
            if (!trim(next).empty()) break;
        }
        if (!next.empty()) {
            skipDirective(stream, next, lineNo);
        }
    }

    // Глубина больше 32 — ошибка. Раньше хвост вроде #def A A молча оставался нераскрытым.
    std::string expand(std::string_view text, int depth) {
        if (depth > 32) {
            throw RError("macro expanded too many times");
        }
        std::string out;
        bool inString = false;
        for (size_t i = 0; i < text.size();) {
            char c = text[i];
            if (inString) {
                out.push_back(c);
                if (c == '\\' && i + 1 < text.size()) {
                    out.push_back(text[++i]);
                    ++i;
                    continue;
                }
                if (c == '"') {
                    inString = false;
                }
                ++i;
                continue;
            }
            if (c == '"') {
                inString = true;
                out.push_back(c);
                ++i;
                continue;
            }
            if (isIdentStart(static_cast<unsigned char>(c))) {
                size_t begin = i;
                ++i;
                while (i < text.size() && isIdentCont(static_cast<unsigned char>(text[i]))) {
                    ++i;
                }
                std::string ident(text.substr(begin, i - begin));
                auto it = defines_.find(ident);
                if (it != defines_.end() && !it->second.empty()) {
                    out += expand(it->second, depth + 1);
                } else {
                    out += ident;
                }
                continue;
            }
            out.push_back(c);
            ++i;
        }
        return out;
    }
};

}  // анонимное пространство имён

SourceUnit preprocessFile(const files::IFileSystem::Ptr& fileSystem, const std::string& path) {
    if (!fileSystem) {
        throw RError("file system is empty");
    }
    return Preprocessor(fileSystem, parentPath(path)).run(path);
}

SourceUnit preprocessSource(const files::IFileSystem::Ptr& fileSystem, std::string_view source, std::string_view name, const std::string& includeRoot) {
    if (!fileSystem) {
        throw RError("file system is empty");
    }
    return Preprocessor(fileSystem, includeRoot).runText(source, name);
}

}  // пространство имён sme
