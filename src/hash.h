#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace mcd {

// 128 位（双 64 位通道）非加密哈希。
// 设计目标：单次遍历、无分支预测失败、对短字符串与 UTF-8 文本都有良好雪崩性。
// 使用 64x64->128 位乘法（MUM）混合，碰撞概率约为 2^-128 量级。
struct Hash128 {
    uint64_t lo = 0;
    uint64_t hi = 0;

    bool operator==(const Hash128& other) const noexcept {
        return lo == other.lo && hi == other.hi;
    }
    bool operator!=(const Hash128& other) const noexcept { return !(*this == other); }
};

inline uint64_t HashMum(uint64_t a, uint64_t b, uint64_t& high) noexcept {
    const unsigned __int128 product = static_cast<unsigned __int128>(a) * b;
    high = static_cast<uint64_t>(product >> 64);
    return static_cast<uint64_t>(product);
}

inline uint64_t HashLoad64(const uint8_t* p) noexcept {
    uint64_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

inline Hash128 HashBytes(std::string_view data) noexcept {
    constexpr uint64_t k1 = 0xA0761D6478BD642Full;
    constexpr uint64_t k2 = 0xE7037ED1A0B428DBull;
    constexpr uint64_t k3 = 0x8EBC6AF09C88C6E3ull;
    constexpr uint64_t k4 = 0x589965CC75374CC3ull;

    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    const size_t n = data.size();

    uint64_t h1 = k1 ^ (static_cast<uint64_t>(n) * k3);
    uint64_t h2 = k2 ^ (static_cast<uint64_t>(n) << 32);
    uint64_t high = 0;

    size_t i = 0;
    while (i + 32 <= n) {
        const uint64_t v0 = HashLoad64(p + i);
        const uint64_t v1 = HashLoad64(p + i + 8);
        const uint64_t v2 = HashLoad64(p + i + 16);
        const uint64_t v3 = HashLoad64(p + i + 24);
        h1 = HashMum(h1 ^ v0, v1 ^ k1, high) ^ high;
        h2 = HashMum(h2 ^ v2, v3 ^ k2, high) ^ high;
        i += 32;
    }
    while (i + 8 <= n) {
        const uint64_t v = HashLoad64(p + i);
        h1 = HashMum(h1 ^ v, k2, high) ^ high;
        h2 = HashMum(h2 ^ (v + static_cast<uint64_t>(i)), k4, high) ^ high;
        i += 8;
    }
    if (i < n) {
        const size_t remaining = n - i;
        uint64_t tail = 0;
        std::memcpy(&tail, p + i, remaining);
        tail ^= static_cast<uint64_t>(remaining) << 56;
        h1 = HashMum(h1 ^ tail, k1, high) ^ high;
        h2 = HashMum(h2 ^ (tail * k3), k4, high) ^ high;
    }

    // 最终混合：把两条通道互相折叠，再做一次雪崩。
    h1 = HashMum(h1, h2 ^ k1, high) ^ high;
    h2 = HashMum(h2 ^ k2, h1, high) ^ high;
    h1 ^= h1 >> 33;
    h1 *= 0xFF51AFD7ED558CCDull;
    h1 ^= h1 >> 33;
    h2 ^= h2 >> 29;
    h2 *= 0xC4CEB9FE1A85EC53ull;
    h2 ^= h2 >> 32;
    return Hash128{h1, h2};
}

// 供 unordered_map 使用的键哈希（取 128 位中的低位通道）。
struct KeyHash {
    size_t operator()(std::string_view key) const noexcept {
        return static_cast<size_t>(HashBytes(key).lo);
    }
};

}  // namespace mcd
