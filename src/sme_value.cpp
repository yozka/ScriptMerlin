#include "sme/sme_value.h"

#include <utility>

namespace sme {

Value Value::integer(int value) {
    return Value(std::in_place_type<int>, value);
}

Value Value::real(float value) {
    return Value(std::in_place_type<float>, value);
}

Value Value::boolean(bool value) {
    return Value(std::in_place_type<bool>, value);
}

Value Value::string(std::string value) {
    return Value(std::in_place_type<std::string>, std::move(value));
}

Value Value::null() {
    return Value{};
}

Value Value::pointer() {
    return Value(std::in_place_type<std::nullptr_t>, nullptr);
}

Value Value::shared(Value value) {
    return Value(std::in_place_type<SharedValue>, std::make_shared<Value>(std::move(value)));
}

bool Value::isNull() const {
    return std::holds_alternative<std::monostate>(*this);
}

bool Value::isReference() const {
    return std::holds_alternative<SharedValue>(*this) ||
        std::holds_alternative<LocalReference>(*this) ||
        std::holds_alternative<GlobalReference>(*this) ||
        std::holds_alternative<IndexedReference>(*this);
}

}  // namespace sme
