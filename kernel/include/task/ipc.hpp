#pragma once

#include "task/process.hpp"

#define PORT_QUEUE_CAPACITY 32
#define MAX_MSG_DATA        256

namespace kernel::task {
struct IPCMessage {
    size_t sender_pid;
    size_t length;
    uint8_t data[MAX_MSG_DATA];
    size_t timestamp;
    size_t message_id;
};

struct IPCPort {
    size_t id;
    SpinLock lock;

    IPCMessage messages[PORT_QUEUE_CAPACITY];
    size_t head  = 0;
    size_t tail  = 0;
    size_t count = 0;

    IntrusiveList<Thread, WaitTag> blocked_recievers;
    IntrusiveList<Thread, WaitTag> blocked_senders;

    IPCPort(size_t id) : id(id) {}

    bool send(Thread* sender, const uint8_t* data, size_t len);
    size_t receive(Thread* receiver, uint8_t* out_buf, size_t max_len);
};
}  // namespace kernel::task