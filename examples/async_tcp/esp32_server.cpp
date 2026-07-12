// SPDX-License-Identifier: MIT
//
// esp32_server.cpp -- ESP32 side of the async-TCP serialization example.
//
// The ESP32 runs an ecomm `esp_async_wifi_channel` (a non-blocking TCP
// *server* built on AsyncTCP). A Raspberry Pi -- or any computer -- connects
// to it as a client using the Python `ecomm.channels.AsyncTcpChannel` (see
// pi_client.py in this directory). The Pi serializes a small {name, age}
// record into a packet payload; this firmware parses those fields and sends
// back a greeting string. It demonstrates that ecomm carries an opaque
// payload -- the application defines the byte layout on both sides.
//
// This is an Arduino-framework sketch written as a .cpp. Build it with
// PlatformIO or the Arduino IDE for an ESP32 board. See README.md for the
// wiring, dependencies, and the all-important schema-matching table.
//
// Copyright (c) 2026 Mark Tikhonov -- MIT License. See LICENSE.

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>

#include <cstdio>
#include <cstring>

#include <ecomm/protocol/protocol.hpp>
#include <ecomm/channels/esp_async_wifi_channel.hpp>

using namespace ecomm::protocol;
using namespace ecomm::channels;

// ---------------------------------------------------------------------------
// Configuration -- EDIT THESE
// ---------------------------------------------------------------------------

static constexpr char WIFI_SSID[]     = "YOUR_WIFI_SSID";
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
static constexpr uint16_t TCP_PORT    = 3333;

// This board's ecomm identity. The Python client addresses packets to this
// id, and this must equal ECOMM_BOARD_ID (default 1). If you override the
// board id with a build flag (-DECOMM_BOARD_ID=N), change ESP32_BOARD_ID in
// pi_client.py to match.
static constexpr uint8_t THIS_BOARD_ID = ECOMM_BOARD_ID;  // == 1 by default

// ---------------------------------------------------------------------------
// Wire schema -- MUST MATCH the Python PacketSchema field-for-field.
//
//   packet<PacketSize, Topology, SequencePolicy, ChecksumPolicy>
//
// 64-byte packets, network topology (sender_id/receiver_id present),
// no sequence field, no checksum (TCP already guarantees integrity).
// 64 is a multiple of the ESP32's 4-byte word, satisfying packet<>'s
// word-alignment static_assert.
// ---------------------------------------------------------------------------

using app_packet = packet<64, topology::network, no_sequence, none>;

// AsyncServer must outlive the channel; both are file-scope statics here.
static AsyncServer server(TCP_PORT);
static esp_async_wifi_channel<app_packet> channel(server);

// ---------------------------------------------------------------------------
// Application payload layout (our protocol, not ecomm's). Little-endian, to
// match ecomm's wire convention -- though every field here is a single byte.
//
//   request : [name_len : u8][name : name_len bytes][age : u8]
//   reply   : [greeting_len : u8][greeting : greeting_len bytes]
// ---------------------------------------------------------------------------

// Parse a {name, age} record out of a received payload. Returns false if the
// bytes do not form a well-formed record that fits the payload -- always
// bounds-check data that came off the wire.
static bool parse_person(const app_packet& in, char* name_out, std::size_t name_cap, std::uint8_t& age_out) {
    constexpr std::size_t cap = app_packet::payload_size;
    const auto* p = reinterpret_cast<const std::uint8_t*>(in.payload);

    const std::uint8_t name_len = p[0];
    // Need: 1 (len) + name_len (name) + 1 (age) bytes, and room in name_out.
    if (1u + name_len + 1u > cap) return false;
    if (name_len + 1u > name_cap) return false;

    std::memcpy(name_out, p + 1, name_len);
    name_out[name_len] = '\0';
    age_out = p[1u + name_len];
    return true;
}

// Serialize a length-prefixed greeting string into a reply payload.
static void write_greeting(app_packet& out, const char* greeting) {
    constexpr std::size_t cap = app_packet::payload_size;
    auto* p = reinterpret_cast<std::uint8_t*>(out.payload);

    std::size_t glen = std::strlen(greeting);
    if (glen > cap - 1) glen = cap - 1;  // clamp so [len][bytes] fits the payload
    p[0] = static_cast<std::uint8_t>(glen);
    std::memcpy(p + 1, greeting, glen);
}

// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.printf("\n[ecomm async_tcp] connecting to Wi-Fi \"%s\"", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print('.');
    }
    Serial.println();
    Serial.print("[ecomm async_tcp] connected. Point the Pi client at ");
    Serial.print(WiFi.localIP());
    Serial.printf(":%u\n", TCP_PORT);

    // Start accepting connections. The channel already registered its
    // AsyncServer callbacks in its constructor above.
    server.begin();
    Serial.printf("[ecomm async_tcp] listening as board id %u\n", THIS_BOARD_ID);
}

void loop() {
    // try_receive() is non-blocking: it pops a complete, validated packet
    // from the channel's ring queue, or returns std::nullopt.
    if (auto received = channel.try_receive()) {
        app_packet& in = *received;

        char name[app_packet::payload_size];
        std::uint8_t age = 0;
        if (not parse_person(in, name, sizeof(name), age)) {
            Serial.printf("[recv] from board %u: malformed record, ignoring\n", in.header.sender_id);
            return;
        }
        Serial.printf("[recv] from board %u: name=\"%s\" age=%u\n", in.header.sender_id, name, age);

        // Build a greeting reply that uses the parsed name, addressed back to
        // whoever sent the record.
        char greeting[app_packet::payload_size];
        std::snprintf(greeting, sizeof(greeting), "hello %s, it's your esp", name);

        app_packet out{header_type::data, header_options::none};
        out.header.receiver_id = in.header.sender_id;  // reply to the sender
        // out.header.sender_id defaults to ECOMM_BOARD_ID (this board).
        write_greeting(out, greeting);

        (void)channel.send(out);  // send() seals + transmits; returns send_result
        Serial.printf("[send] to board %u: \"%s\"\n", out.header.receiver_id, greeting);
    }

    delay(1);  // keep the loop cooperative; AsyncTCP runs on its own task
}
