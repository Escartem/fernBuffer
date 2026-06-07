#pragma once
#include "bit.hpp"
#include "coding.hpp"
#include "value.hpp"
#include "packing.hpp"
#include <vector>
#include <string>
#include <cstring>

// Decode values sector

static std::vector<std::string> decode_values_sector(BitReader& r) {
    uint64_t count = ReadFibonacci(r);
    std::vector<std::string> tokens;
    tokens.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t sz   = ReadFibonacci(r);
        int      type = r.read_bits(1);
        auto     raw  = r.read_bytes((size_t)sz);
        if (type == 0) {
            tokens.push_back(std::string(raw.begin(), raw.end()));
        } else {
            tokens.push_back(hex_encode(raw, false)); // lower hex
        }
    }
    return tokens;
}

// Decode keys sector

static std::vector<std::string> decode_key_sector(BitReader& r) {
    uint64_t count = ReadFibonacci(r);
    std::vector<std::string> tokens;
    tokens.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t sz  = ReadFibonacci(r);
        auto     raw = r.read_bytes((size_t)sz);
        tokens.push_back(std::string(raw.begin(), raw.end()));
    }
    return tokens;
}

// Decode schema ops

struct SchemaOp {
    enum class Kind { Pop, Push };
    Kind kind;

    // Push payload
    bool        is_index = false; // Idx
    bool        is_token = false; // int token id
    int64_t     token_id = 0;
    std::string key;              // plain string key
};

static std::vector<SchemaOp> decode_schema(BitReader& r) {
    std::vector<SchemaOp> ops;
    while (true) {
        int change = r.read_bits(1);
        if (change == 0) break;
        int dir = r.read_bits(1);
        if (dir == 0) {
            SchemaOp op; op.kind = SchemaOp::Kind::Pop;
            ops.push_back(op);
        } else {
            SchemaOp op; op.kind = SchemaOp::Kind::Push;
            int t1 = r.read_bits(1);
            if (t1 == 0) {
                int t2 = r.read_bits(1);
                if (t2 == 0) {
                    // Array index (Idx), stored value is always 0, caller assigns actual idx
                    ReadFibonacci(r); // consume dummy 0
                    op.is_index = true;
                } else {
                    // Integer token key
                    op.is_token = true;
                    op.token_id = (int64_t)ReadFibonacci(r);
                }
            } else {
                // String key
                uint64_t sz = ReadFibonacci(r);
                auto raw = r.read_bytes((size_t)sz);
                op.key = std::string(raw.begin(), raw.end());
            }
            ops.push_back(op);
        }
    }
    return ops;
}

// Decode a single field value

static Value decode_field_value(BitReader& r, const std::vector<std::string>& value_tokens) {
    int t = r.read_bits(2);

    if (t == 0) { // Special
        int sub = r.read_bits(2);
        if (sub == 0) return Value::make_null();
        if (sub == 1) return Value::make_object();
        if (sub == 2) return Value::make_array();
        // sub == 3: float
        int is64 = r.read_bits(1);
        if (is64) {
            auto raw = r.read_bytes(8);
            double d; memcpy(&d, raw.data(), 8);
            return Value::make_float(d);
        } else {
            auto raw = r.read_bytes(4);
            float f; memcpy(&f, raw.data(), 4);
            return Value::make_float((double)f);
        }
    }

    if (t == 1) { // String
        uint64_t n_parts = ReadFibonacci(r);
        std::string joined;
        for (uint64_t i = 0; i < n_parts; ++i) {
            int is_str = r.read_bits(1);
            if (is_str == 0) {
                // Raw hex
                int isUpper = r.read_bits(1);
                uint64_t sz = ReadFibonacci(r);
                auto raw = r.read_bytes((size_t)sz);
                std::string hex = hex_encode(raw, isUpper != 0);
                joined += hex;
            } else {
                int is_token = r.read_bits(1);
                if (is_token) {
                    uint64_t tok_id = ReadFibonacci(r);
                    if (tok_id < value_tokens.size())
                        joined += value_tokens[tok_id];
                } else {
                    uint64_t sz = ReadFibonacci(r);
                    auto raw = r.read_bytes((size_t)sz);
                    joined += std::string(raw.begin(), raw.end());
                }
            }
        }
        return Value::make_string(joined);
    }

    if (t == 2) { // Int
        int is_signed = r.read_bits(1);
        int64_t val = (int64_t)ReadFibonacci(r);
        if (is_signed) val = -val;
        return Value::make_int(val);
    }

    if (t == 3) { // Bool run
        uint64_t n = ReadFibonacci(r);
        std::vector<bool> bools;
        for (uint64_t i = 0; i < n; ++i) bools.push_back(r.read_bits(1) != 0);
        if (bools.size() == 1) return Value::make_bool(bools[0]);
        return Value::make_bool(bools[0]);
    }

    throw std::runtime_error("Unknown field type");
}

// Top-level decode

inline Value fb_decode(const uint8_t* data, size_t len) {
    BitReader r(data, len);

    uint64_t version = ReadEliasGamma(r);
    (void)version; // forward compatible

    // Values sector
    std::vector<std::string> value_tokens;
    if (r.read_bits(1)) value_tokens = decode_values_sector(r);

    // Keys sector
    std::vector<std::string> key_tokens;
    if (r.read_bits(1)) key_tokens = decode_key_sector(r);

    uint64_t n_fields = ReadLEB128(r);

    std::vector<Path>  all_paths;
    std::vector<Value> all_values;
    all_paths.reserve(n_fields);
    all_values.reserve(n_fields);

    Path current_path;
    std::unordered_map<size_t, int64_t> array_counters; // path-hash -> next idx

    auto path_hash = [](const Path& p) -> size_t {
        size_t h = 0;
        for (auto& e : p) {
            h ^= std::hash<std::string>()(e.is_index ? std::to_string(e.idx) : e.key)
               + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    };

    for (uint64_t fi = 0; fi < n_fields; ++fi) {
        auto ops = decode_schema(r);
        Value val = decode_field_value(r, value_tokens);

        for (auto& op : ops) {
            if (op.kind == SchemaOp::Kind::Pop) {
                current_path.pop_back();
            } else {
                PathElem elem;
                if (op.is_index) {
                    // Assign next array index for this parent path
                    size_t ph = path_hash(current_path);
                    auto it = array_counters.find(ph);
                    int64_t idx = 0;
                    if (it != array_counters.end()) idx = it->second;
                    array_counters[ph] = idx + 1;
                    elem = PathElem::from_idx(idx);
                } else if (op.is_token) {
                    // Token key
                    std::string key = (op.token_id < (int64_t)key_tokens.size())
                                    ? key_tokens[op.token_id]
                                    : std::to_string(op.token_id);
                    elem = PathElem::from_key(key);
                } else {
                    elem = PathElem::from_key(op.key);
                }
                current_path.push_back(elem);
            }
        }

        all_paths.push_back(current_path);
        all_values.push_back(val);
    }

    return unflatten(all_paths, all_values, key_tokens);
}
