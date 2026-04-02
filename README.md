# Anonymous Messaging Network (C++17)

A production-style local simulation of onion routing concepts (Tor-inspired, simplified for academic use).

## What This Project Demonstrates

- Multi-hop anonymous routing over TCP on localhost
- Layered encryption (onion model) with at least 3 hops
- Each relay knows only its previous connection and next hop
- Final receiver obtains plaintext message only after all layers are peeled
- Concurrent node and server handling using `std::thread`

## Project Structure

```text
anonymous-network/
├── CMakeLists.txt
├── README.md
├── web/
│   ├── package.json
│   ├── server.js
│   └── public/
│       ├── index.html
│       ├── script.js
│       └── style.css
├── common/
│   ├── include/common/
│   │   ├── config.hpp
│   │   ├── crypto.hpp
│   │   ├── logger.hpp
│   │   ├── protocol.hpp
│   │   └── socket_utils.hpp
│   └── src/
│       ├── config.cpp
│       ├── crypto.cpp
│       ├── logger.cpp
│       ├── protocol.cpp
│       └── socket_utils.cpp
├── client/
│   ├── include/client/
│   │   └── client_app.hpp
│   └── src/
│       ├── client_app.cpp
│       └── main.cpp
├── node/
│   ├── include/node/
│   │   └── relay_node.hpp
│   └── src/
│       ├── main.cpp
│       └── relay_node.cpp
├── server/
│   ├── include/server/
│   │   └── final_server.hpp
│   └── src/
│       ├── final_server.cpp
│       └── main.cpp
└── config/
    └── network.conf
```

## Onion Routing Concept in This Implementation

Client builds route: `Client -> RelayA -> RelayB -> RelayC -> Server`.

Given message `HELLO`, the client wraps layers from inside out:

1. Innermost layer says: next hop = `Server`, payload = `HELLO`
2. Encrypt layer with key of `RelayC`
3. Wrap: next hop = `RelayC`, payload = previous ciphertext
4. Encrypt with key of `RelayB`
5. Wrap: next hop = `RelayB`, payload = previous ciphertext
6. Encrypt with key of `RelayA` (outermost)

At runtime:

- `RelayA` decrypts only its layer and learns next hop `RelayB`
- `RelayB` decrypts only its layer and learns next hop `RelayC`
- `RelayC` decrypts only its layer and learns next hop `Server`
- `Server` receives plaintext `HELLO`

No relay can see both original sender identity and final destination semantics beyond immediate next hop.

## Encryption Design

- Current cipher: XOR stream-style transform (mandatory baseline)
- Pluggable architecture: `Cipher` interface in `common/crypto.hpp`
- Future upgrade path: implement AES class (e.g., OpenSSL) and inject it in node/client

## Build Instructions

### Using CMake (recommended)

```bash
cd anonymous-network
cmake -S . -B build
cmake --build build -j
```

Generated binaries:

- `build/anon_client`
- `build/anon_node`
- `build/anon_server`

### Optional direct g++ build

You can compile manually with C++17 and pthreads:

```bash
cd anonymous-network
mkdir -p build

g++ -std=c++17 -pthread -Icommon/include -Iclient/include \
    client/src/main.cpp client/src/client_app.cpp \
    common/src/config.cpp common/src/crypto.cpp common/src/logger.cpp \
    common/src/protocol.cpp common/src/socket_utils.cpp \
    -o build/anon_client

g++ -std=c++17 -pthread -Icommon/include -Inode/include \
    node/src/main.cpp node/src/relay_node.cpp \
    common/src/config.cpp common/src/crypto.cpp common/src/logger.cpp \
    common/src/protocol.cpp common/src/socket_utils.cpp \
    -o build/anon_node

g++ -std=c++17 -pthread -Icommon/include -Iserver/include \
    server/src/main.cpp server/src/final_server.cpp \
    common/src/config.cpp common/src/crypto.cpp common/src/logger.cpp \
    common/src/protocol.cpp common/src/socket_utils.cpp \
    -o build/anon_server
```

## Run Instructions (Multi-terminal)

Open 5 terminals from `anonymous-network`.

1. Start final server:

```bash
./build/anon_server 9100
```

2. Start relay nodes (at least 3, each unique key/port):

```bash
./build/anon_node A 9001 alpha_key 20 120
./build/anon_node B 9002 bravo_key 20 120
./build/anon_node C 9003 charlie_key 20 120
```

Optional extra relay (for larger random pool):

```bash
./build/anon_node D 9004 delta_key 20 120
```

3. Send message from client:

```bash
./build/anon_client config/network.conf HELLO 3
```

## Example Execution Flow (Expected)

Client logs:

- selected random route (3 relays)
- each wrapping layer and encrypted preview
- dispatch to first hop

Each relay logs:

- incoming encrypted frame
- post-decrypt layer bytes (onion peeling)
- discovered forwarding destination
- forwarded payload preview

Server logs:

- incoming connection
- final plaintext received: `HELLO`

## Failure Handling

- Client retries connecting to first hop
- Relay retries connecting to next hop
- Invalid layers/instructions are dropped with warnings
- Socket failures are logged without crashing listener loops

## Anonymity Properties (Simplified)

- No single relay knows full path
- Relay only sees previous TCP peer and embedded next hop
- Final server gets plaintext but not original route details

## Limitations

- XOR is not cryptographically secure
- No key exchange / PKI / perfect forward secrecy
- No cover traffic, circuit rotation, congestion control, or directory service
- Localhost simulation only

This project is for education and systems design learning, not real-world privacy security.

## Web Dashboard (Control + Visualization Layer)

The project now includes a lightweight web UI that controls and visualizes the existing C++ executables.

### What the web layer does

- Starts and manages `anon_server` and all relay `anon_node` processes
- Sends messages by invoking `anon_client`
- Captures stdout logs from client, relays, and server
- Exposes API endpoints for status and live log polling
- Renders route visualization, status indicators, and hop-by-hop logs in a browser

The C++ core routing logic is unchanged.

### Web setup

```bash
cd anonymous-network/web
npm install
node server.js
```

Open:

- `http://localhost:8080`

### Web demo flow

1. Enter message (e.g., `HELLO`)
2. Choose hop count (1-5 in UI)
3. Click **Send Anonymous Message**
4. UI shows:
    - selected route (Client -> Node -> ... -> Server)
    - live relay/server/client logs
    - delivery status transitions: Sending -> In Transit -> Delivered

### Notes about hop count

- UI allows 1-5 for demonstration flexibility
- C++ backend requires at least 3 hops, so lower values are auto-adjusted
- If requested hops exceed configured relay count, the value is reduced automatically

