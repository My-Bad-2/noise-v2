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
    LockGuard guard(this->lock);

    size_t new_id;
    IPCPort* new_port;

    if (!this->free_ids.empty()) {
        new_id = this->free_ids.back();
        this->free_ids.pop_back();

        new_port            = new IPCPort(new_id);
        this->ports[new_id] = new_port;
    } else {
        new_id   = this->ports.size();
        new_port = new IPCPort(new_id);

        this->ports.push_back(new_port);
    }

    return new_id;
}

IPCPort* PortManager::get_port(size_t id) {
    LockGuard guard(this->lock);

    if (id >= this->ports.size()) {
        return nullptr;
    }

    return this->ports[id];
}

void PortManager::destroy_port(size_t id) {
    IPCPort* target = nullptr;

    {
        LockGuard(this->lock);

        if (id >= this->ports.size() || this->ports[id] == nullptr) {
            return;
        }

        target          = this->ports[id];
        this->ports[id] = nullptr;

        this->free_ids.push_back(id);
    }

    if (target) {
        target->close();

        delete target;
    }
}

bool PortManager::is_valid_port(size_t id) {
    LockGuard guard(this->lock);

    if (id >= this->ports.size()) {
        return false;
    }

    if (this->ports[id] == nullptr) {
        return false;
    }

    return true;
}

PortManager& PortManager::get() {
    static PortManager manager;
    return manager;
}
}  // namespace kernel::task