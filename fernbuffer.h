#pragma once
/**
 * Encode/decode JSON data using the fernBuffer binary format.
 * All allocations are owned by the library; call fernbuffer_free() when done.
 *
 * Thread safety: encode/decode calls are stateless and safe to call
 * concurrently. fernbuffer_free() must be called from the same thread that
 * received the pointer (or with external synchronisation).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(FERNBUFFER_BUILD_DLL)
#    define FERNBUFFER_API __declspec(dllexport)
#  elif defined(FERNBUFFER_IMPORT_DLL)
#    define FERNBUFFER_API __declspec(dllimport)
#  else
#    define FERNBUFFER_API
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define FERNBUFFER_API __attribute__((visibility("default")))
#else
#  define FERNBUFFER_API
#endif

/**
 * Encode a JSON string into fernBuffer binary format.
 *
 * @param json_in   Null-terminated UTF-8 JSON string.
 * @param json_len  Length of json_in in bytes (not counting the null).
 *                  Pass 0 to have the library call strlen() automatically.
 * @param out_len   On success, receives the number of bytes in the returned
 *                  buffer. Undefined on error.
 * @return          Heap-allocated byte buffer (owned by caller, free with
 *                  fernbuffer_free()), or NULL on error.
 */
FERNBUFFER_API uint8_t* fernbuffer_encode(const char*   json_in,
                                           size_t        json_len,
                                           size_t*       out_len);

/**
 * Decode a fernBuffer binary buffer back into a JSON string.
 *
 * @param data      Pointer to the fernBuffer bytes.
 * @param data_len  Number of bytes in data.
 * @return          Null-terminated UTF-8 JSON string (owned by caller, free
 *                  with fernbuffer_free()), or NULL on error.
 */
FERNBUFFER_API char*    fernbuffer_decode(const uint8_t* data,
                                           size_t         data_len);

/**
 * Free a buffer returned by fernbuffer_encode() or fernbuffer_decode().
 * Passing NULL is a no-op.
 */
FERNBUFFER_API void     fernbuffer_free(void* ptr);

#ifdef __cplusplus
} // extern "C"
#endif
