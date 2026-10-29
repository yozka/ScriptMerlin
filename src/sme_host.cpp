#include "sme/sme_host.h"

#include "sme/sme_error.h"

// Реализация по умолчанию: неизвестное свойство или метод — ошибка.
// Игра перекрывает get/set/call у своего типа.

#include <string>

namespace sme {

Value HostObject::get(Engine&, std::string_view name) {
    throw RError("unknown property '" + std::string(name) + "'");
}

void HostObject::set(Engine&, std::string_view name, const Value&) {
    throw RError("unknown property '" + std::string(name) + "'");
}

Value HostObject::call(Engine&, std::string_view name, std::span<const Value>) {
    throw RError("unknown method '" + std::string(name) + "'");
}

}  // пространство имён sme
