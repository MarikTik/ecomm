# ecomm
[![Ask DeepWiki](https://devin.ai/assets/askdeepwiki.png)](https://deepwiki.com/MarikTik/ecomm)

`ecomm` is a flexible, header-only C++17 library for creating robust, packet-based communication protocols, designed primarily for embedded systems and microcontrollers (e.g., Arduino, ESP32). It provides a modular and policy-based framework to define, transmit, and validate data packets over various communication media.

The library is built on three core concepts: a flexible **Protocol** definition layer, an extensible **Interface** abstraction for communication hardware, and a central **Hub** to manage multiple communication channels simultaneously.

## Features

*   **Flexible Protocol Design**: Define custom, fixed-size packets with the `framed_packet` template.
*   **Policy-Based Checksums**: Easily add data integrity checks using policies like `crc32`, `crc16`, `sum16`, and more.
*   **Abstracted Communication Interfaces**: A common CRTP-based `interface` allows for easy integration of different communication media.
*   **Pre-built Interfaces**: Includes ready-to-use interfaces for Arduino `HardwareSerial` and `WiFi` (ESP32/ESP8266).
*   **Multi-Interface Management**: The `hub` class acts as a central dispatcher to send and receive data across multiple interfaces at once.
*   **Automatic Validation**: The library automatically handles packet sealing (calculating checksums) before sending and validation upon receiving.
*   **Header-Only & CMake-Ready**: Integration is simple with CMake's `FetchContent`. The library is header-only, requiring no separate compilation.

## Core Concepts

### Protocol (`ecomm::protocol`)

The protocol layer provides the building blocks for your communication packets.

*   **`packet_header`**: A compact 24-bit header containing metadata like packet type, priority, flags, and sender/receiver IDs.
*   **`basic_packet`**: A simple, fixed-size packet structure containing a header, task ID, and payload, but no checksum.
*   **`framed_packet`**: The primary packet structure, extending `basic_packet` with a configurable checksum field (Frame Check Sequence, FCS). The checksum algorithm is chosen via a template policy.
*   **`validator`**: A system that automatically seals packets with a checksum before sending and validates them upon receipt.

### Interfaces (`ecomm::interfaces`)

The interface layer abstracts the underlying communication hardware.

*   **`interface`**: A CRTP base class that defines the `send()` and `try_receive()` API. It automatically integrates the `validator` to ensure all communications are sealed and checked.
*   **`arduino_serial_interface`**: An implementation for UART communication using Arduino's `HardwareSerial`.
*   **`arduino_wifi_interface`**: An implementation for TCP communication using Arduino's `WiFiServer`.

### Hub (`ecomm::hub`)

The `hub` is a central manager that simplifies handling one or more communication interfaces. It can broadcast a packet to all active interfaces and poll all interfaces for incoming data, returning the first valid packet it finds.

## Getting Started

`ecomm` is a header-only library best integrated into a project using CMake and `FetchContent`. Its dependency, `etools`, is fetched automatically.

### Installation (CMake)

Add the following to your `CMakeLists.txt` to fetch and link `ecomm`:
```cmake
include(FetchContent)

FetchContent_Declare(
  ecomm
  GIT_REPOSITORY https://github.com/MarikTik/ecomm.git
  GIT_TAG main # Or a specific release tag
)
FetchContent_MakeAvailable(ecomm)

# ... later in your CMakeLists.txt
# Link ecomm to your target (e.g., your firmware executable)
target_link_libraries(your_target_name PRIVATE ecomm)
```

### Quick Start (Arduino Example)

Here is a simple example for an Arduino-based platform demonstrating how to define a packet, set up an interface, and use the hub to communicate.

```cpp
#include <Arduino.h>
#include <ecomm/hub/hub.hpp>
#include <ecomm/interfaces/arduino_serial_interface.hpp>
#include <ecomm/protocol/protocol.hpp>

// 1. Define a custom packet type.
// This is a 32-byte packet with a uint16_t task ID and a crc32 checksum.
using MyPacket = ecomm::protocol::framed_packet<32, uint16_t, ecomm::protocol::crc32>;

// 2. Create a hub to manage interfaces.
// The hub will manage communication for `MyPacket` types over a serial interface.
auto comm_hub = ecomm::hub<MyPacket, ecomm::interfaces::arduino_serial_interface<>>{
    ecomm::interfaces::arduino_serial_interface{Serial}
};

void setup() {
  Serial.begin(115200);
  while (!Serial); // Wait for serial connection
}

void loop() {
  // 3. Create and send a packet.
  // This packet is destined for a device with ID 1.
  ecomm::protocol::packet_header header{
      ecomm::protocol::header_type::data, // type
      false,                              // encrypted
      false,                              // fragmented
      0,                                  // priority
      ecomm::protocol::header_flags::none,// flags
      true,                               // validated (indicates checksum presence)
      false,                              // reserved
      1                                   // receiver_id
  };

  uint16_t task_id = 101;
  MyPacket tx_packet(header, task_id, 0); // Header, task ID, status code

  // The hub automatically seals the packet (adds checksum) and sends it.
  comm_hub.send(tx_packet);
  Serial.println("Sent a packet.");


  // 4. Try to receive a packet.
  // The hub polls its interfaces and automatically validates incoming data.
  if (auto maybe_packet = comm_hub.try_receive()) {
    MyPacket& rx_packet = *maybe_packet;
    Serial.print("Received packet with Task ID: ");
    Serial.println(rx_packet.task_id);
    Serial.print("From Sender ID: ");
    Serial.println(rx_packet.header.sender_id());
  }

  delay(1000);
}
```

## Building

The project uses CMake as its build system. The following options can be configured:

*   `ECOMM_BUILD_EXAMPLES` (Default: `ON`): Build example executables.
*   `BUILD_TESTING` (Default: `ON`): Enable the test suite, which uses GoogleTest.

## License

This project is licensed under the **Business Source License 1.1 (BSL 1.1)**. It is free for non-commercial use. Commercial use requires a separate license. Please see the LICENSE file for more details.