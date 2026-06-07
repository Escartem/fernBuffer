#pragma once
#include "bit.hpp"
#include <vector>
#include <cstdint>

// Fibonacci table

static std::vector<uint64_t> g_fibs = {1, 2};

static uint64_t fib(int n) {
    while (static_cast<int>(g_fibs.size()) <= n)
        g_fibs.push_back(g_fibs[g_fibs.size()-1] + g_fibs[g_fibs.size()-2]);
    return g_fibs[n];
}

// Fibonacci (Zeckendorf) coding

inline void WriteFibonacci(BitWriter& w, uint64_t val) {
    val += 1;
    int highest = 0;
    while (fib(highest + 1) <= val) ++highest;

    // collect bits
    std::vector<int> bits;
    bits.reserve(highest + 1);
    for (int i = highest; i >= 0; --i) {
        if (fib(i) <= val) { bits.push_back(1); val -= fib(i); }
        else               { bits.push_back(0); }
    }
    // write LSB-first
    for (int i = static_cast<int>(bits.size()) - 1; i >= 0; --i)
        w.write_bits(bits[i], 1);
    w.write_bits(1, 1); // terminator
}

inline uint64_t ReadFibonacci(BitReader& r) {
    uint64_t val = 0;
    int prev = 0, i = 0;
    while (true) {
        int bit = r.read_bits(1);
        if (bit == 1 && prev == 1) break;
        if (bit == 1) val += fib(i);
        prev = bit;
        ++i;
    }
    return val - 1;
}

// Elias-Gamma

inline void WriteEliasGamma(BitWriter& w, uint64_t val) {
    val += 1;
    int bits = 0;
    uint64_t tmp = val;
    while (tmp >>= 1) ++bits;
    for (int i = 0; i < bits; ++i) w.write_bits(0, 1);
    for (int i = bits; i >= 0; --i) w.write_bits((val >> i) & 1, 1);
}

inline uint64_t ReadEliasGamma(BitReader& r) {
    int bits = 1;
    while (r.read_bits(1) == 0) ++bits;
    uint64_t val = (1ull << (bits - 1));
    for (int i = bits - 2; i >= 0; --i) val |= (uint64_t)r.read_bits(1) << i;
    return val - 1;
}

// LEB128 (bit-stream variant, 7 data bits + 1 continuation)

inline void WriteLEB128(BitWriter& w, uint64_t val) {
    while (val >= 0x7F) {
        w.write_bits(0, 1);          // continuation
        w.write_bits(val & 0x7F, 7);
        val >>= 7;
    }
    w.write_bits(1, 1);              // stop
    w.write_bits(val & 0x7F, 7);
}

inline uint64_t ReadLEB128(BitReader& r) {
    uint64_t val = 0;
    int shift = 0;
    while (true) {
        bool has_more = r.read_bits(1) == 0;
        uint64_t chunk = r.read_bits(7);
        val |= chunk << shift;
        shift += 7;
        if (!has_more) break;
    }
    return val;
}
