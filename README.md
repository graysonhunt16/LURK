# Lurk Server

## Overview

Lurk Server is a multithreaded TCP server written in C that manages client connections for a simple multiplayer, room-based interaction system. It handles multiple clients concurrently, maintains shared state (such as rooms and characters), and facilitates communication between connected users.

The server listens for incoming connections, assigns clients to available slots, and spawns threads to manage each client’s interaction. It also supports broadcasting messages and character data to other clients within the same room.

---

## Features

* **Concurrent Client Handling** using POSIX threads (`pthread`)
* **Room-Based Communication** between connected clients
* **Character Broadcasting** to synchronize player state
* **Server Narration System** (e.g., "Professor" messages)
* **Thread-Safe Shared State** using mutex locks
* **Utility Helpers** for safer networking and data handling
* **Room Management System** for organizing players

---

## Project Structure

```
.
├── main.c             # Entry point, server setup and connection handling
├── client_thread.c    # Client thread logic and communication handling
├── rooms.c            # Room creation, management, and player grouping
├── utils.c            # Helper functions (safe send, parsing, utilities)
├── lurk.h             # Shared definitions, structs, constants, prototypes
```

---

## How It Works

### 1. Server Initialization

* Sets up signal handling and initializes rooms (via `rooms.c`).
* Creates a TCP socket and binds to port **5035**.
* Starts listening for incoming client connections.

### 2. Client Connection

* Accepts incoming connections in a loop.
* Assigns each client to an available slot in a global `clients` array.
* Stores metadata such as file descriptor, IP address, and state.

### 3. Multithreading

* Each client is handled in a separate thread (`client_thread.c`).
* Threads interact with shared resources protected by mutexes:

  * `clients_lock` for client state
  * Additional locks for shared systems (e.g., rooms or game entities)

### 4. Communication

* **Safe Send:** Implemented in `utils.c` to ensure reliable transmission.
* **Broadcasting:** Sends updates (e.g., character data) to all clients in the same room.
* **Narration System:** Sends special system messages (e.g., from "Professor").

### 5. Room Management

* Implemented in `rooms.c`
* Handles:

  * Room creation and initialization
  * Assigning clients to rooms
  * Broadcasting within specific rooms

---

## Key Concepts

### Client Structure

Each client maintains:

* Connection file descriptor
* Room assignment
* Character data
* Connection state flags (`in_use`, `started`, `alive`)

### Thread Safety

* Shared data is protected using mutexes.
* Locks are minimized and released before network I/O to prevent blocking.

### Utilities (`utils.c`)

* Wrapper functions for safer socket communication
* Common helpers used across modules

### Message Format

* Custom binary protocol
* Includes headers, sender/recipient fields, and payload
* Supports system/narrator flags

---

## Building the Project

### Requirements

* GCC or compatible C compiler
* POSIX-compliant system (Linux/macOS)
* pthread library

### Compile

```bash
gcc -o lurk_server main.c client_thread.c rooms.c utils.c -lpthread
```

### Run

```bash
./lurk_server
```

Server will start listening on:

```
Port: 5035
```

---

## Usage

* Connect via a TCP client (e.g., telnet, netcat, or a custom client).
* Multiple clients can connect simultaneously.
* Clients interact within assigned rooms and receive broadcasts.

---

## Future Improvements

* Authentication and user accounts
* Expanded gameplay (combat, items, NPCs)
* Detailed protocol documentation
* Logging and debugging tools
* Graceful shutdown and cleanup

---

## Notes

* The server assumes a fixed maximum number of clients (`MAX_CLIENTS`).
* Modular design separates networking, threading, utilities, and room logic.
* Suitable as a foundation for multiplayer or game server projects.

---

## License

This project is provided as-is for educational or development purposes.
