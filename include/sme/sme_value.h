#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <variant>

namespace sme {

struct Object;
using Handle = std::shared_ptr<Object>;
using SharedString = std::shared_ptr<std::string>;

// Скриптовый указатель NULL. Это не «пустое значение»: у null код типа 0,
// у NULL — 6. В языке они пишутся по-разному: null/NUL и NULL.
struct Pointer {};

// Строки и числа хранятся прямо в Value. Строка при адресации может перейти
// в SharedString; таблицы, ссылки и объекты хоста разделяют владение через shared_ptr.
// bool скрипт сам не производит (true/false компилируются в int 0/1),
// но хост может передать настоящий bool.
using Value = std::variant<std::monostate, int, float, bool, Handle, Pointer, std::string, SharedString>;

// Индекс типа в Value. Число совпадает с порядком в variant выше.
#define VT_NULL 0
#define VT_INT  1
#define VT_FLOAT 2
#define VT_BOOL 3
#define VT_HANDLE 4
#define VT_PTR  5
#define VT_STRING 6
#define VT_SHARED_STRING 7

#define VT_CHECK(index, type) \
    static_assert(std::is_same_v<std::variant_alternative_t<(index), Value>, type>, \
        #index " is not " #type " in Value")

VT_CHECK(VT_NULL, std::monostate);
VT_CHECK(VT_INT, int);
VT_CHECK(VT_FLOAT, float);
VT_CHECK(VT_BOOL, bool);
VT_CHECK(VT_HANDLE, Handle);
VT_CHECK(VT_PTR, Pointer);
VT_CHECK(VT_STRING, std::string);
VT_CHECK(VT_SHARED_STRING, SharedString);
static_assert(std::variant_size_v<Value> == 8, "Value gained or lost an alternative");

inline Value integer(int value) { return Value(std::in_place_type<int>, value); }
inline Value real(float value) { return Value(std::in_place_type<float>, value); }
inline Value boolean(bool value) { return Value(std::in_place_type<bool>, value); }
// monostate — «значения нет». Так выглядят null, непереданный аргумент и пустой слот.
inline Value null() { return Value{}; }
inline Value pointer() { return Value(std::in_place_type<Pointer>); }

inline bool isNull(const Value& value) { return std::holds_alternative<std::monostate>(value); }

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
