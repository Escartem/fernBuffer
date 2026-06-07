# 🍃 fernBuffer

A compact binary serialization format for JSON-compatible data.
Designed to be smaller than MessagePack on typical payloads by exploiting structural patterns in JSON: hex strings, repeated keys/values, boolean runs, and integer distributions.

**Note:** this is more of a prototype / proof of concept rather than an actual format, bugs or flaws may exist.

---

## Benchmark

Tested against Python's `msgpack` on random representative payloads:

| Test case | JSON size | fernBuffer size | MsgPack size |
|---|---|---|---|
| log entries | 20.0 KB | 10.9 KB | 15.1 KB |
| flat records | 5.3 KB | 2.1 KB | 4.0 KB |
| hex strings | 3.6 KB | 1.9 KB | 3.4 KB |
| sparse nulls | 3.3 KB | 903 B | 1.9 KB |
| repeated strings | 1.5 KB | 557 B | 1.2 KB |
| float array | 9.1 KB | 4.8 KB | 4.4 KB |
| int array | 3.8 KB | 3.1 KB | 2.6 KB |
| bool array | 2.7 KB | 940 B | 503 B |
| tiny objects | 19 B | 11 B | 10 B |
| deep nesting | 50 B | 18 B | 25 B |

As you can see, not every payload yields a better result than MsgPack, therefore the usage of this library depends on what you need to encode. All files used for the benchmark are available in the `benchmark` folder.

---

## Usage

#### Get the library by building it yourself (see the associated section) or grab it from the Releases page

### C

```c
#include "fernbuffer.h"
#include <stdio.h>

int main(void) {
    // Encode JSON -> binary
    size_t enc_len;
    uint8_t* enc = fernbuffer_encode("{\"name\":\"alice\",\"score\":42}", 0, &enc_len);
    if (!enc) { fputs("encode failed\n", stderr); return 1; }

    printf("encoded: %zu bytes\n", enc_len);

    // Decode binary -> JSON
    char* json = fernbuffer_decode(enc, enc_len);
    fernbuffer_free(enc);
    if (!json) { fputs("decode failed\n", stderr); return 1; }

    printf("decoded: %s\n", json);
    fernbuffer_free(json);
    return 0;
}
```

Both `encode` and `decode` return heap-allocated buffers, always free them with `fernbuffer_free()`.  
Passing `json_len = 0` to `fernbuffer_encode` makes it call `strlen` internally.

---

### Python (ctypes)

```python
import ctypes, json, sys

_lib_name = "fernbuffer.dll"
_fb = ctypes.CDLL(_lib_name)

_fb.fernbuffer_encode.restype  = ctypes.c_void_p
_fb.fernbuffer_encode.argtypes = [ctypes.c_char_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]

_fb.fernbuffer_decode.restype  = ctypes.c_void_p
_fb.fernbuffer_decode.argtypes = [ctypes.c_void_p, ctypes.c_size_t]

_fb.fernbuffer_free.restype  = None
_fb.fernbuffer_free.argtypes = [ctypes.c_void_p]


def encode(obj) -> bytes:
    """Encode a JSON-compatible Python object to fernBuffer bytes."""
    data    = json.dumps(obj, separators=(',', ':')).encode()
    out_len = ctypes.c_size_t(0)
    ptr     = _fb.fernbuffer_encode(data, len(data), ctypes.byref(out_len))
    if not ptr:
        raise RuntimeError("fernbuffer_encode failed")
    result = ctypes.string_at(ptr, out_len.value)
    _fb.fernbuffer_free(ctypes.c_void_p(ptr))
    return result


def decode(data: bytes) -> object:
    """Decode fernBuffer bytes back to a Python object."""
    ptr = _fb.fernbuffer_decode(data, len(data))
    if not ptr:
        raise RuntimeError("fernbuffer_decode failed")
    result = ctypes.string_at(ptr)
    _fb.fernbuffer_free(ctypes.c_void_p(ptr))
    return json.loads(result)

payload  = {"name": "alice", "hash": "96695f422286fc0cc01712420a2da82f", "score": 42}
encoded  = encode(payload)
print(f"encoded: {len(encoded)} bytes")

decoded  = decode(encoded)
print(f"decoded: {decoded}")
assert decoded == payload
```

> `c_void_p` is used as the return type for both functions so ctypes does not
> consume the pointer before you can call `fernbuffer_free`. Using `c_char_p`
> would cause ctypes to copy the string automatically and lose the original
> address, leaking memory.

---

### Web (WASM)

Make sure the module is served over HTTP (browsers block WASM on `file://`).
#### Browser (classic script)

```html
<script src="build/fernbuffer.js"></script>
<script>
  FernBuffer().then(fb => {
    const encoded = fb.encode({ name: "alice", score: 42 });
    console.log("encoded:", encoded);       // Uint8Array

    const decoded = fb.decode(encoded);
    console.log("decoded:", decoded);       // plain JS object
  });
</script>
```

#### ES module / async

```js
import FernBuffer from './build/fernbuffer.js';

const fb = await FernBuffer();

const encoded = fb.encode({ name: "alice", score: 42 });  // Uint8Array
const decoded = fb.decode(encoded);                        // plain JS object
```

`fb.encode` accepts any JSON-serializable value and returns a `Uint8Array`.  
`fb.decode` accepts a `Uint8Array` (or any array-like) and returns a plain JS object.  
No manual WASM memory management needed.

---

### CLI

```
build\fernbuffer_cli.exe encode input.json output.bin
build\fernbuffer_cli.exe decode output.bin decoded.json
```

---

## Building

### Requirements

| Target | Tools needed |
|---|---|
| Windows DLL + CLI | Visual Studio 2017+ with "Desktop development with C++" workload, GNU make |
| WASM | Emscripten (emsdk activated), GNU make |

### Commands

```cmd
make dll    # build/fernbuffer.dll  +  build/fernbuffer.lib
make cli    # build/fernbuffer_cli.exe
make wasm   # build/fernbuffer.js  +  build/fernbuffer.wasm
make all    # all three
make clean  # remove build/
```

`make dll` and `make cli` auto-detect your Visual Studio installation via `vswhere.exe`.

`make wasm` requires Emscripten. Activate it first:
```cmd
C:\emsdk\emsdk_env.bat
make wasm
```

---

## How It Works

### Bit-level packing

All output is bit-packed using `BitWriter`/`BitReader`, data is never byte-aligned unless necessary. This eliminates padding waste common in fixed-width formats.

### Flat serialization with delta-path schema

The input object is flattened into an ordered list of `(path, value)` pairs:

```json
{"a": {"b": 1}, "c": [2, 3]}
-> [(["a","b"], 1), (["c",0], 2), (["c",1], 3)]
```

Paths are encoded as **delta diffs** from the previous path using push/pop operations. Two sibling keys under the same parent only need to encode the leaf key change, not the full parent path each time. This is analogous to a trie traversal.

### Variable-length integer coding

Three integer encoders are available, chosen based on context:

- **Fibonacci (Zeckendorf)** : used for small non-negative integers (key lengths, string lengths, token IDs, field counts). Self-delimiting, no length prefix needed.
- **Elias-Gamma** : used for the format version field.
- **LEB128** : used for the total field count.

Fibonacci coding is particularly compact for small values: `0` encodes in 2 bits, `1` in 3 bits, `2` in 3 bits, `3` in 4 bits. More research is required for dynamically choosing each encoding based on context.

### String compression via hex-encodable parts

Strings that contains hex are stored as raw bytes halving their size. Substrings within larger strings (paths, hashes in URLs) are also detected.

### String/key deduplication via token tables

Before encoding, the format scans all strings and keys for values that appear >2 times. These are collected into two **token tables** (one for string values, one for object keys), written once at the start of the stream. Repeated occurrences are replaced with a compact integer token ID.

### Typed field encoding

Each field carries a 2-bit type tag:

| Tag | Type | Notes |
|-----|------|-------|
| 00  | Special | null, `{}`, `[]`, float32/float64 |
| 01  | String | segmented: raw hex runs + plain UTF-8 + token refs |
| 10  | Int | sign bit + Fibonacci magnitude |
| 11  | Bool | N booleans packed as N bits with Fibonacci-coded count |

Floats are stored as f32 when lossless, f64 otherwise.

### Format Structure

```
[EliasGamma: version]
[1-bit: has_values_sector]
  (if 1): [Fibonacci: token count] [token entries...]
[1-bit: has_keys_sector]
  (if 1): [Fibonacci: token count] [token entries...]
[LEB128: field count]
[fields...]
  each field:
    [delta schema ops: push/pop pairs, terminated by 0 bit]
    [2-bit type tag]
    [type-specific payload]
```

Everything is written into a single bit stream. No sub-stream padding, sectors are written inline.

---

## Known Limitations

### Dense integer arrays are larger than MsgPack

`[0,1,2,...,49]` encodes to ~2.19× MsgPack size. MsgPack uses fixint (1 byte per small int); fernBuffer uses Fibonacci coding (2–5 bits/int) plus per-field schema overhead.

### Float precision

Floats that survive f32 round-trip are stored as 4 bytes. Others use 8 bytes. There is no special handling for NaN, Inf, or subnormals.

### No streaming / incremental decode

The entire buffer must be available before decoding. Not suitable for large streaming payloads.

### No schema-less fast-path for repeated structures

If you have 1000 objects with the same shape (e.g. `[{"x":1,"y":2}, ...]`), the key dedup table helps but the delta schema encoding will still emit a pop+push per entry. A future version could detect repeated schemas and use run-length encoding.

---

## Credits & Contributing

This only uses one dependency, [nlohmann/json](https://github.com/nlohmann/json). 
If you use fernBuffer in your tools, make sure to give proper credits. And if you find any issue or want to add new features feel free to contribute !
