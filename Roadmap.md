# **NaC Language Roadmap**

## **Stage 2 - Network and Web API Support (Completed)**

Objective: To make NaC usable in web and network applications.

1. **JSON support** [Done]
   * Parse JSON string -> NaC objects (`jsonParse`)
   * Stringify NaC objects -> JSON string (`jsonStringify`)
   * Integration with HTTP responses (`httpRequest`, `httpJson`)

2. **Package manager / module system** [Done]
   * File-based Module System (`moduleLoad`, `moduleRequire`)
   * Namespace Management (`moduleRegister`, `moduleGet`, `moduleNames`)
   * Local Repository Manager (`modules/<name>.json` via `moduleRequire`)

## **Stage 3: Advanced Core & Asynchrony**

1. **Async / Concurrent Architecture**

* Non-blocking HTTP requests: Ability to fire requests without freezing the main execution thread.
* Promise-like structure / Await: High-level syntax for handling asynchronous operations.
* Event Loop: A centralized manager to handle I/O events, timers, and callbacks efficiently.
* Async I/O: Support for non-blocking file system operations and input/output streams.

2. **Networking & Real-time Communication**

* Low-level Sockets: TCP/UDP support for building custom protocols or servers.
* WebSockets: Full-duplex communication channels over a single TCP connection for real-time apps.
* HTTP/2 Support: (Optional) Support for modern web performance standards.
