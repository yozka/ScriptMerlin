#include "sme_state.h"

#include "sme/sme_error.h"

#include <limits>

// Поколенческая куча. Слоты лежат в deque: рост не двигает уже выданные элементы.
// Сборка помечает корни и всё, до чего от них дойти. Непомеченное отдаётся
// в free-list, generation растёт, старый Handle больше не проходит check.

namespace sme {

void Heap::setRoots(RootFn roots) { roots_ = std::move(roots); }

bool Heap::check(Handle handle) const {
    if (handle.index >= slots_.size()) {
        return false;
    }
    const Slot& slot = slots_[handle.index];
    return slot.live && slot.generation == handle.generation;
}

Object& Heap::at(Handle handle) {
    if (!check(handle)) {
        throw RError("dangling script handle");
    }
    return slots_[handle.index].data;
}

const Object& Heap::at(Handle handle) const {
    if (!check(handle)) {
        throw RError("dangling script handle");
    }
    return slots_[handle.index].data;
}

Handle Heap::allocate(Object object) {
    // Порог небольшой: игра часто создаёт короткие строки, и release объекта
    // хоста должен случиться не откладываясь до конца скрипта.
    if (free_.empty() && slots_.size() >= kCollectAt) {
        collect();
    }
    size_t index = 0;
    if (free_.empty()) {
        slots_.emplace_back();
        index = slots_.size() - 1;
    } else {
        index = free_.back();
        free_.pop_back();
    }
    Slot& slot = slots_[index];
    slot.live = true;
    slot.data = std::move(object);
    return Handle{index, slot.generation};
}

void Heap::markValue(const Value& value, std::vector<uint8_t>& mark) const {
    if (!std::holds_alternative<Handle>(value)) {
        return;
    }
    Handle handle = std::get<Handle>(value);
    if (!check(handle) || mark[handle.index]) {
        return;
    }
    mark[handle.index] = 1;
    const Object& object = slots_[handle.index].data;
    if (const auto* table = std::get_if<Table>(&object)) {
        for (const Value& child : table->items) {
            markValue(child, mark);
        }
    } else if (const auto* ref = std::get_if<Reference>(&object)) {
        // Ссылка на локал кадр не удерживает: после return она должна протухнуть.
        // Ссылка на элемент таблицы держит саму таблицу, иначе элемент исчезнет.
        if (ref->indexed) {
            markValue(Value{ref->container}, mark);
        }
    }
}

void Heap::collect() {
    std::vector<Value> roots;
    if (roots_) {
        roots_(roots);
    }
    std::vector<uint8_t> mark(slots_.size(), 0);
    for (const Value& root : roots) {
        markValue(root, mark);
    }
    for (size_t index = 0; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (!slot.live || mark[index]) {
            continue;
        }
        if (auto* host = std::get_if<HostBox>(&slot.data)) {
            if (host->object) {
                host->object->release();
            }
        }
        slot.live = false;
        slot.data = std::string{};
        // После максимума поколение снова 1, не 0: нулевой Handle значит «хендла не было».
        slot.generation = slot.generation == std::numeric_limits<size_t>::max() ? 1 : slot.generation + 1;
        free_.push_back(index);
    }
}

}  // пространство имён sme
