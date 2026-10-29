#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void) {
    struct timespec value;
    (void)clock_gettime(CLOCK_MONOTONIC, &value);
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static int write_all(int fd, const void *raw, size_t length) {
    const unsigned char *data = (const unsigned char *)raw;
    size_t offset = 0, iteration;
    uint64_t deadline = now_ms() + 5000u;
    for (iteration = 0; iteration <= length && offset < length && now_ms() < deadline; ++iteration) {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written > 0) { offset += (size_t)written; continue; }
        if (written < 0 && errno == EINTR) continue;
        return 0;
    }
    return offset == length;
}

int main(int argc, char **argv) {
    alarm(10u);
    if (argc < 2) return 2;
    if (strcmp(argv[1], "echo") == 0) {
        int index;
        for (index = 2; index < argc; ++index) (void)write_all(STDOUT_FILENO, argv[index], strlen(argv[index]));
    } else if (strcmp(argv[1], "stdin") == 0) {
        unsigned char buffer[4096];
        size_t iteration;
        uint64_t deadline = now_ms() + 5000u;
        for (iteration = 0; iteration < 65536u && now_ms() < deadline; ++iteration) {
            ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count > 0) { if (!write_all(STDOUT_FILENO, buffer, (size_t)count)) return 3; continue; }
            if (count < 0 && errno == EINTR) continue;
            break;
        }
    } else if (strcmp(argv[1], "exit23") == 0) {
        return 23;
    } else if (strcmp(argv[1], "sleep") == 0) {
        sleep(8u);
    } else if (strcmp(argv[1], "output") == 0) {
        char chunk[4096];
        unsigned int index;
        memset(chunk, 'x', sizeof(chunk));
        for (index = 0; index < 256u; ++index) if (!write_all(STDOUT_FILENO, chunk, sizeof(chunk))) return 3;
    } else if (strcmp(argv[1], "streams") == 0) {
        (void)write_all(STDOUT_FILENO, "stdout\r\n\n", 9u);
        (void)write_all(STDERR_FILENO, "stderr\n", 7u);
    } else if (strcmp(argv[1], "invalid-utf8") == 0) {
        const unsigned char bytes[] = {0x76, 0x61, 0x6c, 0x69, 0x64, 0xff, 0x0a};
        (void)write_all(STDOUT_FILENO, bytes, sizeof(bytes));
    } else if (strcmp(argv[1], "close-stdin") == 0) {
        close(STDIN_FILENO);
        sleep(1u);
        (void)write_all(STDOUT_FILENO, "alive", 5u);
    } else if (strcmp(argv[1], "close-streams") == 0) {
        close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO);
        sleep(8u);
    } else if (strcmp(argv[1], "tree") == 0 || strcmp(argv[1], "tree-wait") == 0) {
        pid_t child = fork();
        char text[32];
        int length;
        if (child < 0) return 3;
        if (child == 0) { sleep(8u); _exit(0); }
        length = snprintf(text, sizeof(text), "%ld", (long)child);
        if (length < 0 || !write_all(STDOUT_FILENO, text, (size_t)length)) return 3;
        if (strcmp(argv[1], "tree-wait") == 0) sleep(8u);
    } else if (strcmp(argv[1], "cwd") == 0) {
        char cwd[4096];
        if (!getcwd(cwd, sizeof(cwd))) return 3;
        (void)write_all(STDOUT_FILENO, cwd, strlen(cwd));
    } else {
        return 2;
    }
    return 0;
}
