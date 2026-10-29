#pragma once

#include <memory>
#include <string>

namespace sme::log {

// Уровни сообщений, которые хост может принять через useLog.
// Сейчас в лог попадают ошибки call/resume и вывод println, если логгер задан.
enum class Level {
    Output,
    Info,
    Warning,
    RError,
    Halt
};

class ILogging {
public:
    using Ptr = std::shared_ptr<ILogging>;

    virtual ~ILogging() = default;
    virtual void message(Level level, const std::string& text) = 0;
};

}  // пространство имён sme::log
