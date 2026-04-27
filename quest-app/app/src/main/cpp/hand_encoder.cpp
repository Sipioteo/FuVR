// SPDX-License-Identifier: Apache-2.0

#include "hand_encoder.hpp"

#include <cstring>

namespace fuvr {

namespace {

constexpr const char kPrefix[] = "q-hand: ";

constexpr const char kB64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64encode(const uint8_t* data, size_t n) {
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= n) {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i+1] << 8 | (uint32_t)data[i+2];
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 6)  & 0x3F]);
        out.push_back(kB64Alphabet[v & 0x3F]);
        i += 3;
    }
    if (i < n) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < n) v |= (uint32_t)data[i+1] << 8;
        out.push_back(kB64Alphabet[(v >> 18) & 0x3F]);
        out.push_back(kB64Alphabet[(v >> 12) & 0x3F]);
        if (i + 1 < n) {
            out.push_back(kB64Alphabet[(v >> 6) & 0x3F]);
        } else {
            out.push_back('=');
        }
        out.push_back('=');
    }
    return out;
}

int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool b64decode(const std::string& in, std::vector<uint8_t>& out) {
    out.clear();
    if (in.size() % 4 != 0) return false;
    out.reserve((in.size() / 4) * 3);
    for (size_t i = 0; i < in.size(); i += 4) {
        int a = b64val(in[i]);
        int b = b64val(in[i+1]);
        if (a < 0 || b < 0) return false;
        int c = (in[i+2] == '=') ? -2 : b64val(in[i+2]);
        int d = (in[i+3] == '=') ? -2 : b64val(in[i+3]);
        if (c == -1 || d == -1) return false;
        uint32_t v = ((uint32_t)a << 18) | ((uint32_t)b << 12);
        out.push_back((uint8_t)((v >> 16) & 0xFF));
        if (c >= 0) {
            v |= ((uint32_t)c << 6);
            out.push_back((uint8_t)((v >> 8) & 0xFF));
            if (d >= 0) {
                v |= (uint32_t)d;
                out.push_back((uint8_t)(v & 0xFF));
            }
        }
    }
    return true;
}

}  // namespace

uint16_t HandEncoder::f32_to_f16(float v) {
    uint32_t x;
    std::memcpy(&x, &v, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (((x >> 23) & 0xFF) == 0xFF) {
        // inf or NaN: clamp to inf with preserved sign.
        return (uint16_t)(sign | 0x7C00 | (mant ? 0x0200 : 0));
    }
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mant = (mant | 0x800000) >> (1 - exp);
        if (mant & 0x1000) mant += 0x2000;
        return (uint16_t)(sign | (mant >> 13));
    }
    if (mant & 0x1000) {
        mant += 0x2000;
        if (mant & 0x800000) { mant = 0; exp += 1; }
        if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    }
    return (uint16_t)(sign | (uint32_t)(exp << 10) | (mant >> 13));
}

float HandEncoder::f16_to_f32(uint16_t h) {
    const uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x3FF;
    uint32_t out;
    if (exp == 0) {
        if (mant == 0) {
            out = sign;
        } else {
            // subnormal
            while (!(mant & 0x400)) { mant <<= 1; exp -= 1; }
            mant &= 0x3FF;
            exp += 1;
            out = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out = sign | 0x7F800000 | (mant << 13);
    } else {
        out = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &out, sizeof(f));
    return f;
}

std::string HandEncoder::encode(const HandJointSet& joints) {
    std::vector<uint8_t> bytes(kHandFloatsTotal * 2);
    for (size_t i = 0; i < kHandFloatsTotal; ++i) {
        const uint16_t h = f32_to_f16(joints.floats[i]);
        bytes[i * 2 + 0] = (uint8_t)(h & 0xFF);
        bytes[i * 2 + 1] = (uint8_t)((h >> 8) & 0xFF);
    }
    return std::string(kPrefix) + b64encode(bytes.data(), bytes.size());
}

bool HandEncoder::decode(const std::string& wire, HandJointSet& out) {
    const size_t pl = sizeof(kPrefix) - 1;
    if (wire.size() <= pl) return false;
    if (wire.compare(0, pl, kPrefix) != 0) return false;
    std::vector<uint8_t> bytes;
    if (!b64decode(wire.substr(pl), bytes)) return false;
    if (bytes.size() != kHandFloatsTotal * 2) return false;
    for (size_t i = 0; i < kHandFloatsTotal; ++i) {
        uint16_t h = (uint16_t)bytes[i*2] | ((uint16_t)bytes[i*2 + 1] << 8);
        out.floats[i] = f16_to_f32(h);
    }
    return true;
}

}
