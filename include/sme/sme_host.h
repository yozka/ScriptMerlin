#pragma once

#include "sme/sme_value.h"

#include <span>
#include <string_view>

namespace sme {

class Engine;

// Объект игры, который скрипт видит как значение со свойствами и методами:
// counter.value, obj.run(). Набор типов открытый, поэтому это виртуальный
// класс, а не ещё одна альтернатива variant.
class HostObject {
public:
    virtual ~HostObject() = default;

    // Сборщик мусора зовёт release, когда скрипт больше не держит хендл.
    // Сам C++-объект при этом может жить дальше, если его держит shared_ptr игры.
    virtual void release() {}

    virtual Value get(Engine& engine, std::string_view name);
    virtual void set(Engine& engine, std::string_view name, const Value& value);
    virtual Value call(Engine& engine, std::string_view name, std::span<const Value> args);
};

}  // пространство имён sme
