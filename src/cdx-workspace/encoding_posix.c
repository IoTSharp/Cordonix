#define _POSIX_C_SOURCE 200809L
#include "cdx/document.h"

#include <iconv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int valid_utf8(const unsigned char *text, size_t length) {
    size_t i = 0;
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return 0;
    while (i < length) {
        unsigned char lead = text[i++];
        unsigned count, j;
        if ((i & 65535u) < 4u && (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec - start.tv_sec >= 15)) return 0;
        if (lead < 0x80u) continue;
        if (lead >= 0xc2u && lead <= 0xdfu) count = 1;
        else if (lead >= 0xe0u && lead <= 0xefu) count = 2;
        else if (lead >= 0xf0u && lead <= 0xf4u) count = 3;
        else return 0;
        if (length - i < count || (lead == 0xe0u && text[i] < 0xa0u) ||
            (lead == 0xedu && text[i] >= 0xa0u) || (lead == 0xf0u && text[i] < 0x90u) ||
            (lead == 0xf4u && text[i] >= 0x90u)) return 0;
        for (j = 0; j < count; ++j) if ((text[i++] & 0xc0u) != 0x80u) return 0;
    }
    return 1;
}

static int convert(const char *from, const char *to, const unsigned char *input,
                   size_t length, unsigned char **output, size_t *output_length) {
    iconv_t converter;
    unsigned char *buffer;
    char *source = (char *)input, *target;
    size_t remaining = length, capacity, available, converted;
    if (length > CDX_DOCUMENT_MAX_UTF8_BYTES) return 0;
    /* glibc iconv accepts historic UTF-8 sequences beyond the Unicode range. */
    if (!strcmp(from, "UTF-8") && !valid_utf8(input, length)) return 0;
    capacity = length * 4u + 4u;
    buffer = (unsigned char *)malloc(capacity);
    if (!buffer) return 0;
    converter = iconv_open(to, from);
    if (converter == (iconv_t)-1) { free(buffer); return 0; }
    target = (char *)buffer;
    available = capacity - 1u;
    converted = iconv(converter, &source, &remaining, &target, &available);
    if (converted != 0 || remaining != 0) {
        iconv_close(converter); free(buffer); return 0;
    }
    iconv_close(converter);
    *target = 0;
    *output_length = (size_t)(target - (char *)buffer);
    *output = buffer;
    return 1;
}

static const char *encoding_name(cdx_encoding encoding) {
    switch (encoding) {
    case CDX_ENCODING_UTF8:
    case CDX_ENCODING_UTF8_BOM: return "UTF-8";
    case CDX_ENCODING_UTF16LE: return "UTF-16LE";
    case CDX_ENCODING_UTF16BE: return "UTF-16BE";
    case CDX_ENCODING_CP936: return "CP936";
    default: return NULL;
    }
}

int cdx_decode_to_utf8(const unsigned char *input, size_t length, cdx_encoding encoding,
                       char **text, size_t *text_length) {
    unsigned char *result = NULL;
    const char *name = encoding_name(encoding);
    if (!name || length > CDX_DOCUMENT_MAX_BYTES ||
        !convert(name, "UTF-8", input, length, &result, text_length)) return 0;
    *text = (char *)result;
    return 1;
}

int cdx_encode_text(const char *text, size_t text_length, cdx_encoding encoding,
                    unsigned char **bytes, size_t *byte_length) {
    unsigned char *body = NULL, *result;
    size_t body_size, prefix = 0;
    const char *name = encoding_name(encoding);
    if (!name || !convert("UTF-8", name, (const unsigned char *)text, text_length, &body, &body_size)) return 0;
    if (encoding == CDX_ENCODING_UTF8_BOM) prefix = 3;
    else if (encoding == CDX_ENCODING_UTF16LE || encoding == CDX_ENCODING_UTF16BE) prefix = 2;
    if (!prefix) { *bytes = body; *byte_length = body_size; return 1; }
    result = (unsigned char *)malloc(body_size + prefix);
    if (!result) { free(body); return 0; }
    if (encoding == CDX_ENCODING_UTF8_BOM) { result[0] = 0xef; result[1] = 0xbb; result[2] = 0xbf; }
    else if (encoding == CDX_ENCODING_UTF16LE) { result[0] = 0xff; result[1] = 0xfe; }
    else { result[0] = 0xfe; result[1] = 0xff; }
    memcpy(result + prefix, body, body_size);
    free(body);
    *bytes = result; *byte_length = body_size + prefix;
    return 1;
}

int cdx_validate_utf8(const char *text, size_t length) {
    unsigned char *validated = NULL;
    size_t validated_length;
    int valid;
    if (!length) return 1;
    if (length > CDX_DOCUMENT_MAX_BYTES || memchr(text, 0, length)) return 0;
    valid = convert("UTF-8", "UTF-8", (const unsigned char *)text, length, &validated, &validated_length);
    free(validated);
    return valid;
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

int cdx_sha256(const unsigned char *bytes, size_t length, char output[CDX_SHA256_HEX_LENGTH + 1]) {
    uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                         0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    unsigned char final[128] = {0};
    static const char hex[] = "0123456789abcdef";
    size_t offset, remainder, final_size;
    uint64_t bits;
    struct timespec start, now;
    unsigned i;
    if (length > CDX_DOCUMENT_MAX_UTF8_BYTES || (!bytes && length)) return 0;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) return 0;
    for (offset = 0; offset + 64 <= length; offset += 64) {
        sha256_block(state, bytes + offset);
        if ((offset & 65535u) == 0 &&
            (clock_gettime(CLOCK_MONOTONIC, &now) || now.tv_sec - start.tv_sec >= 15)) return 0;
    }
    remainder = length - offset;
    if (remainder) memcpy(final, bytes + offset, remainder);
    final[remainder] = 0x80;
    final_size = remainder < 56 ? 64 : 128;
    bits = (uint64_t)length * 8u;
    for (i = 0; i < 8; ++i) final[final_size - 1u - i] = (unsigned char)(bits >> (i * 8));
    sha256_block(state, final);
    if (final_size == 128) sha256_block(state, final + 64);
    for (i = 0; i < 32; ++i) {
        unsigned char byte = (unsigned char)(state[i / 4] >> (24 - (i % 4) * 8));
        output[i * 2] = hex[byte >> 4]; output[i * 2 + 1] = hex[byte & 15];
    }
    output[64] = 0;
    return 1;
}
