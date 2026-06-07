#define FERNBUFFER_BUILD_DLL
#include "fernbuffer.h"

#include "src/bit.hpp"
#include "src/coding.hpp"
#include "src/value.hpp"
#include "src/packing.hpp"
#include "src/json.hpp"
#include "src/encoder.hpp"
#include "src/decoder.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

using ojson = nlohmann::ordered_json;

static Value from_json(const ojson& j) {
    switch (j.type()) {
        case ojson::value_t::null:            return Value::make_null();
        case ojson::value_t::boolean:         return Value::make_bool(j.get<bool>());
        case ojson::value_t::number_integer:  return Value::make_int(j.get<int64_t>());
        case ojson::value_t::number_unsigned: return Value::make_int(static_cast<int64_t>(j.get<uint64_t>()));
        case ojson::value_t::number_float:    return Value::make_float(j.get<double>());
        case ojson::value_t::string:          return Value::make_string(j.get<std::string>());
        case ojson::value_t::array: {
            Value v = Value::make_array();
            for (const auto& e : j) v.arr.push_back(from_json(e));
            return v;
        }
        case ojson::value_t::object: {
            Value v = Value::make_object();
            for (const auto& [k, val] : j.items()) v.set(k, from_json(val));
            return v;
        }
        default: return Value::make_null();
    }
}

static ojson to_json(const Value& v) {
    switch (v.kind) {
        case ValueKind::Null:   return nullptr;
        case ValueKind::Bool:   return v.b;
        case ValueKind::Int:    return v.i;
        case ValueKind::Float:  return v.d;
        case ValueKind::String: return v.s;
        case ValueKind::Array: {
            ojson arr = ojson::array();
            for (const auto& e : v.arr) arr.push_back(to_json(e));
            return arr;
        }
        case ValueKind::Object: {
            ojson obj = ojson::object();
            for (size_t i = 0; i < v.obj_keys.size(); ++i)
                obj[v.obj_keys[i]] = to_json(v.obj_vals[i]);
            return obj;
        }
    }
    return nullptr;
}

extern "C" {

FERNBUFFER_API uint8_t* fernbuffer_encode(const char* json_in, size_t json_len, size_t* out_len) {
    if (!json_in || !out_len) return nullptr;
    if (json_len == 0) json_len = strlen(json_in);
    try {
        Value root = from_json(ojson::parse(json_in, json_in + json_len));
        std::vector<uint8_t> buf = fb_encode(root);
        uint8_t* out = (uint8_t*)malloc(buf.size());
        if (!out) return nullptr;
        memcpy(out, buf.data(), buf.size());
        *out_len = buf.size();
        return out;
    } catch (...) {
        return nullptr;
    }
}

FERNBUFFER_API char* fernbuffer_decode(const uint8_t* data, size_t data_len) {
    if (!data || data_len == 0) return nullptr;
    try {
        Value root = fb_decode(data, data_len);
        std::string json = to_json(root).dump();
        char* out = (char*)malloc(json.size() + 1);
        if (!out) return nullptr;
        memcpy(out, json.data(), json.size());
        out[json.size()] = '\0';
        return out;
    } catch (...) {
        return nullptr;
    }
}

FERNBUFFER_API void fernbuffer_free(void* ptr) {
    free(ptr);
}

} // extern "C"
