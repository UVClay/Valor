#include "valor/entities.h"

#include <algorithm>

namespace valor {

Entity* Entities::create(int32_t type, std::unique_ptr<Behaviour> b) {
    int slot;
    if (slots_used < kSlots) {
        slot = int(slots_used++);
    } else {
        slot = -1;
        for (int i = 0; i < kSlots; i++)
            if (!handles_[i]) {
                slot = i;
                break;
            }
        if (slot < 0) return nullptr;
    }
    serial = (serial + 1) & 0xFFFF;
    return adopt((serial << 16) | uint32_t(slot), type, std::move(b));
}

Entity* Entities::adopt(uint32_t h, int32_t type, std::unique_ptr<Behaviour> b) {
    uint32_t slot = h & 0xFFFF;
    if (slot >= kSlots) return nullptr;
    // A slot still naming another entity (removed, its handle not freed yet): that one is gone.
    if (Entity* old = handles_[slot]) {
        old->flags_a |= kFlagARemoved;
        order_.erase(std::remove(order_.begin(), order_.end(), old), order_.end());
    }
    auto e = std::make_unique<Entity>();
    e->handle = h;
    e->type = type;
    e->behaviour = b ? std::move(b) : std::make_unique<Behaviour>();
    handles_[slot] = e.get();
    serials_[slot] = h >> 16;
    removed_[slot] = false;
    if (slot >= slots_used) slots_used = slot + 1;
    owned_.push_back(std::move(e));
    return owned_.back().get();
}

void Entities::set_head(Entity* e) {
    head_ = e;
    order_.erase(std::remove(order_.begin(), order_.end(), e), order_.end());
    order_.insert(order_.begin(), e);
}

void Entities::link_front(Entity* e) {
    auto it = head_ ? std::find(order_.begin(), order_.end(), head_) : order_.end();
    order_.insert(it == order_.end() ? order_.begin() : it + 1, e);
}

void Entities::link_end(Entity* e) { order_.push_back(e); }

void Entities::remove(Session& s, Entity* e) {
    if (!e || (e->flags_a & kFlagARemoved)) return;
    e->flags_a |= kFlagARemoved;
    order_.erase(std::remove(order_.begin(), order_.end(), e), order_.end());
    uint32_t slot = e->handle & 0xFFFF;
    if (slot < kSlots && handles_[slot] == e) removed_[slot] = true;
    e->behaviour->destroy(s, *e);
}

void Entities::free_removed() {
    for (int slot = 0; slot < kSlots; slot++) {
        if (!removed_[slot]) continue;
        Entity* e = handles_[slot];
        handles_[slot] = nullptr, serials_[slot] = 0, removed_[slot] = false;
        if (e == head_) head_ = nullptr;
        for (auto it = owned_.begin(); it != owned_.end(); ++it)
            if (it->get() == e) {
                owned_.erase(it);
                break;
            }
    }
    // The high-water mark drops over trailing free slots.
    while (slots_used > 0 && !handles_[slots_used - 1]) slots_used--;
}

void Entities::set_order(const std::vector<Entity*>& order) {
    order_ = order;
    head_ = !order_.empty() && order_[0]->type == kTypeCamera ? order_[0] : head_;
}

Entity* Entities::resolve(uint32_t h) const { return h ? handles_[h & 0xFFFF] : nullptr; }

bool Entities::dead(uint32_t h) const {
    if (!h) return true;
    uint32_t slot = h & 0xFFFF;
    return slot >= kSlots || serials_[slot] != (h >> 16) || !handles_[slot];
}

Entity* Entities::find_type(int32_t type) const {
    for (Entity* e : order_)
        if (e->type == type) return e;
    return nullptr;
}

bool MessageQueue::push(const Message& m) {
    if (count_ == kSize) return false;
    ring_[(head_ + count_) % kSize] = m;
    count_++;
    return true;
}

bool MessageQueue::run_one(const std::function<void(const Message&)>& handler) {
    if (!count_) return false;
    Message m = ring_[head_];
    head_ = (head_ + 1) % kSize;
    count_--;
    if (handler) handler(m);
    return true;
}

void MessageQueue::run_all(const std::function<void(const Message&)>& handler) {
    while (run_one(handler)) {
    }
}

}  // namespace valor
