#pragma once
#include "bit.hpp"
#include "coding.hpp"
#include "value.hpp"
#include "packing.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <cstring>
#include <cmath>

// Payload field

enum class FieldType { Special = 0, String = 1, Int = 2, BoolRun = 3 };

struct StrPart {
    enum class Kind { Raw, Token, String };
    Kind        kind    = Kind::String;
    std::string value;
    bool        isUpper = false;
    int         tokenId = 0;
};

struct PayloadField {
    FieldType            type       = FieldType::Special;
    Value                special_val;
    std::vector<StrPart> parts;
    int64_t              int_val    = 0;
    std::vector<bool>    bools;
};

// Value -> PayloadField

static PayloadField value_to_field(const Value& v) {
    PayloadField f;
    if (v.is_null() || (v.is_array() && v.arr.empty()) ||
        (v.is_object() && v.obj_keys.empty()) || v.is_float()) {
        f.type = FieldType::Special; f.special_val = v; return f;
    }
    if (v.is_bool())   { f.type = FieldType::BoolRun; f.bools = {v.b}; return f; }
    if (v.is_int())    { f.type = FieldType::Int; f.int_val = v.i; return f; }
    if (v.is_string()) {
        f.type = FieldType::String;
        const std::string& s = v.s;
        if (is_pure_hex(s)) {
            f.parts.push_back({StrPart::Kind::Raw, s, str_isupper(s), 0});
        } else {
            for (auto& seg : split_hex_segments(s)) {
                if (seg.type == SegType::Hex)
                    f.parts.push_back({StrPart::Kind::Raw, seg.value, seg.isUpper, 0});
                else
                    f.parts.push_back({StrPart::Kind::String, seg.value, false, 0});
            }
        }
        return f;
    }
    f.type = FieldType::Special; f.special_val = Value::make_null(); return f;
}

// Values dedup sector

static bool write_values_sector(BitWriter& w, std::vector<PayloadField>& fields) {
    // Count
    std::unordered_map<std::string, int> counts;
    for (auto& f : fields) {
        if (f.type != FieldType::String) continue;
        for (auto& p : f.parts) if (!p.value.empty()) counts[p.value]++;
    }
    // Build token list
    std::unordered_map<std::string, int> seen;
    std::vector<std::string> tokens;
    std::vector<bool>        token_is_hex;
    for (auto& f : fields) {
        if (f.type != FieldType::String) continue;
        for (auto& p : f.parts) {
            if (p.value.empty()) continue;
            if (counts[p.value] >= 2 && seen.find(p.value) == seen.end()) {
                seen[p.value] = (int)tokens.size();
                tokens.push_back(p.value);
                token_is_hex.push_back(p.kind == StrPart::Kind::Raw);
            }
        }
    }
    // Replace parts
    for (auto& f : fields) {
        if (f.type != FieldType::String) continue;
        for (auto& p : f.parts) {
            auto it = seen.find(p.value);
            if (it != seen.end()) {
                p.kind = StrPart::Kind::Token;
                p.tokenId = it->second;
                // value no longer needed but keep for debugging
            }
        }
    }
    if (tokens.empty()) return false;

    w.write_bits(1, 1); // sector present
    WriteFibonacci(w, (uint64_t)tokens.size());
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (!token_is_hex[i]) {
            auto raw = reinterpret_cast<const uint8_t*>(tokens[i].data());
            WriteFibonacci(w, tokens[i].size());
            w.write_bits(0, 1); // string
            w.write_bytes(raw, tokens[i].size());
        } else {
            auto data = hex_decode(tokens[i]);
            WriteFibonacci(w, data.size());
            w.write_bits(1, 1); // hex
            w.write_bytes(data.data(), data.size());
        }
    }
    return true;
}

// Keys dedup sector

static bool write_keys_sector(BitWriter& w, std::vector<Path>& paths) {
    std::unordered_map<std::string, int> counts;
    for (auto& path : paths)
        for (auto& e : path)
            if (!e.is_index) counts[e.key]++;

    std::unordered_map<std::string, int> seen;
    std::vector<std::string> tokens;
    for (auto& path : paths)
        for (auto& e : path)
            if (!e.is_index && counts[e.key] >= 2 && seen.find(e.key) == seen.end()) {
                seen[e.key] = (int)tokens.size();
                tokens.push_back(e.key);
            }

    // Replace repeated keys with token references
    for (auto& path : paths)
        for (auto& e : path)
            if (!e.is_index) {
                auto it = seen.find(e.key);
                if (it != seen.end()) {
                    e.is_token = true;
                    e.token_id = it->second;
                }
            }

    if (tokens.empty()) return false;

    w.write_bits(1, 1); // sector present
    WriteFibonacci(w, (uint64_t)tokens.size());
    for (auto& tok : tokens) {
        WriteFibonacci(w, tok.size());
        w.write_bytes(reinterpret_cast<const uint8_t*>(tok.data()), tok.size());
    }
    return true;
}

// Schema encoder

static void encode_schema(BitWriter& w, const Path& cur, const Path& prev) {
    size_t n = 0;
    while (n < cur.size() && n < prev.size()) {
        const auto& a = cur[n]; const auto& b = prev[n];
        if (a.is_index != b.is_index) break;
        if (a.is_index && a.idx != b.idx) break;
        if (!a.is_index) {
            if (a.is_token != b.is_token) break;
            if (a.is_token ? a.token_id != b.token_id : a.key != b.key) break;
        }
        ++n;
    }
    for (size_t i = n; i < prev.size(); ++i) { w.write_bits(1,1); w.write_bits(0,1); }
    for (size_t i = n; i < cur.size(); ++i) {
        w.write_bits(1,1); w.write_bits(1,1);
        const auto& e = cur[i];
        if (e.is_index) {
            w.write_bits(0,1); w.write_bits(0,1); WriteFibonacci(w, 0);
        } else if (e.is_token) {
            w.write_bits(0,1); w.write_bits(1,1); WriteFibonacci(w, (uint64_t)e.token_id);
        } else {
            w.write_bits(1,1);
            WriteFibonacci(w, e.key.size());
            w.write_bytes(reinterpret_cast<const uint8_t*>(e.key.data()), e.key.size());
        }
    }
    w.write_bits(0,1); // terminate
}

// Field value encoder

static void encode_field_value(BitWriter& w, const PayloadField& f) {
    w.write_bits((int)f.type, 2);
    switch (f.type) {
    case FieldType::Special: {
        const Value& v = f.special_val;
        if (v.is_null())                              { w.write_bits(0,2); }
        else if (v.is_object() && v.obj_keys.empty()) { w.write_bits(1,2); }
        else if (v.is_array()  && v.arr.empty())      { w.write_bits(2,2); }
        else if (v.is_float()) {
            w.write_bits(3,2);
            double dv = v.d; float fv = (float)dv;
            bool is64 = (dv != (double)fv);
            w.write_bits(is64?1:0, 1);
            uint8_t buf[8]; int nbytes = is64 ? 8 : 4;
            if (is64) memcpy(buf,&dv,8); else memcpy(buf,&fv,4);
            for (int i=0;i<nbytes;++i) w.write_bits(buf[i],8);
        }
        break;
    }
    case FieldType::String: {
        WriteFibonacci(w, (uint64_t)f.parts.size());
        for (const auto& p : f.parts) {
            if (p.kind == StrPart::Kind::Raw) {
                auto data = hex_decode(p.value);
                w.write_bits(0,1);
                w.write_bits(p.isUpper?1:0,1);
                WriteFibonacci(w, data.size());
                w.write_bytes(data.data(), data.size());
            } else if (p.kind == StrPart::Kind::Token) {
                w.write_bits(1,1); w.write_bits(1,1);
                WriteFibonacci(w, (uint64_t)p.tokenId);
            } else {
                w.write_bits(1,1); w.write_bits(0,1);
                WriteFibonacci(w, p.value.size());
                w.write_bytes(reinterpret_cast<const uint8_t*>(p.value.data()), p.value.size());
            }
        }
        break;
    }
    case FieldType::Int: {
        int64_t val = f.int_val; bool neg = val < 0;
        w.write_bits(neg?1:0,1); if (neg) val=-val;
        WriteFibonacci(w, (uint64_t)val);
        break;
    }
    case FieldType::BoolRun: {
        WriteFibonacci(w, (uint64_t)f.bools.size());
        for (bool b : f.bools) w.write_bits(b?1:0,1);
        break;
    }
    }
}

// Top-level encode

inline std::vector<uint8_t> fb_encode(const Value& root) {
    static const int VERSION = 1;

    auto flat = flatten(root);

    std::vector<PayloadField> fields;
    fields.reserve(flat.size());
    for (auto& e : flat) fields.push_back(value_to_field(e.value));

    std::vector<Path> paths;
    paths.reserve(flat.size());
    for (auto& e : flat) paths.push_back(e.path);

    BitWriter w;
    WriteEliasGamma(w, VERSION);

    if (!write_values_sector(w, fields)) w.write_bits(0, 1);

    if (!write_keys_sector(w, paths)) w.write_bits(0, 1);

    WriteLEB128(w, (uint64_t)fields.size());

    Path prev_path;
    for (size_t i = 0; i < fields.size(); ++i) {
        encode_schema(w, paths[i], prev_path);
        encode_field_value(w, fields[i]);
        prev_path = paths[i];
    }

    return w.finish();
}
