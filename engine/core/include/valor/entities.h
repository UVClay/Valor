// The entity list, handles and the deferred message queue (docs/re/entities.md: Lists and handles,
// Removal; objects.md: The frame and the tick). One list, walked in order by the tick: the camera
// at its head, then everything spawned (newest first, linked right after the camera), the world,
// the player's soldier, and projectiles at the end. A handle is (serial << 16) | slot; removal is
// immediate but the handle's slot is freed only at the end of the frame.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace valor {

class Session;
struct Entity;

// What an entity does: the think (+0x36C), the move (entity_move and its contact handlers) and the
// destructor (+0x384). Kinds the core doesn't run yet get the default: nothing.
struct Behaviour {
    virtual ~Behaviour() = default;
    virtual void think(Session&, Entity&) {}
    virtual void move(Session&, Entity&) {}
    virtual void destroy(Session&, Entity&) {}
};

enum : uint32_t {
    kFlagAPlayer = 0x1,
    kFlagAWalker = 0x8,
    kFlagAGravity = 0x20,
    kFlagANoMove = 0x20000,
    kFlagARemoved = 0x80000,
    kFlagADead = 0x20000000,
};

enum : int32_t {
    kTypeSoldierPlayer = 1,
    kTypeCamera = 2,
    kTypeWorld = 3,
    kTypeLadder = 0x4C,
};

struct Entity {
    uint32_t handle = 0;   // +0x0
    int32_t type = 0;      // +0x8
    uint32_t flags_a = 0;  // +0xC
    uint32_t flags_b = 0;  // +0x10
    int record = -1;       // +0x1C: the record it came from (an index), -1 none
    std::unique_ptr<Behaviour> behaviour;
};

class Entities {
public:
    static constexpr int kSlots = 256;

    // entity_create's handle_alloc (0x8006D930): the next slot while the high-water mark is
    // below 256, else the first free one; the serial counter +1. Not linked yet.
    Entity* create(int32_t type, std::unique_ptr<Behaviour> b);
    // An entity whose handle is given (one the original made: a seed, or a system not ported).
    Entity* adopt(uint32_t handle, int32_t type, std::unique_ptr<Behaviour> b);

    void set_head(Entity* e);    // the camera, g_player: the list's head
    void link_front(Entity* e);  // entity_link_front (0x8006B834): right after the head
    void link_end(Entity* e);    // entity_link (0x8006B854): at the end (projectiles)
    // entity_remove (0x8006B8F0): flagged, unlinked, the handle marked removed, the destructor.
    void remove(Session& s, Entity* e);
    // handles_free_removed (0x8006DA88), at the end of the frame.
    void free_removed();

    Entity* resolve(uint32_t h) const;  // h ? slot's entity : null (the serial isn't checked)
    bool dead(uint32_t h) const;        // handle_dead (0x8006D8AC)
    const std::vector<Entity*>& list() const { return order_; }
    // The list in this order (a seed, or following the original's after a system not ported
    // linked something); the first is the head when it is the camera.
    void set_order(const std::vector<Entity*>& order);
    Entity* find_type(int32_t type) const;

    uint32_t serial = 0;      // 0x8009D670
    uint32_t slots_used = 0;  // 0x8009D674

private:
    std::array<Entity*, kSlots> handles_{};
    std::array<uint32_t, kSlots> serials_{};
    std::array<bool, kSlots> removed_{};
    std::vector<Entity*> order_;
    std::vector<std::unique_ptr<Entity>> owned_;
    Entity* head_ = nullptr;
};

// The level's message queue: send_event appends a context, the end of the frame runs them
// (objects.md: level-script messages are deferred). 128 entries, a ring.
struct Message {
    uint32_t msg = 0;
    int sender_record = -1;  // the record object (a volume, a pair), -1 none
    uint32_t sender = 0;     // a handle
    uint32_t actor = 0;      // a handle: who entered, used, ...
};

class MessageQueue {
public:
    static constexpr size_t kSize = 128;
    bool push(const Message& m);
    // Runs one (the idle job) or all, with the level logic's handler (not ported: the default
    // keeps them for the host to read).
    bool run_one(const std::function<void(const Message&)>& handler);
    void run_all(const std::function<void(const Message&)>& handler);
    size_t size() const { return count_; }

private:
    std::array<Message, kSize> ring_{};
    size_t head_ = 0, count_ = 0;
};

}  // namespace valor
