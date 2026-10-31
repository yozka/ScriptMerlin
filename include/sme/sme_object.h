#pragma once

#include "sme/sme_value.h"

#include <span>
#include <string_view>

namespace sme {

class Engine;

// Объект приложения, который скрипт видит как значение со свойствами и методами:
// counter.value, obj.run(). Набор типов открытый, поэтому это интерфейс.
class IScriptObject {
public:
    virtual ~IScriptObject() = default;

    virtual void SetProperty(Engine& engine, std::string_view name, const Value& value) = 0;
    virtual Value GetProperty(Engine& engine, std::string_view name) const = 0;
    virtual Value Call(Engine& engine, std::string_view name, std::span<const Value> args) = 0;
};

}  // пространство имён sme
