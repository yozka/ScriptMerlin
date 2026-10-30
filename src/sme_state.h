#pragma once

#include "sme/sme_engine.h"
#include "sme/sme_host.h"
#include "sme/sme_value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sme {

// Внутреннее состояние движка: байткод и кадры.
// Публичный API — Engine. Сюда смотрят компилятор и интерпретатор.

// Таблица скрипта: плоский вектор, индекс — целое с нуля.
struct Table {
    std::vector<Value> items;
};

// Объект хоста с RAII-владением.
struct HostBox {
    std::shared_ptr<HostObject> object;
};

// Ссылка скрипта: &локал, &глобал или &таблица[i].
// Ссылка на локал помнит поколение кадра. Индекс в стеке после return
// занимают заново, и без поколения старая ссылка прочитала бы чужой регистр.
struct Reference {
    bool global = false;
    bool indexed = false;
    size_t frame = 0;
    size_t frameGeneration = 0;
    size_t slot = 0;
    Handle container{};
    size_t index = 0;
};

// Объекты со значимым сроком жизни хранятся через shared_ptr в Value.
using StoredObject = std::variant<Table, HostBox, Reference>;

struct Object {
    template <class T>
    explicit Object(T&& value) : data(std::forward<T>(value)) {}

    StoredObject data;
};

#define OT_TAB 0
#define OT_OBJ 1
#define OT_REF 2

#define OT_CHECK(index, type) \
    static_assert(std::is_same_v<std::variant_alternative_t<(index), StoredObject>, type>, \
        #index " is not " #type " in Object")

OT_CHECK(OT_TAB, Table);
OT_CHECK(OT_OBJ, HostBox);
OT_CHECK(OT_REF, Reference);
static_assert(std::variant_size_v<StoredObject> == 3, "Object gained or lost an alternative");
using Factory = std::function<std::shared_ptr<HostObject>()>;

// Константа байткода. Строка здесь ещё текст: LoadConst копирует её в Value
// при первом выполнении и затем использует interned-значение.
using Constant = std::variant<std::monostate, int, float, bool, std::string, Pointer>;

// Байткод регистровой машины. r[n] — регистр текущего кадра.
// Арифметика с двумя операндами пишет r[a] = r[b] □ r[c], если не сказано иначе.
// Move и Store* пропускают null: присваивание null не затирает старое значение.
enum class Op : uint8_t {
    LoadConst,    // r[a] = константа[b], в том числе null
    Move,         // r[a] = r[b]; null не пишет
    LoadGlobal,   // r[a] = глобал[b]
    StoreGlobal,  // глобал[a] = r[b]; null не пишет
    Add,          // r[a] = r[b] + r[c]; строки склеиваются, таблицы соединяются
    Sub,
    Mul,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    Shl,          // логический сдвиг, счётчик только 0..30
    Shr,
    Neg,          // r[a] = -r[b]
    Not,          // r[a] = !r[b], результат 0 или 1
    BitNot,
    CmpEq,        // сравнения дают int 0/1, не bool
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,
    Jump,         // ip = a
    JumpIfFalse,  // если r[a] ложно, ip = b
    JumpIfTrue,
    Call,         // r[a] = функция[b](r[c] .. count d); лишние аргументы отбрасываются, нехватка остаётся null
    CallNative,   // r[a] = натив[b](r[c] .. count d)
    Return,       // вернуть r[a] в вызывающий кадр или хосту
    MakeTable,    // r[a] = таблица из r[b] .. count c
    GetIndex,     // r[a] = r[b][r[c]]; у строки это строка из одного символа
    SetIndex,     // r[a][r[b]] = r[c]; null пишет честно, в отличие от Move
    GetProp,      // r[a] = r[b].имя(константа c)
    SetProp,      // r[a].имя(константа b) = r[c]
    CallMethod,   // r[a] = r[b].имя(константа c)(r[d] .. count e)
    CallName,     // r[a] = имя(константа b)(r[c] .. count d); неизвестное имя даёт int 0
    MakeRefLocal, // r[a] = &локал[b] текущего кадра
    MakeRefGlobal,// r[a] = &глобал[b]
    MakeRefIndex, // r[a] = &r[b][r[c]]
    LoadRef,      // r[a] = *r[b]
    StoreRef,     // *r[a] = r[b]; null не пишет
    NewObj,       // r[a] = фабрика(константа b)
    Cast,         // r[a] = каст r[b] в вид c; формат — константа d, 65535 значит «без формата»
    Typeof,       // r[a] = код типа r[b]; оператор ? компилируется сюда
    LogicAnd,     // r[a] = истина(r[b]) && истина(r[c]); обе стороны уже вычислены, короткой схемы нет
    LogicOr,
    Stop,         // пауза вызова; resume продолжит со следующей инструкции
    GetChar,      // r[a] = байт строки r[b] по индексу r[c]
    SetChar,      // байт строки r[a] по индексу r[b] = r[c]
    RefGlobal,    // r[a] = &глобал[r[b]]; индекс глобала лежит в регистре, так устроен var(id)
    Copy,         // r[a] = r[b], в том числе null. Move null не пишет, Copy пишет всегда
    BinaryImm     // r[a] = r[b] □ int16(d), код операции в c
};

// Одна инструкция. Поля a..e — операнды, смысл зависит от op.
// line нужен, чтобы ошибка выполнения указывала на строку скрипта.
struct alignas(16) Instruction {
    Op op{};
    uint16_t a = 0;
    uint16_t b = 0;
    uint16_t c = 0;
    uint16_t d = 0;
    uint16_t e = 0;
    uint16_t line = 1;
};

// Скомпилированная функция: сколько регистров занять в кадре и сам код.
struct Chunk {
    std::string name;
    uint16_t parameterCount = 0;
    uint16_t registerCount = 0;
    std::vector<Instruction> code;
    std::vector<Constant> constants;
    // Строковые константы разделяют владельца и живут, пока жив chunk или Value.
    mutable std::vector<Value> interned;
    size_t scope = 0; // копия #scope файла, одна на все функции загрузки
};

struct Global {
    std::string name;
    Value value;
};

// Кадр вызова. regs[0..parameterCount) — аргументы, дальше локалы и временные.
// generation монотонный: ссылка сверяет его, а не только позицию в векторе.
struct Frame {
    size_t function = 0;
    size_t ip = 0;
    std::vector<Value> regs;
    uint16_t parentDest = 0;   // куда в кадре вызывающего записать результат
    uint16_t supplied = 0;     // сколько аргументов реально передали
    size_t generation = 0;
    bool writesParent = false; // false у самого внешнего кадра: результат уходит хосту
};

// Один вход хоста или resume. Вложенный call кладёт ещё один RunState сверху,
// не смешивая свои кадры с паузой нижнего вызова.
struct RunState {
    std::vector<Frame> frames;
};

struct State {
    std::vector<Global> globals;
    std::vector<Chunk> functions;
    std::vector<std::string> nativeNames;
    std::vector<NativeFn> natives;
    std::unordered_map<std::string, Factory> factories;
    files::IFileSystem::Ptr files;
    log::ILogging::Ptr log;
    std::vector<RunState> runStack;
    size_t frameSerial = 1;
    std::string sourceName = "script";
    size_t scope = 0;

    size_t freshFrameId();
    void fail(const std::string& message, int line = 0);

    Value makeString(std::string text);
    std::string toString(const Value& value) const;
    std::string_view typeName(const Value& value) const;
    Type typeOf(const Value& value) const;
    std::string stringOf(const Value& value) const;
    size_t lengthOf(const Value& value) const;
    bool truthy(const Value& value) const;

    // Вид живёт в пуле констант chunk, пока этот chunk не заменят.
    std::string_view constantString(const Chunk& chunk, size_t index, int line) const;
};

}  // пространство имён sme
