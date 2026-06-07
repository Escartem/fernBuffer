#pragma once
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <stdexcept>
#include <cctype>
#include <cstdint>
#include <regex>

// Value

enum class ValueKind { Null, Bool, Int, Float, String, Array, Object };

struct Value {
    ValueKind kind = ValueKind::Null;

    bool                     b   = false;
    int64_t                  i   = 0;
    double                   d   = 0.0;
    std::string              s;
    std::vector<Value>       arr;

    // For objects: parallel key/value vectors (preserves insertion order)
    std::vector<std::string> obj_keys;
    std::vector<Value>       obj_vals;

    // Constructors
    static Value make_null()               { Value v; v.kind = ValueKind::Null;   return v; }
    static Value make_bool(bool x)         { Value v; v.kind = ValueKind::Bool;   v.b = x; return v; }
    static Value make_int(int64_t x)       { Value v; v.kind = ValueKind::Int;    v.i = x; return v; }
    static Value make_float(double x)      { Value v; v.kind = ValueKind::Float;  v.d = x; return v; }
    static Value make_string(std::string x){ Value v; v.kind = ValueKind::String; v.s = std::move(x); return v; }
    static Value make_array()              { Value v; v.kind = ValueKind::Array;  return v; }
    static Value make_object()             { Value v; v.kind = ValueKind::Object; return v; }

    bool is_null()   const { return kind == ValueKind::Null;   }
    bool is_bool()   const { return kind == ValueKind::Bool;   }
    bool is_int()    const { return kind == ValueKind::Int;    }
    bool is_float()  const { return kind == ValueKind::Float;  }
    bool is_string() const { return kind == ValueKind::String; }
    bool is_array()  const { return kind == ValueKind::Array;  }
    bool is_object() const { return kind == ValueKind::Object; }

    // Object access helpers
    const Value* get(const std::string& key) const {
        for (size_t i = 0; i < obj_keys.size(); ++i)
            if (obj_keys[i] == key) return &obj_vals[i];
        return nullptr;
    }
    void set(std::string key, Value val) {
        obj_keys.push_back(std::move(key));
        obj_vals.push_back(std::move(val));
    }
};

// Hex helpers

inline bool is_hex_char(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline bool is_pure_hex(const std::string& s) {
    if (s.size() < 2 || s.size() % 2 != 0) return false;
    for (char c : s) if (!is_hex_char(c)) return false;
    return true;
}

inline bool str_isupper(const std::string& s) {
    for (char c : s)
        if (c >= 'a' && c <= 'f') return false;
    return true;
}

inline std::string hex_encode(const std::vector<uint8_t>& data, bool upper) {
    static const char* lo = "0123456789abcdef";
    static const char* up = "0123456789ABCDEF";
    const char* t = upper ? up : lo;
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) { out += t[b >> 4]; out += t[b & 0xF]; }
    return out;
}

inline std::vector<uint8_t> hex_decode(const std::string& s) {
    std::vector<uint8_t> out;
    out.reserve(s.size() / 2);
    auto nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    };
    for (size_t i = 0; i + 1 < s.size(); i += 2)
        out.push_back((nibble(s[i]) << 4) | nibble(s[i+1]));
    return out;
}

// String segment (for encoder)

enum class SegType { String, Hex };

struct Segment {
    SegType     type;
    std::string value;   // raw text or hex string
    bool        isUpper; // only for Hex
};

// Split a string into alternating plain/hex segments (>8 even hex chars)
inline std::vector<Segment> split_hex_segments(const std::string& s) {
    std::vector<Segment> out;
    size_t n = s.size();
    size_t last = 0;

    size_t i = 0;
    while (i < n) {
        if (is_hex_char(s[i])) {
            if (i > 0 && is_hex_char(s[i-1])) { ++i; continue; }
            size_t j = i;
            while (j < n && is_hex_char(s[j])) ++j;
            size_t run = j - i;
            bool valid = (j >= n || !is_hex_char(s[j]));
            if (valid && run >= 8 && run % 2 == 0) {
                // Flush preceding text
                if (i > last) {
                    out.push_back({SegType::String, s.substr(last, i - last), false});
                }
                std::string hex_part = s.substr(i, run);
                out.push_back({SegType::Hex, hex_part, str_isupper(hex_part)});
                last = j;
                i = j;
                continue;
            }
        }
        ++i;
    }
    if (last < n)
        out.push_back({SegType::String, s.substr(last), false});
    if (out.empty())
        out.push_back({SegType::String, s, false});
    return out;
}
