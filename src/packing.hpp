#pragma once
#include "value.hpp"
#include <vector>
#include <string>
#include <variant>
#include <cassert>

// Path element

struct PathElem {
    bool        is_index;
    bool        is_token = false; // encoder-internal: key replaced by token reference
    int         token_id = 0;
    std::string key;
    int64_t     idx;

    static PathElem from_key(std::string k)  { PathElem e; e.is_index = false; e.key = std::move(k); e.idx = 0; return e; }
    static PathElem from_idx(int64_t i)      { PathElem e; e.is_index = true;  e.idx = i;            return e; }

    bool operator==(const PathElem& o) const {
        if (is_index != o.is_index) return false;
        if (is_index) return idx == o.idx;
        if (is_token != o.is_token) return false;
        if (is_token) return token_id == o.token_id;
        return key == o.key;
    }
};

using Path = std::vector<PathElem>;

// Flat entry

struct FlatEntry {
    Path  path;
    Value value;
};

// Flatten

// Iterative DFS flattening
static void flatten_value(const Value& node, Path path, std::vector<FlatEntry>& out) {
    struct Frame { const Value* node; Path path; };

    // Use a manual stack; push children in reverse so left child is popped first
    std::vector<Frame> stack;
    stack.push_back({&node, std::move(path)});

    while (!stack.empty()) {
        auto [cur_node, cur_path] = stack.back();
        stack.pop_back();

        if (cur_node->is_object()) {
            if (cur_node->obj_keys.empty()) {
                // Empty object, emit directly
                out.push_back({cur_path, *cur_node});
                continue;
            }
            // Push children in reverse
            for (int i = (int)cur_node->obj_keys.size() - 1; i >= 0; --i) {
                Path child_path = cur_path;
                child_path.push_back(PathElem::from_key(cur_node->obj_keys[i]));
                stack.push_back({&cur_node->obj_vals[i], std::move(child_path)});
            }
        } else if (cur_node->is_array()) {
            if (cur_node->arr.empty()) {
                out.push_back({cur_path, *cur_node});
                continue;
            }
            for (int i = (int)cur_node->arr.size() - 1; i >= 0; --i) {
                Path child_path = cur_path;
                child_path.push_back(PathElem::from_idx(i));
                stack.push_back({&cur_node->arr[i], std::move(child_path)});
            }
        } else {
            out.push_back({cur_path, *cur_node});
        }
    }
}

inline std::vector<FlatEntry> flatten(const Value& root) {
    std::vector<FlatEntry> out;
    Path empty;
    flatten_value(root, empty, out);
    return out;
}

// Unflatten

// Reconstruct nested Value from flat path/value pairs.
// tokens maps integer path keys (token IDs) to real string keys.
static Value unflatten(const std::vector<Path>& all_paths,
                       const std::vector<Value>& values,
                       const std::vector<std::string>& tokens)
{
    auto resolve = [&](const PathElem& e) -> PathElem {
        // In the key sector, token IDs are stored as integers in path elems
        // (not as is_index, those come from Idx). The fields decoder sets
        // is_index=false and key="<int>" when it's a token reference.
        return e; // resolution done in decoder
    };
    (void)resolve;

    // Determine root type from first path
    Value root = Value::make_null();
    if (all_paths.empty()) return root;

    bool root_is_array = (!all_paths[0].empty() && all_paths[0][0].is_index);
    root = root_is_array ? Value::make_array() : Value::make_object();

    for (size_t fi = 0; fi < all_paths.size(); ++fi) {
        const Path& path = all_paths[fi];
        const Value& v   = values[fi];

        Value* cur = &root;
        for (size_t pi = 0; pi + 1 < path.size(); ++pi) {
            const PathElem& p    = path[pi];
            const PathElem& next = path[pi + 1];

            if (p.is_index) {
                // cur must be an array
                while ((int64_t)cur->arr.size() <= p.idx)
                    cur->arr.push_back(Value::make_null());
                if (cur->arr[p.idx].is_null()) {
                    cur->arr[p.idx] = next.is_index ? Value::make_array() : Value::make_object();
                }
                cur = &cur->arr[p.idx];
            } else {
                // cur must be an object
                // find or insert
                size_t ki = cur->obj_keys.size();
                bool found = false;
                for (size_t j = 0; j < cur->obj_keys.size(); ++j) {
                    if (cur->obj_keys[j] == p.key) { ki = j; found = true; break; }
                }
                if (!found) {
                    cur->obj_keys.push_back(p.key);
                    cur->obj_vals.push_back(next.is_index ? Value::make_array() : Value::make_object());
                }
                cur = &cur->obj_vals[ki];
            }
        }

        // Set leaf
        const PathElem& last = path.back();
        if (last.is_index) {
            while ((int64_t)cur->arr.size() <= last.idx)
                cur->arr.push_back(Value::make_null());
            cur->arr[last.idx] = v;
        } else {
            size_t ki = cur->obj_keys.size();
            for (size_t j = 0; j < cur->obj_keys.size(); ++j)
                if (cur->obj_keys[j] == last.key) { ki = j; break; }
            if (ki == cur->obj_keys.size()) {
                cur->obj_keys.push_back(last.key);
                cur->obj_vals.push_back(v);
            } else {
                cur->obj_vals[ki] = v;
            }
        }
    }

    return root;
}
