#ifndef CDX_DOCUMENT_INTERNAL_H
#define CDX_DOCUMENT_INTERNAL_H

#include "cdx/document.h"

int cdx_decode_to_utf8(const unsigned char *, size_t, cdx_encoding, char **, size_t *);
int cdx_encode_text(const char *, size_t, cdx_encoding, unsigned char **, size_t *);
int cdx_sha256(const unsigned char *, size_t, char[CDX_SHA256_HEX_LENGTH + 1]);
int cdx_validate_utf8(const char *, size_t);

void cdx_free_bytes(unsigned char **value);
void cdx_free_text(char **value);
cdx_encoding cdx_detect_encoding(const unsigned char *bytes, size_t length,
                                 cdx_encoding requested, size_t *offset);
cdx_eol cdx_detect_eol(const char *text, size_t length);
int cdx_read_file(const cdx_path_char *path, unsigned char **bytes,
                  size_t *length, cdx_file_version *version);
int cdx_same_version(const cdx_file_version *left, const cdx_file_version *right);

#endif
