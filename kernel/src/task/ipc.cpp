#include "task/ipc.hpp"
#include "task/scheduler.hpp"

namespace kernel::task {
bool IPCPort::send(Thread* sender, const uint8_t* data, size_t len) {
    if (len > MAX_MSG_DATA) {
        return false;
    }

    LockGuard guard(this->lock);

    while (this->count >= PORT_QUEUE_CAPACITY) {
        sender->state = Blocked;
        this->blocked_senders.push_back(*sender);

        guard.unlock();

        Scheduler::get().block();
        guard.lock();
    }

    IPCMessage& msg = this->messages[this->tail];
    msg.sender_pid  = sender->owner->pid;
    msg.length      = len;

    memcpy(msg.data, data, len);

    this->tail = (this->tail + 1) % PORT_QUEUE_CAPACITY;
    this->count++;

    // Wake up a Reciever
    if (!this->blocked_recievers.empty()) {
        Thread* receiver = &this->blocked_recievers.front();
        this->blocked_recievers.pop_front();

        Scheduler::get().unblock(receiver);
    }

    return true;
}

size_t IPCPort::receive(Thread* receiver, uint8_t* out_buf, size_t max_len) {
    LockGuard guard(this->lock);

    size_t port_id = this->id;

    while (this->count == 0) {
        receiver->state = ThreadState::Blocked;

        this->blocked_recievers.push_back(*receiver);
        guard.unlock();

        Scheduler::get().block();

        if (PortManager::get().is_valid_port(port_id)) {
            return 0;
        }

        guard.lock();
    }

    IPCMessage& msg = this->messages[this->head];

    size_t copy_len = (msg.length < max_len) ? msg.length : max_len;
    memcpy(out_buf, msg.data, copy_len);

    this->head = (this->head + 1) % PORT_QUEUE_CAPACITY;
    this->count--;

    if (!this->blocked_senders.empty()) {
        Thread* sender = &this->blocked_senders.front();
        this->blocked_senders.pop_front();
        Scheduler::get().unblock(sender);
    }

    return copy_len;
}

void IPCPort::close() {
    LockGuard guard(this->lock);

    while (!this->blocked_recievers.empty()) {
        Thread* t = &this->blocked_recievers.front();
        this->blocked_recievers.pop_front();
        Scheduler::get().unblock(t);
    }

    while (!this->blocked_senders.empty()) {
        Thread* t = &this->blocked_senders.front();
        this->blocked_senders.pop_front();

        Scheduler::get().unblock(t);
    }

    this->head  = 0;
    this->tail  = 0;
    this->count = 0;
}

size_t PortManager::create_port() {
    WriteGuard guard(this->lock);

    uint32_t index;
    IPCPort* new_port;

    if (!this->free_indices.empty()) {
        index = this->free_indices.back();
        this->free_indices.pop_back();
    } else {
        index = static_cast<uint32_t>(this->table.size());
        this->table.push_back(PortEntry{});
    }

    this->table[index].port = new IPCPort(index);
    size_t handle           = (static_cast<size_t>(this->table[index].generation) << 32) | index;
    return handle;
}

IPCPort* PortManager::get_port(size_t handle) {
    uint32_t index = handle & 0xFFFFFFFF;
    uint32_t gen   = handle >> 32;

    ReadGuard guard(this->lock);

    if (index >= this->table.size()) {
        return nullptr;
    }

    // If the slot's generation doesn't match the handle's,
    // the handle is stale (the port was destroyed and recreated).
    if (table[index].generation != gen) {
        return nullptr;
    }

    return this->table[index].port;
}

void PortManager::destroy_port(size_t handle) {
    uint32_t index = handle & 0xFFFFFFFF;
    uint32_t gen   = handle >> 32;

    IPCPort* target = nullptr;

    {
        WriteGuard(this->lock);

        if (index >= this->table.size()) {
            return;
        }

        // Stale handle
        if (this->table[index].generation != gen) {
            return;
        }

        target                  = this->table[index].port;
        this->table[index].port = nullptr;

        // Increment generation so old handles become invalid
        this->table[index].generation++;

        this->free_indices.push_back(index);
    }

    if (target) {
        target->close();
        delete target;
    }
}

bool PortManager::is_valid_port(size_t handle) {
    uint32_t index = handle & 0xFFFFFFFF;
    uint32_t gen   = handle >> 32;

    ReadGuard guard(this->lock);

    if (index >= this->table.size()) {
        return false;
    }

    // if the handle's generation doesn't match the table's
    // it means the original port was deleted and a new one
    // was created at the same index. This handle is STALE
    // and must be rejected.
    if (this->table[index].generation != gen) {
        return false;
    }

    if (this->table[index].port == nullptr) {
        return false;
    }

    return true;
}

PortManager& PortManager::get() {
    static PortManager manager;
    return manager;
}
}  // namespace kernel::task