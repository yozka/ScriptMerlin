#include "sme/sme_value.h"
#include "sme/sme_error.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace sme {
namespace {

std::string render(const Value& value, std::vector<const Table*>& seen) {
    if (std::holds_alternative<std::monostate>(value) ||
        std::holds_alternative<std::nullptr_t>(value)) {
        return "null";
    }
    if (const auto* integer = std::get_if<int>(&value)) return std::to_string(*integer);
    if (const auto* real = std::get_if<float>(&value)) {
        std::ostringstream out;
        out << *real;
        return out.str();
    }
    if (const auto* boolean = std::get_if<bool>(&value)) return *boolean ? "true" : "false";
    if (const auto* text = std::get_if<std::string>(&value)) return *text;
    if (const auto* text = std::get_if<SharedString>(&value)) return *text ? **text : "invalid";
    if (std::holds_alternative<SharedValue>(value) ||
        std::holds_alternative<LocalReference>(value) ||
        std::holds_alternative<GlobalReference>(value) ||
        std::holds_alternative<IndexedReference>(value)) {
        return "ref";
    }
    if (const auto* object = std::get_if<Handle>(&value)) return *object ? "object" : "invalid";

    const TableHandle& table = std::get<TableHandle>(value);
    if (!table) return "invalid";
    if (std::find(seen.begin(), seen.end(), table.get()) != seen.end()) return "[...]";

    seen.push_back(table.get());
    std::string result = "[";
    for (size_t i = 0; i < table->size(); ++i) {
        if (i != 0) result += ", ";
        result += render((*table)[i], seen);
    }
    result += "]";
    seen.pop_back();
    return result;
}

}  // namespace

Value Value::Integer(int value) {
    return Value(std::in_place_type<int>, value);
}

Value Value::Real(float value) {
    return Value(std::in_place_type<float>, value);
}

Value Value::Boolean(bool value) {
    return Value(std::in_place_type<bool>, value);
}

Value Value::String(std::string value) {
    return Value(std::in_place_type<std::string>, std::move(value));
}

Value Value::Null() {
    return Value{};
}

Value Value::Pointer() {
    return Value(std::in_place_type<std::nullptr_t>, nullptr);
}

Value Value::Shared(Value value) {
    return Value(std::in_place_type<SharedValue>, std::make_shared<Value>(std::move(value)));
}

bool Value::IsNull() const {
    return std::holds_alternative<std::monostate>(*this);
}

bool Value::IsReference() const {
    return std::holds_alternative<SharedValue>(*this) ||
        std::holds_alternative<LocalReference>(*this) ||
        std::holds_alternative<GlobalReference>(*this) ||
        std::holds_alternative<IndexedReference>(*this);
}

bool Value::IsInteger() const { return std::holds_alternative<int>(*this); }
bool Value::IsFloat() const { return std::holds_alternative<float>(*this); }
bool Value::IsBool() const { return std::holds_alternative<bool>(*this); }
bool Value::IsString() const {
    if (std::holds_alternative<std::string>(*this)) return true;
    const auto* string = std::get_if<SharedString>(this);
    return string && static_cast<bool>(*string);
}
bool Value::IsTable() const {
    const auto* table = std::get_if<TableHandle>(this);
    return table && static_cast<bool>(*table);
}
bool Value::IsObject() const {
    const auto* object = std::get_if<Handle>(this);
    return object && static_cast<bool>(*object);
}
bool Value::IsPointer() const { return std::holds_alternative<std::nullptr_t>(*this); }
bool Value::IsInvalid() const {
    if (const auto* object = std::get_if<Handle>(this)) return !*object;
    if (const auto* string = std::get_if<SharedString>(this)) return !*string;
    if (const auto* table = std::get_if<TableHandle>(this)) return !*table;
    if (const auto* value = std::get_if<SharedValue>(this)) return !*value;
    return false;
}

std::optional<float> Value::AsNumber() const {
    if (const auto* integer = std::get_if<int>(this)) return static_cast<float>(*integer);
    if (const auto* real = std::get_if<float>(this)) return *real;
    if (const auto* flag = std::get_if<bool>(this)) return *flag ? 1.0f : 0.0f;
    return std::nullopt;
}

float Value::AsFloat(const float def) const {
    if (const auto* real = std::get_if<float>(this)) return *real;
    if (const auto* integer = std::get_if<int>(this)) return static_cast<float>(*integer);
    return def;
}

int Value::AsInteger(const int def) const {
    if (const auto* integer = std::get_if<int>(this)) return *integer;
    if (const auto* real = std::get_if<float>(this)) return static_cast<int>(*real);
    return def;
}

std::string Value::AsString(const std::string& def) const {
    if (const auto* text = std::get_if<std::string>(this)) return *text;
    if (const auto* text = std::get_if<SharedString>(this); text && *text) return **text;
    return def;
}

size_t Value::Length() const {
    if (IsString()) return AsString().size();
    if (IsTable()) return std::get<TableHandle>(*this)->size();
    throw RError("len expects a string or a table");
}

Type Value::TypeOf() const {
    const auto kind = index();
    if (kind == VT_INT) return Type::Int;
    if (kind == VT_FLOAT) return Type::Float;
    if (kind == VT_STRING) return Type::String;
    if (kind == VT_SHARED_STRING) return std::get<SharedString>(*this) ? Type::String : Type::Invalid;
    if (kind == VT_NULL) return Type::Null;
    if (kind == VT_TABLE) return std::get<TableHandle>(*this) ? Type::Table : Type::Invalid;
    if (kind == VT_HANDLE) return std::get<Handle>(*this) ? Type::Object : Type::Invalid;
    if (kind == VT_SHARED_VALUE) return std::get<SharedValue>(*this) ? Type::Ref : Type::Invalid;
    if (kind == VT_LOCAL_REF || kind == VT_GLOBAL_REF || kind == VT_INDEXED_REF) return Type::Ref;
    if (kind == VT_BOOL) return Type::Bool;
    if (kind == VT_PTR) return Type::Pointer;
    return Type::Invalid;
}

int32_t Value::TypeCode() const {
    switch (TypeOf()) {
    case Type::Pointer: return 6;
    case Type::Int:
    case Type::Bool: return 1;
    case Type::Float: return 2;
    case Type::String: return 3;
    case Type::Table: return 4;
    case Type::Null:
    case Type::Ref:
    case Type::Object:
    case Type::Invalid: return 0;
    }
    return 0;
}

std::string_view Value::TypeName() const {
    switch (TypeOf()) {
    case Type::Null: return "null";
    case Type::Int: return "int";
    case Type::Float: return "float";
    case Type::Bool: return "bool";
    case Type::String: return "string";
    case Type::Table: return "table";
    case Type::Ref: return "ref";
    case Type::Object: return "object";
    case Type::Pointer: return "pointer";
    case Type::Invalid: return "invalid";
    }
    return "invalid";
}

std::string Value::ToString() const {
    std::vector<const Table*> seen;
    return render(*this, seen);
}

}  // namespace sme
