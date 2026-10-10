// SPDX-FileCopyrightText: 2026 Micael Dias
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <array>
#include <cstddef>

/// The four pieces of protobuf 3 encoding this module needs, and nothing else.
///
/// The wheel's LED command is a protobuf message, but pulling in libprotobuf for
/// three nested messages of scalars would be the tail wagging the dog: it is a
/// large dependency, it generates code at build time, and the plugin otherwise
/// links only Qt. What it actually takes is varints, tags, fixed32 and
/// length-delimited fields, which is this file.
///
/// Everything is constexpr on purpose. That is what lets the composer in
/// AlphaEvoLedProtocol.h be checked against real captured packets at compile
/// time, with no test framework and no chance of the check drifting out of date.
namespace hidcenter::simagic_alpha_evo::proto3 {

/// Protobuf wire types. Only the three that appear in the LED message exist here.
enum class WireType : quint32 {
    Varint = 0,          ///< int32, int64, uint32, uint64, bool, enum
    LengthDelimited = 2, ///< string, bytes, embedded messages
    Fixed32 = 5,         ///< fixed32, sfixed32, float
};

/// A fixed-capacity output buffer.
///
/// Overflow sets a flag instead of writing past the end. That is not defensive
/// habit: indexing a std::array out of range is undefined behaviour, and
/// undefined behaviour inside a constant expression is a hard compile error, so
/// a version that could overrun could not be used in a static_assert at all. The
/// same check makes it safe at runtime, where the input is a user's colour list
/// rather than a literal.
///
/// Callers check `overflow` once at the end rather than after each field. A
/// buffer that has overflowed stays overflowed and swallows everything after,
/// so one test at the end cannot miss a failure in the middle.
template <std::size_t N>
struct Buffer
{
    std::array<quint8, N> bytes{};
    std::size_t size = 0;
    bool overflow = false;

    constexpr void put(quint8 byte)
    {
        if (size >= N) {
            overflow = true;
            return;
        }
        bytes[size++] = byte;
    }

    constexpr void putBytes(const quint8 *data, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
            put(data[i]);
    }

    /// Append another buffer, propagating its overflow. Used to splice a
    /// submessage in once its length is known.
    template <std::size_t M>
    constexpr void append(const Buffer<M> &other)
    {
        if (other.overflow)
            overflow = true;
        putBytes(other.bytes.data(), other.size);
    }
};

/// Base-128 varint, low group first, continuation bit set on all but the last.
constexpr void putVarint(auto &buffer, quint64 value)
{
    while (value >= 0x80) {
        buffer.put(quint8((value & 0x7F) | 0x80));
        value >>= 7;
    }
    buffer.put(quint8(value));
}

/// How many bytes putVarint() would emit. Needed to size scratch buffers.
constexpr std::size_t varintSize(quint64 value)
{
    std::size_t n = 1;
    while (value >= 0x80) {
        value >>= 7;
        ++n;
    }
    return n;
}

/// A field's key: its number and wire type packed into one varint.
constexpr void putTag(auto &buffer, quint32 fieldNumber, WireType type)
{
    putVarint(buffer, (quint64(fieldNumber) << 3) | quint64(type));
}

/// A varint field, omitted when zero.
///
/// Omission is part of proto3, not an optimisation: a field at its default is
/// simply absent from the encoding, and a decoder cannot tell the difference. It
/// matters here because the reference captures rely on it — the first colour
/// stop sits at position zero and so carries no position field at all, and
/// emitting one would produce bytes no capture ever showed.
constexpr void putVarintField(auto &buffer, quint32 fieldNumber, quint64 value)
{
    if (value == 0)
        return;
    putTag(buffer, fieldNumber, WireType::Varint);
    putVarint(buffer, value);
}

/// A fixed32 field, little-endian, omitted when zero.
constexpr void putFixed32Field(auto &buffer, quint32 fieldNumber, quint32 value)
{
    if (value == 0)
        return;
    putTag(buffer, fieldNumber, WireType::Fixed32);
    buffer.put(quint8(value & 0xFF));
    buffer.put(quint8((value >> 8) & 0xFF));
    buffer.put(quint8((value >> 16) & 0xFF));
    buffer.put(quint8((value >> 24) & 0xFF));
}

/// An embedded message: tag, then length, then the bytes.
///
/// The submessage is composed in full before this is called, because the length
/// prefix is a varint and its own width depends on the value. Reserving one byte
/// and patching it afterwards would work for everything this module sends today
/// and break silently the first time a message reached 128 bytes.
///
/// A length-delimited field is *not* omitted when empty. An empty submessage is
/// a present field with zero length, which is a different thing from an absent
/// one, and the repeated colour entries below rely on each one appearing.
template <std::size_t M>
constexpr void putMessageField(auto &buffer, quint32 fieldNumber, const Buffer<M> &message)
{
    if (message.overflow)
        buffer.overflow = true;
    putTag(buffer, fieldNumber, WireType::LengthDelimited);
    putVarint(buffer, message.size);
    buffer.append(message);
}

namespace detail {

/// Encode one varint on its own, so the cases below can be written as literals.
template <std::size_t N>
constexpr Buffer<N> varintOnly(quint64 value)
{
    Buffer<N> b;
    putVarint(b, value);
    return b;
}

constexpr bool varintIs(quint64 value, std::initializer_list<quint8> expected)
{
    const auto encoded = varintOnly<10>(value);
    if (encoded.size != expected.size() || encoded.overflow)
        return false;
    std::size_t i = 0;
    for (const quint8 byte : expected) {
        if (encoded.bytes[i++] != byte)
            return false;
    }
    return true;
}

// The varint boundaries, which is where an encoder of this kind goes wrong.
static_assert(varintIs(0, {0x00}));
static_assert(varintIs(1, {0x01}));
static_assert(varintIs(127, {0x7F}));
static_assert(varintIs(128, {0x80, 0x01}));
static_assert(varintIs(300, {0xAC, 0x02}));
static_assert(varintIs(16383, {0xFF, 0x7F}));
static_assert(varintIs(16384, {0x80, 0x80, 0x01}));

// The two values the LED packet actually carries, from the captures.
static_assert(varintIs(256, {0x80, 0x02}));
static_assert(varintIs(400, {0x90, 0x03}));

static_assert(varintSize(0) == 1);
static_assert(varintSize(127) == 1);
static_assert(varintSize(128) == 2);
static_assert(varintSize(16384) == 3);

/// A zero varint field writes nothing; a non-zero one writes tag then value.
constexpr bool omitsDefaults()
{
    Buffer<8> zero;
    putVarintField(zero, 1, 0);

    Buffer<8> one;
    putVarintField(one, 1, 1);

    return zero.size == 0 && one.size == 2 && one.bytes[0] == 0x08 && one.bytes[1] == 0x01;
}
static_assert(omitsDefaults());

/// Overflow is reported, not written, and is sticky once set.
constexpr bool overflowIsSafe()
{
    Buffer<2> b;
    putVarint(b, 300); // fits exactly
    const bool fitted = !b.overflow && b.size == 2;
    b.put(0xFF); // one too many
    return fitted && b.overflow && b.size == 2;
}
static_assert(overflowIsSafe());

} // namespace detail

} // namespace hidcenter::simagic_alpha_evo::proto3
