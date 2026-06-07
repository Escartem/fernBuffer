// High-level JS API injected into the Emscripten module via --post-js.
// After `await FernBuffer()`, the returned module has encode() and decode()
// with no manual WASM memory management needed.

Module['encode'] = function(obj) {
    const json    = JSON.stringify(obj);
    const jsonLen = Module.lengthBytesUTF8(json) + 1;
    const jsonPtr = Module._malloc(jsonLen);
    Module.stringToUTF8(json, jsonPtr, jsonLen);

    const outLenPtr = Module._malloc(4);
    const dataPtr   = Module._fernbuffer_encode(jsonPtr, 0, outLenPtr);
    Module._free(jsonPtr);

    if (!dataPtr) {
        Module._free(outLenPtr);
        throw new Error('fernbuffer_encode failed');
    }
    const len    = Module.HEAPU32[outLenPtr >> 2];
    const result = Module.HEAPU8.slice(dataPtr, dataPtr + len);
    Module._fernbuffer_free(dataPtr);
    Module._free(outLenPtr);
    return result; // Uint8Array
};

Module['decode'] = function(data) {
    if (!(data instanceof Uint8Array)) data = new Uint8Array(data);
    const buf     = Module._malloc(data.length);
    Module.HEAPU8.set(data, buf);
    const jsonPtr = Module._fernbuffer_decode(buf, data.length);
    Module._free(buf);

    if (!jsonPtr) throw new Error('fernbuffer_decode failed');
    const json = Module.UTF8ToString(jsonPtr);
    Module._fernbuffer_free(jsonPtr);
    return JSON.parse(json); // plain JS object
};
