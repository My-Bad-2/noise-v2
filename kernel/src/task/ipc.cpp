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

    while (this->count == 0) {
        receiver->state = ThreadState::Blocked;

        this->blocked_recievers.push_back(*receiver);
        guard.unlock();

        Scheduler::get().block();

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
}  // namespace kernel::task