#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace sme {

class HostObject;
class Table;
class Value;
using Handle = std::shared_ptr<HostObject>;
using TableHandle = std::shared_ptr<Table>;
using SharedString = std::shared_ptr<std::string>;
using SharedValue = std::shared_ptr<Value>;

class Table : public std::vector<Value> {
public:
    using std::vector<Value>::vector;
};

struct LocalReference {
    uint32_t frame = 0;
    size_t frameGeneration = 0;
    uint16_t slot = 0;
    uint32_t identity = 0;

    friend bool operator==(const LocalReference&, const LocalReference&) = default;
};

struct GlobalReference {
    uint32_t slot = 0;
    uint32_t identity = 0;

    friend bool operator==(const GlobalReference&, const GlobalReference&) = default;
};

struct IndexedReference {
    TableHandle container;
    uint32_t index = 0;
    uint32_t identity = 0;

    friend bool operator==(const IndexedReference&, const IndexedReference&) = default;
};

// Строки и числа хранятся прямо в Value. Таблицы доступны как vector<Value>,
// а SharedValue разделяет владение отдельным значением между копиями.
// bool скрипт сам не производит (true/false компилируются в int 0/1),
// но хост может передать настоящий bool.
using ValueStorage = std::variant<std::monostate, int, float, bool, Handle, std::nullptr_t, std::string,
    SharedString, TableHandle, SharedValue, LocalReference, GlobalReference, IndexedReference>;

class Value : public ValueStorage {
public:
    using Base = ValueStorage;
    using Base::Base;
    using Base::operator=;

public:
    static Value integer(int value);
    static Value real(float value);
    static Value boolean(bool value);
    static Value string(std::string value);
    static Value null();
    static Value pointer();
    static Value shared(Value value);

public:
    bool isNull() const;
    bool isReference() const;
};

// Вид значения. Invalid зарезервирован для пустого shared_ptr.
enum class Type {
    Null,
    Int,
    Float,
    Bool,
    String,
    Table,
    Ref,
    Object,
    Pointer,
    Invalid
};

}  // пространство имён sme

namespace std {

template <>
struct variant_size<sme::Value> : variant_size<sme::Value::Base> {};

template <size_t Index>
struct variant_alternative<Index, sme::Value> : variant_alternative<Index, sme::Value::Base> {};

}  // namespace std

// Индекс типа в Value. Число совпадает с порядком в variant выше.
#define VT_NULL 0
#define VT_INT  1
#define VT_FLOAT 2
#define VT_BOOL 3
#define VT_HANDLE 4
#define VT_PTR  5
#define VT_STRING 6
#define VT_SHARED_STRING 7
#define VT_TABLE 8
#define VT_SHARED_VALUE 9
#define VT_LOCAL_REF 10
#define VT_GLOBAL_REF 11
#define VT_INDEXED_REF 12

#define VT_CHECK(index, type) \
    static_assert(std::is_same_v<std::variant_alternative_t<(index), sme::Value>, type>, \
        #index " is not " #type " in Value")

VT_CHECK(VT_NULL, std::monostate);
VT_CHECK(VT_INT, int);
VT_CHECK(VT_FLOAT, float);
VT_CHECK(VT_BOOL, bool);
VT_CHECK(VT_HANDLE, sme::Handle);
VT_CHECK(VT_PTR, std::nullptr_t);
VT_CHECK(VT_STRING, std::string);
VT_CHECK(VT_SHARED_STRING, sme::SharedString);
VT_CHECK(VT_TABLE, sme::TableHandle);
VT_CHECK(VT_SHARED_VALUE, sme::SharedValue);
VT_CHECK(VT_LOCAL_REF, sme::LocalReference);
VT_CHECK(VT_GLOBAL_REF, sme::GlobalReference);
VT_CHECK(VT_INDEXED_REF, sme::IndexedReference);
static_assert(std::variant_size_v<sme::Value> == 13, "Value gained or lost an alternative");
