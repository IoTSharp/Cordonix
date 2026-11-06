#include "cdx/file_ops.h"
#include "cdx/filesystem.h"
#include "file_ops_internal.h"
#include <stdlib.h>
#include <string.h>

void cdx_file_execute(const cdx_file_request *request, cdx_file_result *result) {
    cdx_path_char *source = NULL, *destination = NULL;
    cdx_fs_status status;
    uint64_t started;
    if (!result) return;
    memset(result, 0, sizeof(*result));
    result->status = "validation_error";
    result->cleanup_complete = 1;
    if (!request || request->operation < CDX_FILE_HASH || request->operation > CDX_FILE_MOVE ||
        !request->source || !request->timeout_ms || request->timeout_ms > 60000u ||
        !request->max_bytes || request->max_bytes > CDX_FILE_MAX_BYTES ||
        (request->operation == CDX_FILE_HASH && request->destination) ||
        (request->operation != CDX_FILE_HASH && !request->destination)) return;
    started = cdx_file_clock_ms();
    status = cdx_fs_normalize_path(request->source, request->cancellation, &source, &result->native_error);
    if (status != CDX_FS_OK) { result->status = cdx_fs_status_name(status); goto done; }
    if (request->destination) {
        status = cdx_fs_normalize_path(request->destination, request->cancellation, &destination, &result->native_error);
        if (status != CDX_FS_OK) { result->status = cdx_fs_status_name(status); goto done; }
    }
    /* Normalization validates path syntax; open the original path to retain native traversal. */
    cdx_file_platform_execute(request, started + request->timeout_ms, result);
done:
    result->elapsed_ms = cdx_file_clock_ms() - started;
    free(source); free(destination);
}

static uint32_t rotate_right(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32u - count));
}

static void sha256_block(uint32_t state[8], const unsigned char block[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t words[64], a, b, c, d, e, f, g, h;
    unsigned i;
    for (i = 0; i < 16; ++i)
        words[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    for (i = 16; i < 64; ++i) {
        uint32_t x = words[i - 15], y = words[i - 2];
        words[i] = words[i - 16] + (rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3)) + words[i - 7] +
                   (rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10));
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];
    for (i = 0; i < 64; ++i) {
        uint32_t t1 = h + (rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25)) +
                      ((e & f) ^ (~e & g)) + constants[i] + words[i];
        uint32_t t2 = (rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void cdx_file_sha256_init(cdx_file_sha256 *context) {
    static const uint32_t initial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    memcpy(context->state, initial, sizeof(initial)); context->length = 0; context->used = 0;
}

void cdx_file_sha256_update(cdx_file_sha256 *context, const unsigned char *bytes, size_t length) {
    size_t offset = 0;
    context->length += length;
    if (context->used) {
        size_t part = 64u - context->used;
        if (part > length) part = length;
        memcpy(context->block + context->used, bytes, part);
        context->used += part; offset += part;
        if (context->used == 64u) { sha256_block(context->state, context->block); context->used = 0; }
    }
    for (; offset + 64u <= length; offset += 64u) sha256_block(context->state, bytes + offset);
    if (offset < length) {
        context->used = length - offset;
        memcpy(context->block, bytes + offset, context->used);
    }
}

void cdx_file_sha256_finish(cdx_file_sha256 *context, char output[65]) {
    unsigned char final[128] = {0};
    static const char hex[] = "0123456789abcdef";
    size_t final_size = context->used < 56u ? 64u : 128u;
    uint64_t bits = context->length * 8u;
    unsigned i;
    memcpy(final, context->block, context->used); final[context->used] = 0x80;
    for (i = 0; i < 8; ++i) final[final_size - 1u - i] = (unsigned char)(bits >> (i * 8u));
    sha256_block(context->state, final);
    if (final_size == 128u) sha256_block(context->state, final + 64u);
    for (i = 0; i < 32; ++i) {
        unsigned char byte = (unsigned char)(context->state[i / 4u] >> (24u - (i % 4u) * 8u));
        output[i * 2u] = hex[byte >> 4]; output[i * 2u + 1u] = hex[byte & 15u];
    }
    output[64] = 0;
}
