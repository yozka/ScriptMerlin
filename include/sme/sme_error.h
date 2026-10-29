#pragma once

#include <stdexcept>
#include <string>

namespace sme::error
{
	// Коды ошибок
	enum class Code : int
	{
		OK = 0,
		
		FUNCTION_ARGUMENTS = 100
	};

}

namespace sme
{

// Ошибка языка: разбор, компиляция или выполнение.
// Номер строки дописывается к тексту, чтобы хост видел место в скрипте.
// line == 0 значит «строка неизвестна» (ошибка на стороне хоста, не в исходнике).
class RError : public std::runtime_error {
public:
    RError(std::string message, int line = 0)
        : std::runtime_error(decorate(message, line))
        , line_(line) {}

    int line() const { return line_; }

private:
    int line_;

    static std::string decorate(const std::string& message, int line) {
        if (line <= 0) {
            return message;
        }
        return message + " (line " + std::to_string(line) + ")";
    }
};

}  // пространство имён sme

