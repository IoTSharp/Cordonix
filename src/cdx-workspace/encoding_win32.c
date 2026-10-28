#include "cdx/document.h"

#include <bcrypt.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "bcrypt.lib")

static int utf8_to_wide(const char *input, size_t length, wchar_t **output, size_t *output_length) {
    int required;
    wchar_t *buffer;
    if (length == 0) { buffer = (wchar_t *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(wchar_t)); if (!buffer) return 0; *output = buffer; *output_length = 0; return 1; }
    if (length > INT_MAX) return 0;
    required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, (int)length, NULL, 0);
    if (required <= 0) return 0;
    buffer = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, ((size_t)required + 1u) * sizeof(wchar_t));
    if (!buffer) return 0;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, (int)length, buffer, required) != required) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return 0;
    }
    buffer[required] = L'\0';
    *output = buffer;
    *output_length = (size_t)required;
    return 1;
}

static int wide_to_utf8(const wchar_t *input, size_t length, char **output, size_t *output_length) {
    int required;
    char *buffer;
    if (length == 0) { buffer = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1); if (!buffer) return 0; *output = buffer; *output_length = 0; return 1; }
    if (length > INT_MAX) return 0;
    required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, (int)length, NULL, 0, NULL, NULL);
    if (required <= 0) return 0;
    buffer = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)required + 1u);
    if (!buffer) return 0;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, (int)length, buffer, required, NULL, NULL) != required) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return 0;
    }
    buffer[required] = '\0';
    *output = buffer;
    *output_length = (size_t)required;
    return 1;
}

static int codepage_to_utf8(UINT codepage, const unsigned char *input, size_t length, char **output, size_t *output_length) {
    int required;
    wchar_t *wide;
    int flags = MB_ERR_INVALID_CHARS;
    if (length == 0) return wide_to_utf8(L"", 0, output, output_length);
    if (length > INT_MAX) return 0;
    required = MultiByteToWideChar(codepage, flags, (const char *)input, (int)length, NULL, 0);
    if (required <= 0) return 0;
    wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, ((size_t)required + 1u) * sizeof(wchar_t));
    if (!wide) return 0;
    if (MultiByteToWideChar(codepage, flags, (const char *)input, (int)length, wide, required) != required) {
        HeapFree(GetProcessHeap(), 0, wide);
        return 0;
    }
    wide[required] = L'\0';
    if (!wide_to_utf8(wide, (size_t)required, output, output_length)) {
        HeapFree(GetProcessHeap(), 0, wide);
        return 0;
    }
    HeapFree(GetProcessHeap(), 0, wide);
    return 1;
}

static int utf8_to_codepage(UINT codepage, const char *input, size_t length, unsigned char **output, size_t *output_length) {
    wchar_t *wide;
    int required;
    unsigned char *buffer;
    BOOL used_default = FALSE;
    if (length == 0) { buffer = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1); if (!buffer) return 0; *output = buffer; *output_length = 0; return 1; }
    if (!utf8_to_wide(input, length, &wide, &length)) return 0;
    if (length > INT_MAX) { HeapFree(GetProcessHeap(), 0, wide); return 0; }
    required = WideCharToMultiByte(codepage, WC_NO_BEST_FIT_CHARS, wide, (int)length, NULL, 0, "?", &used_default);
    if (required <= 0 || used_default) { HeapFree(GetProcessHeap(), 0, wide); return 0; }
    buffer = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, (size_t)required);
    if (!buffer) { HeapFree(GetProcessHeap(), 0, wide); return 0; }
    used_default = FALSE;
    if (WideCharToMultiByte(codepage, WC_NO_BEST_FIT_CHARS, wide, (int)length, (char *)buffer, required, "?", &used_default) != required || used_default) {
        HeapFree(GetProcessHeap(), 0, buffer);
        HeapFree(GetProcessHeap(), 0, wide);
        return 0;
    }
    HeapFree(GetProcessHeap(), 0, wide);
    *output = buffer;
    *output_length = (size_t)required;
    return 1;
}

static int utf8_to_utf16(const char *input, size_t length, int big_endian, unsigned char **output, size_t *output_length) {
    wchar_t *wide;
    unsigned char *buffer;
    size_t index;
    if (!utf8_to_wide(input, length, &wide, &length)) return 0;
    if (length > (SIZE_MAX - 2u) / 2u) { HeapFree(GetProcessHeap(), 0, wide); return 0; }
    buffer = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, length * 2u + 2u);
    if (!buffer) { HeapFree(GetProcessHeap(), 0, wide); return 0; }
    for (index = 0; index < length; ++index) {
        unsigned short value = (unsigned short)wide[index];
        if (big_endian) { buffer[index * 2u] = (unsigned char)(value >> 8); buffer[index * 2u + 1u] = (unsigned char)value; }
        else { buffer[index * 2u] = (unsigned char)value; buffer[index * 2u + 1u] = (unsigned char)(value >> 8); }
    }
    HeapFree(GetProcessHeap(), 0, wide);
    *output = buffer;
    *output_length = length * 2u;
    return 1;
}

int cdx_encode_text(const char *text, size_t text_length, cdx_encoding encoding, unsigned char **bytes, size_t *byte_length) {
    if (encoding == CDX_ENCODING_UTF8 || encoding == CDX_ENCODING_UTF8_BOM) {
        unsigned char *result = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, text_length + (encoding == CDX_ENCODING_UTF8_BOM ? 3u : 1u));
        if (!result) return 0;
        if (encoding == CDX_ENCODING_UTF8_BOM) { result[0] = 0xef; result[1] = 0xbb; result[2] = 0xbf; memcpy(result + 3u, text, text_length); *byte_length = text_length + 3u; }
        else { memcpy(result, text, text_length); *byte_length = text_length; }
        *bytes = result;
        return 1;
    }
    if (encoding == CDX_ENCODING_CP936) return utf8_to_codepage(936u, text, text_length, bytes, byte_length);
    if (encoding == CDX_ENCODING_UTF16LE || encoding == CDX_ENCODING_UTF16BE) {
        unsigned char *body = NULL;
        size_t body_length = 0;
        unsigned char *result;
        if (!utf8_to_utf16(text, text_length, encoding == CDX_ENCODING_UTF16BE, &body, &body_length)) return 0;
        result = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, body_length + 2u);
        if (!result) { HeapFree(GetProcessHeap(), 0, body); return 0; }
        if (encoding == CDX_ENCODING_UTF16BE) { result[0] = 0xfe; result[1] = 0xff; }
        else { result[0] = 0xff; result[1] = 0xfe; }
        memcpy(result + 2u, body, body_length);
        HeapFree(GetProcessHeap(), 0, body);
        *bytes = result; *byte_length = body_length + 2u;
        return 1;
    }
    return 0;
}

int cdx_decode_to_utf8(const unsigned char *input, size_t length, cdx_encoding encoding, char **text, size_t *text_length) {
    wchar_t *wide = NULL;
    size_t wide_length = 0;
    if (encoding == CDX_ENCODING_UTF8 || encoding == CDX_ENCODING_UTF8_BOM) {
        if (!utf8_to_wide((const char *)input, length, &wide, &wide_length)) return 0;
        if (!wide_to_utf8(wide, wide_length, text, text_length)) { HeapFree(GetProcessHeap(), 0, wide); return 0; }
        HeapFree(GetProcessHeap(), 0, wide);
        return 1;
    }
    if (encoding == CDX_ENCODING_CP936) return codepage_to_utf8(936u, input, length, text, text_length);
    if (encoding == CDX_ENCODING_UTF16LE || encoding == CDX_ENCODING_UTF16BE) {
        size_t index;
        if ((length & 1u) != 0u || length / 2u > INT_MAX) return 0;
        wide = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (length / 2u + 1u) * sizeof(wchar_t));
        if (!wide) return 0;
        for (index = 0; index < length / 2u; ++index) {
            unsigned short high = input[index * 2u];
            unsigned short low = input[index * 2u + 1u];
            wide[index] = (wchar_t)(encoding == CDX_ENCODING_UTF16BE ? ((high << 8) | low) : (high | (low << 8)));
        }
        wide[length / 2u] = L'\0';
        if (!wide_to_utf8(wide, length / 2u, text, text_length)) { HeapFree(GetProcessHeap(), 0, wide); return 0; }
        HeapFree(GetProcessHeap(), 0, wide);
        return 1;
    }
    return 0;
}

int cdx_validate_utf8(const char *text, size_t length) {
    if (length == 0) return 1;
    if (length > CDX_DOCUMENT_MAX_BYTES || memchr(text, 0, length) != NULL) return 0;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, (int)length, NULL, 0) > 0;
}

int cdx_sha256(const unsigned char *bytes, size_t length, char output[CDX_SHA256_HEX_LENGTH + 1]) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD object_size = 0, result_size = 0;
    unsigned char *object = NULL;
    unsigned char digest[32];
    static const char hex[] = "0123456789abcdef";
    size_t index;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) return 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size, sizeof(object_size), &result_size, 0) != 0) goto fail;
    object = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, object_size);
    if (!object || BCryptCreateHash(algorithm, &hash, object, object_size, NULL, 0, 0) != 0) goto fail;
    if (length > ULONG_MAX || BCryptHashData(hash, (PUCHAR)bytes, (ULONG)length, 0) != 0 || BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) goto fail;
    for (index = 0; index < sizeof(digest); ++index) { output[index * 2u] = hex[digest[index] >> 4]; output[index * 2u + 1u] = hex[digest[index] & 0x0fu]; }
    output[CDX_SHA256_HEX_LENGTH] = '\0';
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); HeapFree(GetProcessHeap(), 0, object);
    return 1;
fail:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (object) HeapFree(GetProcessHeap(), 0, object);
    return 0;
}
