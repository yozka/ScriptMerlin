#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <variant>

namespace sme {

// Хендл — адрес объекта в куче движка, а не сам объект.
// generation отличает живой слот от переиспользованного: после сборки
// мусора тот же index может принадлежать уже другой строке или таблице.
struct Handle {
    size_t index = 0;
    size_t generation = 0;

    friend bool operator==(const Handle& a, const Handle& b) {
        return a.index == b.index && a.generation == b.generation;
    }
};

// Скриптовый указатель NULL. Это не «пустое значение»: у null код типа 0,
// у NULL — 6. В языке они пишутся по-разному: null/NUL и NULL.
struct Pointer {};

// Короткая строка прямо в значении, без кучи. Длиннее 7 байт — Handle.
struct ShortStr {
    char data[7]{};
    uint8_t len = 0;

    friend bool operator==(const ShortStr& a, const ShortStr& b) {
        return a.len == b.len && std::memcmp(a.data, b.data, a.len) == 0;
    }
};

// Непосредственные значения лежат прямо в variant.
// Длинная строка, таблица, ссылка и объект хоста — это Handle в кучу.
// bool скрипт сам не производит (true/false компилируются в int 0/1),
// но хост может передать настоящий bool.
using Value = std::variant<std::monostate, int, float, bool, Handle, Pointer, ShortStr>;

// Индекс типа в Value. Число совпадает с порядком в variant выше.
#define VT_NULL 0
#define VT_INT  1
#define VT_FLOAT 2
#define VT_BOOL 3
#define VT_HANDLE 4
#define VT_PTR  5
#define VT_SSTR 6

#define VT_CHECK(index, type) \
    static_assert(std::is_same_v<std::variant_alternative_t<(index), Value>, type>, \
        #index " is not " #type " in Value")

VT_CHECK(VT_NULL, std::monostate);
VT_CHECK(VT_INT, int);
VT_CHECK(VT_FLOAT, float);
VT_CHECK(VT_BOOL, bool);
VT_CHECK(VT_HANDLE, Handle);
VT_CHECK(VT_PTR, Pointer);
VT_CHECK(VT_SSTR, ShortStr);
static_assert(std::variant_size_v<Value> == 7, "Value gained or lost an alternative");

inline Value integer(int value) { return Value(std::in_place_type<int>, value); }
inline Value real(float value) { return Value(std::in_place_type<float>, value); }
inline Value boolean(bool value) { return Value(std::in_place_type<bool>, value); }
// monostate — «значения нет». Так выглядят null, непереданный аргумент и пустой слот.
inline Value null() { return Value{}; }
inline Value pointer() { return Value(std::in_place_type<Pointer>); }

inline Value shortString(const char* data, size_t size) {
    ShortStr text;
    text.len = static_cast<uint8_t>(size);
    if (size != 0) {
        std::memcpy(text.data, data, size);
    }
    return Value(std::in_place_type<ShortStr>, text);
}

inline bool isNull(const Value& value) { return std::holds_alternative<std::monostate>(value); }

// Вид значения. Строка — и короткая, и лежащая в куче. Invalid — мёртвый хендл.
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
