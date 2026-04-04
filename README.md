# Distributed Task Coordinator (C++)

A high-performance distributed task scheduler built in C++, designed to simulate real-world job coordination across multiple workers using event-driven networking, lock-free queues, and asynchronous processing.

---

## Implemented Features

- Custom binary serialization layer (BufferReader/Writer)
- Lock-free Single Producer Single Consumer (SPSC) queue for inter-thread communication
- Message-based protocol for task submission, assignment, and completion
- Support for synthetic tasks and distributed word count workload

---

## In Progress / Planned

- Event-driven networking using TCP sockets
- Asynchronous scheduler (non-blocking task dispatch)
- Worker registration and heartbeat monitoring
- Fault-tolerant primary-backup replication
- Task retry and failure handling
- Load balancing strategies
- Performance benchmarking and latency analysis

---

## Tech Stack

- C++ (C++11)
- POSIX sockets (TCP)
- Multithreading (std::thread)
- Lock-free data structures (SPSC queue)

---

More to come....