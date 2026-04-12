# Distributed Task Coordinator (C++)

A high-performance distributed task scheduling system built in C++, simulating real-world job coordination across multiple workers. The system features sharded routing, fault-tolerant primary-backup replication, and low-latency task execution using custom networking and lock-free data structures.

---

## Implemented Features

### Core system
- Fully implemented **Coordinator (Scheduler)** handling task lifecycle:
  - Task submission, assignment, and completion
  - Queue management and scheduling logic
- **Worker nodes** executing synthetic and word count tasks
- **Router layer** for request distribution across shards with failover support

---

### Networking
- Custom TCP-based networking layer using POSIX sockets
- Binary message protocol with custom serialization (`BufferReader` / `BufferWriter`)
- Reliable request-response communication with timeout and reconnection handling
- Thread-local client connections for lock-free routing

---

### Concurrency & Performance
- Lock-free **Single Producer Single Consumer (SPSC)** queues for inter-thread communication
- Multithreaded architecture using `std::thread`
- Thread-per-connection model in router for scalable client handling
- Efficient message passing without shared-state contention

---

### Fault Tolerance & Replication
- **Primary-backup replication** between coordinators
- Event-based replication using:
  - `ASSIGNED_REPLICATE`
  - `COMPLETED_REPLICATE`
- Epoch-based consistency model
- Automatic failover handling in router (primary → backup)
- Eventual consistency maintained across replicas

---

### Task Model
- Support for multiple task types:
  - Synthetic workload (configurable duration)
  - Distributed word count
- Task state tracking:
  - Queued → Running → Completed
- Latency tracking via task timestamps
- Separation of control plane (coordinator) and execution plane (workers)

---

## Project Structure
```text
.
├── CMakeLists.txt
├── README.md
├── include
│   ├── config
│   │   └── system_config.hpp
│   ├── coordinator
│   │   ├── coordinator.hpp
│   │   └── types.hpp
│   ├── lock_free
│   │   └── spsc_queue.hpp
│   ├── message
│   │   └── message.hpp
│   ├── net
│   │   └── net_utils.hpp
│   ├── router
│   │   ├── router.hpp
│   │   └── types.hpp
│   ├── rpc
│   │   ├── client.hpp
│   │   └── server_connection.hpp
│   ├── serialization
│   │   └── buffer.hpp
│   ├── task
│   │   └── task.hpp
│   ├── utils
│   │   └── utils.hpp
│   └── worker
│       └── worker.hpp
└── src
    ├── coordinator
    │   └── coordinator.cpp
    ├── net
    │   └── net_utils.cpp
    ├── router
    │   └── router.cpp
    ├── rpc
    │   ├── client.cpp
    │   └── server_connection.cpp
    └── worker
        ├── main.cpp
        └── worker.cpp

```

The project is organized into modular, loosely coupled components to separate concerns across networking, scheduling, routing, and execution layers:

- `include/` — Header files for all system components
- `src/` — Implementation of coordinator, router, worker, and networking
- `serialization/` — Custom binary protocol utilities
- `lock_free/` — High-performance SPSC queue implementation

---

## In Progress / Planned

### Client & Interface
- Client-side CLI for task submission (interactive + automated workload generation)
- Input parsing for task types and parameters

---

### Observability
- Real-time **top-like monitoring** tool using `ncurses`
- System-level metrics:
  - Queue size, throughput, latency (avg, p95)
- Worker-level metrics:
  - Task distribution, heartbeat, performance

---

### System Enhancements

- Transition from primary-backup replication to **consensus-based replication (Raft / Paxos)** for stronger consistency guarantees
- Support for **multiple replicas per shard** to improve fault tolerance and availability
- Leader election and log agreement across replicas
- Improved recovery mechanisms for partial failures and network partitions
- Dynamic shard scaling and rebalancing
- Advanced load balancing strategies based on worker performance and system load
- Fine-grained latency tracking and percentile estimation improvements

---

### Engineering Enhancements

- Adopt additional **modern C++ (C++17/20)** features to further improve type safety and maintainability
- Replace sentinel values with `std::optional` to explicitly represent nullable fields and eliminate ambiguity
- Refactor task and message handling to use safer, more expressive type abstractions

---

## Tech Stack

- C++ (C++11)
- POSIX sockets (TCP)
- Multithreading (`std::thread`)
- Lock-free data structures (SPSC queue)
- Custom binary serialization

---

## Architecture Overview
```text
  Client (planned)
    ↓
  Router (sharding + failover)
    ↓
 Coordinator (primary / backup)
    ↓
  Workers
```
---

## Status

- Core distributed system implemented
- Networking and replication complete
- Fault-tolerant routing operational
- Client interface, observability (top-like monitoring), and performance benchmarking in progress

---

## Notes
This project focuses on building a low-latency, fault-tolerant distributed system from scratch, emphasizing:

- Systems-level C++ design
- Concurrency without locks in hot paths
- Network protocol design
- Real-world failure handling

The system is designed with a focus on low-latency execution, fault tolerance, and scalability, drawing inspiration from real-world distributed schedulers and high-performance systems.

---

More to come....