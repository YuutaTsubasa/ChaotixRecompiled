// The netplay wire format (issue #46, docs/NETPLAY.md has the exchange).
//
// Every datagram is "CXNP", a message type byte and the message's fields,
// little-endian; strings are a 16-bit length and the bytes. Everything after
// the handshake carries the session id the host chose, so datagrams from an
// earlier session are ignored.
//
//   client                         host
//   HELLO (identity)     ->
//                        <-        WELCOME (identity, session, settings, state size)
//                                  or REJECT (why)
//   STATE_REQUEST (chunk numbers) ->
//                        <-        STATE_CHUNK (one chunk of the start state)
//   READY                ->        (the host starts at frame 0)
//                        <-        START (the client starts at frame 0)
//   INPUT                <->       INPUT (frame, ack, pads, hashes, ping)
//   BYE / DESYNC         <->
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chaotix::net {

// Bumped whenever a message changes.
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kChunkSize = 1024;
constexpr size_t kMaxInputsPerMessage = 120;
constexpr size_t kMaxHashesPerMessage = 4;

// What both players must have in common for their games to stay the same.
struct Identity {
    uint16_t protocol = kProtocolVersion;
    std::string game_version;  // this program's version
    std::string rom_sha1;      // the ROM
    std::string data_sha1;     // the image that runs: after ROM patches and character stats
    // Host settings that change what the game does: "name=value" lines (the
    // replacement music, ...). Compared line by line.
    std::string must_match;
};
// Why two players cannot play together, in words for the player; empty if
// they can. `mine` is this side's.
std::string mismatch(const Identity& mine, const Identity& theirs);

enum class Msg : uint8_t {
    Hello = 1, Welcome, Reject, StateRequest, StateChunk, Ready, Start, Input, Bye, Desync,
};

struct Message {
    Msg type = Msg::Hello;
    uint32_t session = 0;
    // HELLO, WELCOME
    Identity identity;
    // WELCOME: the settings the client takes over for the session (opaque to
    // the session; net/machine_game.h), and the start state's size.
    std::string settings;
    uint32_t state_size = 0;
    uint32_t chunks = 0;
    // REJECT, BYE: why.
    std::string reason;
    // STATE_REQUEST: the chunks wanted. STATE_CHUNK: its number and bytes.
    std::vector<uint32_t> wanted;
    uint32_t index = 0;
    std::vector<uint8_t> data;
    // INPUT: the sender's frame, the frames of the receiver's pads it has
    // (every one before `ack`), how far ahead of the receiver it thinks it is,
    // ping times (the sender's clock, and the last one it got from the
    // receiver), and its pads for frames [first, first + inputs.size()).
    // DESYNC: frame is where the hashes differed.
    uint32_t frame = 0;
    uint32_t ack = 0;
    int8_t advantage = 0;
    uint32_t send_ms = 0, echo_ms = 0;
    uint32_t first = 0;
    std::vector<uint16_t> inputs;
    std::vector<std::pair<uint32_t, uint64_t>> hashes;  // (frame, hash) of confirmed states
};

std::vector<uint8_t> encode(const Message& m);
// False for anything that is not a whole message of this protocol.
bool decode(const uint8_t* data, size_t n, Message& out);

} // namespace chaotix::net
