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

struct LocalReference {
    uint32_t frame = 0;
    size_t frameGeneration = 0;
    uint16_t slot = 0;
};

struct GlobalReference {
    uint32_t slot = 0;
};

struct IndexedReference {
    Handle container;
    uint32_t index = 0;
};

// Ссылка на слот: payload зависит от типа адресуемого слота.
struct Reference {
    enum class Kind : uint8_t { Local, Global, Indexed };
    union Target {
        LocalReference local;
        GlobalReference global;
        IndexedReference indexed;

        Target() {}
        ~Target() {}
    };

    uint32_t identity = 0;
    Kind kind = Kind::Local;
    Target target;

    Reference() { std::construct_at(&target.local, LocalReference{}); }
    Reference(const Reference& other) : identity(other.identity), kind(other.kind) { copyTarget(other); }
    Reference(Reference&& other) noexcept : identity(other.identity), kind(other.kind) { moveTarget(other); }
    ~Reference() { destroyTarget(); }

    Reference& operator=(const Reference& other) {
        if (this != &other) assign(other);
        return *this;
    }

    Reference& operator=(Reference&& other) noexcept {
        if (this != &other) assign(std::move(other));
        return *this;
    }

    bool isLocal() const { return kind == Kind::Local; }
    bool isGlobal() const { return kind == Kind::Global; }
    bool isIndexed() const { return kind == Kind::Indexed; }

    const LocalReference& local() const { return target.local; }
    const GlobalReference& global() const { return target.global; }
    const IndexedReference& indexed() const { return target.indexed; }

    void setLocal(LocalReference value) { setTarget(Kind::Local, value); }
    void setGlobal(GlobalReference value) { setTarget(Kind::Global, value); }
    void setIndexed(IndexedReference value) { setTarget(Kind::Indexed, std::move(value)); }

    friend bool operator==(const Reference& lhs, const Reference& rhs) {
        return lhs.identity == rhs.identity;
    }

private:
    void destroyTarget() {
        if (kind == Kind::Indexed) std::destroy_at(&target.indexed);
    }

    void copyTarget(const Reference& other) {
        switch (kind) {
        case Kind::Local: std::construct_at(&target.local, other.target.local); break;
        case Kind::Global: std::construct_at(&target.global, other.target.global); break;
        case Kind::Indexed: std::construct_at(&target.indexed, other.target.indexed); break;
        }
    }

    void moveTarget(Reference& other) noexcept {
        switch (kind) {
        case Kind::Local: std::construct_at(&target.local, other.target.local); break;
        case Kind::Global: std::construct_at(&target.global, other.target.global); break;
        case Kind::Indexed: std::construct_at(&target.indexed, std::move(other.target.indexed)); break;
        }
    }

    template <class T>
    void setTarget(Kind nextKind, T&& value) {
        if (kind == nextKind) {
            if constexpr (std::is_same_v<std::decay_t<T>, LocalReference>) target.local = std::forward<T>(value);
            else if constexpr (std::is_same_v<std::decay_t<T>, GlobalReference>) target.global = std::forward<T>(value);
            else target.indexed = std::forward<T>(value);
        } else {
            destroyTarget();
            kind = nextKind;
            if constexpr (std::is_same_v<std::decay_t<T>, LocalReference>) std::construct_at(&target.local, std::forward<T>(value));
            else if constexpr (std::is_same_v<std::decay_t<T>, GlobalReference>) std::construct_at(&target.global, std::forward<T>(value));
            else std::construct_at(&target.indexed, std::forward<T>(value));
        }
    }

    void assign(const Reference& other) {
        identity = other.identity;
        if (kind == other.kind) {
            switch (kind) {
            case Kind::Local: target.local = other.target.local; break;
            case Kind::Global: target.global = other.target.global; break;
            case Kind::Indexed: target.indexed = other.target.indexed; break;
            }
        } else {
            destroyTarget();
            kind = other.kind;
            copyTarget(other);
        }
    }

    void assign(Reference&& other) noexcept {
        identity = other.identity;
        if (kind == other.kind) {
            switch (kind) {
            case Kind::Local: target.local = other.target.local; break;
            case Kind::Global: target.global = other.target.global; break;
            case Kind::Indexed: target.indexed = std::move(other.target.indexed); break;
            }
        } else {
            destroyTarget();
            kind = other.kind;
            moveTarget(other);
        }
    }
};

// Скриптовый указатель NULL. Это не «пустое значение»: у null код типа 0,
// у NULL — 6. В языке они пишутся по-разному: null/NUL и NULL.
struct Pointer {};

// Строки и числа хранятся прямо в Value. Строка при адресации может перейти
// в SharedString; таблицы, ссылки и объекты хоста разделяют владение через shared_ptr.
// bool скрипт сам не производит (true/false компилируются в int 0/1),
// но хост может передать настоящий bool.
using Value = std::variant<std::monostate, int, float, bool, Handle, Pointer, std::string, SharedString, Reference>;

// Индекс типа в Value. Число совпадает с порядком в variant выше.
#define VT_NULL 0
#define VT_INT  1
#define VT_FLOAT 2
#define VT_BOOL 3
#define VT_HANDLE 4
#define VT_PTR  5
#define VT_STRING 6
#define VT_SHARED_STRING 7
#define VT_REF 8

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
VT_CHECK(VT_REF, Reference);
static_assert(std::variant_size_v<Value> == 9, "Value gained or lost an alternative");

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
