// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <array>
#include <cstddef>

#include "AlphaEvoProto3.h"
#include "AlphaEvoProtocol.h"
#include "SimagicLeds.h"

/// The wheel's LED ring protocol.
///
/// This is a *second* protocol, not an extension of the one in
/// AlphaEvoProtocol.h. Both live on feature report 0x80 and byte 1 is what tells
/// them apart: the older one puts a block selector there (0x01, 0x02, 0x10) and
/// follows it with a fixed C struct, while this one puts 0x1E and follows a
/// ten-byte header with a protobuf 3 message. Nothing here changes the settings
/// path, and the two do not share a byte beyond the report id.
///
/// It was recovered from six captures of the ring being set to one, two, three,
/// five, nine and ten colours. The last three are two reports each, which is
/// where the fragmenting rules come from. The captures are in AlphaEvoBlocks.cpp
/// and the composer below is asserted against all six at compile time.
namespace hidcenter::simagic_alpha_evo {

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

/// Byte 0. Also the HID report id, which is why it shares a value with the
/// settings protocol's report id: SetFeature takes buffer byte 0 as the id, so
/// the direction marker and the report id are necessarily the same byte.
///
/// It stays byte 0 now the frames go out on an endpoint rather than through
/// SetFeature. A report id is part of the wire data on an interrupt transfer,
/// and the captures carry this byte either way.
inline constexpr quint8 kLedReportId = 0x80;

/// The endpoint the ring's frames go to.
///
/// The OUT half of endpoint 2, which belongs to the base's second interface —
/// the one the kernel binds no HID driver to and gives no hidraw node. So this
/// is unreachable through the report layer by construction, and the frames go
/// out as interrupt transfers on the endpoint itself.
inline constexpr quint8 kLedEndpoint = 0x02;

/// Byte 1. Says the payload is protobuf 3 rather than a packed struct, and is
/// the discriminator against the older block protocol's selectors.
inline constexpr quint8 kLedProtoFormatPb3 = 0x1E;

inline constexpr quint8 kLedDeviceTypeBase = 0x01;
inline constexpr quint8 kLedCmdGroupCtrl = 0x03;    ///< Control command group
inline constexpr quint8 kLedCmdRunEffects = 0x02;   ///< Run-effects command

/// The 10-byte header every packet in this protocol starts with.
///
/// Two things about it are easy to get backwards. `cmdCode` comes *before*
/// `cmdGroup`, which is the opposite of how they are named everywhere else. And
/// `payloadTotalSize` is big-endian, alone in an otherwise little-endian
/// protocol — the capture encodes a 37-byte payload as `00 25`.
#pragma pack(push, 1)
struct LedPacketHeader
{
    quint8 fixedHeader;        ///< kLedReportId
    quint8 dataType;           ///< kLedProtoFormatPb3
    quint8 deviceType;         ///< kLedDeviceTypeBase
    quint8 deviceModel;        ///< BaseId; 8 for an Alpha Evo
    quint8 cmdCode;            ///< kLedCmdRunEffects
    quint8 cmdGroup;           ///< kLedCmdGroupCtrl
    quint16 payloadTotalSize;  ///< Whole payload across all frames, big-endian
    quint8 totalFrames;
    quint8 frameIndex;         ///< 1-based
};
#pragma pack(pop)

static_assert(sizeof(LedPacketHeader) == 10, "the packet header is exactly ten bytes");
static_assert(offsetof(LedPacketHeader, fixedHeader) == 0);
static_assert(offsetof(LedPacketHeader, dataType) == 1);
static_assert(offsetof(LedPacketHeader, deviceType) == 2);
static_assert(offsetof(LedPacketHeader, deviceModel) == 3);
static_assert(offsetof(LedPacketHeader, cmdCode) == 4);
static_assert(offsetof(LedPacketHeader, cmdGroup) == 5);
static_assert(offsetof(LedPacketHeader, payloadTotalSize) == 6);
static_assert(offsetof(LedPacketHeader, totalFrames) == 8);
static_assert(offsetof(LedPacketHeader, frameIndex) == 9);

// ---------------------------------------------------------------------------
// Payload constants
// ---------------------------------------------------------------------------

/// Values copied from the captures whose meaning is not known.
///
/// Each is named and separate rather than folded into the composer because the
/// first test against real hardware may well need one of them changed, and a
/// one-line edit here beats hunting through an encoder.
///
/// `kLedScale` is always 4, so it is not a
/// property of the ring the user picked; `kLedPartType` 1 is the light group;
/// `kLedId` 1 selects the ring among the base's light groups; `kLedCtrl` 1 is the
/// value always seen.
inline constexpr int kLedPartType = 1;
inline constexpr int kLedId = 1;
inline constexpr int kLedScale = 4;
inline constexpr int kLedCtrl = 1;

/// How long the base holds LED colours before forgetting them.
///
/// Measured on hardware: stop sending and the ring reverts to whatever it shows
/// by itself after about ten seconds. So a set of colours lasts only as long as
/// the host keeps restating them, and there is no "set and leave" for this.
inline constexpr int kLedForgetTimeoutMs = 10000;

/// How often the colours are retold.
///
/// Short enough that two refreshes in a row can be lost, or delayed behind a
/// slow settings write, and the third still lands before the base gives up.
inline constexpr int kLedRefreshIntervalMs = 3000;

static_assert(kLedRefreshIntervalMs * 3 < kLedForgetTimeoutMs,
              "refresh often enough that two missed refreshes still beat the timeout");

/// Where each colour stop sits, in the units the wire uses.
///
/// Stops are floating-point positions sent cast to an
/// integer, so this is a position on the ring rather than an index. Six captures
/// pin it:
///
///     1 colour   -> 0
///     2 colours  -> 0, 400
///     3 colours  -> 0, 256, 512
///     5 colours  -> 0, 160, 320, 480, 640
///     9 colours  -> 0, 80, 160, 240, 320, 400, 480, 560, 640
///     10 colours -> 0, 80, 160, 240, 320, 400, 480, 560, 640, 720
///
/// Fitting `i * (leds / n) * unit` across every ring size up to 4000 and every
/// unit up to 256 leaves exactly one solution: fifty LEDs at sixteen units each,
/// so the ring spans eight hundred units and stops land on LED boundaries.
///
/// The last two rows are what settle the rounding. Nine and ten stops share a
/// step of eighty, so nine ends at 640 and leaves the last five LEDs past the
/// final stop rather than reaching 720. A rule that spread the stops evenly, or
/// that floored `800 / n` rather than `50 / n`, would have put them eighty-eight
/// apart instead.
///
/// Four, six, seven and eight stops have not been captured. They follow from the
/// same formula, and the six counts that have been captured leave no room for a
/// different one, but if a count in that gap misbehaves on hardware this is
/// still the first thing to re-derive.
inline constexpr int kKeyIdRingLeds = 50;
inline constexpr int kKeyIdUnitsPerLed = 16;

constexpr int keyIdForStop(int index, int count, bool bPreciseAngles = false)
{
    if (count <= 1)
        return 0;
    if (!bPreciseAngles) // original behavior
        return index * (kKeyIdRingLeds / count) * kKeyIdUnitsPerLed;
    else
        return (800 / count) * index;
}

/// The wire's colour word: a protobuf fixed32, so little-endian, and its bytes
/// run red, green, blue. As an integer that puts red in the *low* byte, which is
/// the reverse of the usual 0x00RRGGBB and the single easiest thing here to get
/// backwards. The capture's `d3 20 29 00` is red 211, green 32, blue 41.
constexpr quint32 ledColourWord(const LedRgb &colour)
{
    return (quint32(colour.b) << 16) | (quint32(colour.g) << 8) | quint32(colour.r);
}

// ---------------------------------------------------------------------------
// Packet
// ---------------------------------------------------------------------------

/// Payload bytes a single report can carry, after the header.
inline constexpr int kLedFramePayload = kReportSize - int(sizeof(LedPacketHeader));

/// Ten stops encode to 106 payload bytes and two frames, the figure the ten-stop
/// capture carries in its header; sixteen, the current ceiling, come to 166 and
/// four.
///
/// The spare two frames are so the ceiling can move a little without this having
/// to. Running out is not silent — composeLedRing refuses, and the structural
/// check below composes the largest ring there can be and asserts that it
/// worked — but a compile error here is a worse way to find out than having the
/// room in the first place.
inline constexpr int kMaxLedFrames = 6;

/// Scratch capacities. Generous on purpose: overflowing is caught rather than
/// undefined, but a caught overflow is still a packet that does not get sent.
inline constexpr std::size_t kLedItemCapacity = 16;
inline constexpr std::size_t kLedKeyColorCapacity = 192;
inline constexpr std::size_t kLedPayloadCapacity = 224;

/// One composed packet: the reports to send, in order.
struct LedPacket
{
    std::array<std::array<quint8, kReportSize>, kMaxLedFrames> frames{};
    int frameCount = 0;
    int payloadSize = 0;
    bool ok = false;
};

/// Encode the RunEffects message that carries the ring's colours.
///
/// Field numbers are as observed in the captures: the control field is 1 and
/// the per-stop colour list is 5; inside that, the ring selector is 2, the light
/// group 3, the colour items 8 and the scale 9; inside an item, the position is 1
/// and the colour 2.
///
/// The ring uses explicit colours per stop. Another field is known to exist that
/// carries palette indices packed at a bit depth; the captures do not use it, so
/// do not switch to it without a capture that says otherwise.
constexpr proto3::Buffer<kLedPayloadCapacity> composeRunEffects(const LedRingColours &colours, bool bPreciseAngles = false)
{
    using namespace proto3;

    const int count = colours.count > kMaxLedStops ? kMaxLedStops : int(colours.count);

    Buffer<kLedKeyColorCapacity> keyColour;
    putVarintField(keyColour, 2, kLedId);
    putVarintField(keyColour, 3, kLedPartType);
    for (int i = 0; i < count; ++i) {
        Buffer<kLedItemCapacity> item;
        putVarintField(item, 1, quint64(keyIdForStop(i, count, bPreciseAngles)));
        putFixed32Field(item, 2, ledColourWord(colours.stops[std::size_t(i)]));
        putMessageField(keyColour, 8, item);
    }
    putVarintField(keyColour, 9, kLedScale);

    Buffer<kLedPayloadCapacity> payload;
    putVarintField(payload, 1, kLedCtrl);
    putMessageField(payload, 5, keyColour);
    return payload;
}

/// Split a payload across as many 64-byte reports as it needs.
///
/// Every frame repeats the header except for its index, which counts from one,
/// and `payloadTotalSize` carries the whole payload in every frame rather than
/// the part that frame holds. The body is a plain byte split at kLedFramePayload
/// with nothing marking the seam.
///
/// All three rules are confirmed: the five, nine and ten stop captures are two
/// reports each and follow every one of them. The receiver's reassembly was
/// never observed, so this is what the sender does rather than what the receiver
/// requires, but the two have to agree.
///
/// What is deliberately not copied is the padding. The captured sender clears its
/// transfer buffer once per packet, not once per frame, so past a final frame's
/// payload sits whatever the frame before it left there — three real bytes then
/// fifty-one stale ones, in the five-stop capture. Zeroes go there instead. The
/// base cannot be reading either, since `payloadTotalSize` is what tells it
/// where the message ends.
constexpr LedPacket composeLedRing(const LedRingColours &colours, quint8 deviceModel, bool bPreciseAngles = false)
{
    LedPacket packet;

    const auto payload = composeRunEffects(colours, bPreciseAngles);
    if (payload.overflow)
        return packet;

    const int size = int(payload.size);
    const int frames = size <= 0 ? 1 : (size + kLedFramePayload - 1) / kLedFramePayload;
    if (frames > kMaxLedFrames)
        return packet;

    for (int f = 0; f < frames; ++f) {
        auto &frame = packet.frames[std::size_t(f)];
        frame[offsetof(LedPacketHeader, fixedHeader)] = kLedReportId;
        frame[offsetof(LedPacketHeader, dataType)] = kLedProtoFormatPb3;
        frame[offsetof(LedPacketHeader, deviceType)] = kLedDeviceTypeBase;
        frame[offsetof(LedPacketHeader, deviceModel)] = deviceModel;
        frame[offsetof(LedPacketHeader, cmdCode)] = kLedCmdRunEffects;
        frame[offsetof(LedPacketHeader, cmdGroup)] = kLedCmdGroupCtrl;
        frame[offsetof(LedPacketHeader, payloadTotalSize)] = quint8((size >> 8) & 0xFF);
        frame[offsetof(LedPacketHeader, payloadTotalSize) + 1] = quint8(size & 0xFF);
        frame[offsetof(LedPacketHeader, totalFrames)] = quint8(frames);
        frame[offsetof(LedPacketHeader, frameIndex)] = quint8(f + 1);

        const int begin = f * kLedFramePayload;
        const int chunk = qMin(kLedFramePayload, size - begin);
        for (int i = 0; i < chunk; ++i)
            frame[std::size_t(int(sizeof(LedPacketHeader)) + i)] = payload.bytes[std::size_t(begin + i)];
    }

    packet.frameCount = frames;
    packet.payloadSize = size;
    packet.ok = true;
    return packet;
}

/// The BaseId byte for a product id.
///
/// Only the value for the plain Alpha Evo is confirmed, from the captures. The
/// other two follow from EVO, EVO2 and EVO3 being numbered consecutively in
/// the same order as their product ids, which is suggestive rather than
/// observed. An unrecognised base falls back to the confirmed value.
constexpr quint8 baseModelForProductId(quint16 productId)
{
    switch (productId) {
    case 0x0500: return 8;
    case 0x0501: return 9;
    case 0x0502: return 10;
    default: return 8;
    }
}

// ---------------------------------------------------------------------------
// Structural checks
// ---------------------------------------------------------------------------

namespace detail {

constexpr LedRingColours stopsOf(int count, LedRgb colour)
{
    LedRingColours ring;
    ring.count = quint8(count);
    for (int i = 0; i < count && i < kMaxLedStops; ++i)
        ring.stops[std::size_t(i)] = colour;
    return ring;
}

inline constexpr LedRgb kAssertRed{0xD3, 0x20, 0x29};

// The step rule, at the counts the captures cover and at the ends of the range.
static_assert(keyIdForStop(0, 1) == 0);
static_assert(keyIdForStop(0, 2) == 0 && keyIdForStop(1, 2) == 400);
static_assert(keyIdForStop(0, 3) == 0 && keyIdForStop(1, 3) == 256
              && keyIdForStop(2, 3) == 512);
static_assert(keyIdForStop(9, 10) == 720);

// Red in the low byte, not the high one.
static_assert(ledColourWord(kAssertRed) == 0x002920D3);

// Where fragmenting starts. Four stops is the last count that fits one report,
// which is the boundary worth pinning; the exact payload sizes come from the
// captures for one to three and from the same encoder for four and five.
static_assert(composeLedRing(stopsOf(1, kAssertRed), 8).payloadSize == 17);
static_assert(composeLedRing(stopsOf(2, kAssertRed), 8).payloadSize == 27);
static_assert(composeLedRing(stopsOf(3, kAssertRed), 8).payloadSize == 37);
static_assert(composeLedRing(stopsOf(4, kAssertRed), 8).frameCount == 1);
static_assert(composeLedRing(stopsOf(5, kAssertRed), 8).frameCount == 2);

// The largest ring there can be. Sixteen stops is 166 bytes across four
// reports, and the .ok is what says the buffers and the frame limit still hold
// at whatever kMaxLedStops has been moved to.
static_assert(composeLedRing(stopsOf(kMaxLedStops, kAssertRed), 8).ok);
static_assert(composeLedRing(stopsOf(kMaxLedStops, kAssertRed), 8).payloadSize == 166);
static_assert(composeLedRing(stopsOf(kMaxLedStops, kAssertRed), 8).frameCount == 4);

/// Every frame of a split packet declares the same total, and the frame indices
/// run 1..n. At the ceiling that is four frames, two more than any capture
/// covers, so this is the only thing checking that the rule holds past the
/// second one.
constexpr bool fragmentHeadersAgree()
{
    const auto packet = composeLedRing(stopsOf(kMaxLedStops, kAssertRed), 8);
    if (!packet.ok || packet.frameCount < 2)
        return false;
    for (int f = 0; f < packet.frameCount; ++f) {
        const auto &frame = packet.frames[std::size_t(f)];
        if (frame[offsetof(LedPacketHeader, totalFrames)] != packet.frameCount)
            return false;
        if (frame[offsetof(LedPacketHeader, frameIndex)] != f + 1)
            return false;
        const int declared = (int(frame[offsetof(LedPacketHeader, payloadTotalSize)]) << 8)
            | int(frame[offsetof(LedPacketHeader, payloadTotalSize) + 1]);
        if (declared != packet.payloadSize)
            return false;
    }
    return true;
}
static_assert(fragmentHeadersAgree());

/// The frames' payload slices, concatenated, are the payload. This is the half
/// of fragmenting we can check without hardware: that nothing is dropped or
/// duplicated at a boundary.
constexpr bool fragmentsReassemble()
{
    const auto ring = stopsOf(kMaxLedStops, kAssertRed);
    const auto payload = composeRunEffects(ring);
    const auto packet = composeLedRing(ring, 8);
    if (!packet.ok || payload.overflow)
        return false;
    for (int i = 0; i < int(payload.size); ++i) {
        const int frame = i / kLedFramePayload;
        const int offset = int(sizeof(LedPacketHeader)) + (i % kLedFramePayload);
        if (packet.frames[std::size_t(frame)][std::size_t(offset)] != payload.bytes[std::size_t(i)])
            return false;
    }
    return true;
}
static_assert(fragmentsReassemble());

} // namespace detail

} // namespace hidcenter::simagic_alpha_evo
