#include "surface.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

int surface_scan_quick(const char *device) {
    int fd = open(device, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    uint8_t buf[4096];
    off_t offset = 0;
    int bad = 0;
    for (int i = 0; i < 1024; ++i) {
        ssize_t r = pread(fd, buf, sizeof(buf), offset);
        if (r < 0) {
            printf("Bad block at offset %lld\n", (long long)offset);
            bad++;
        }
        offset += sizeof(buf) * 1024;
    }
    printf("Quick scan complete. Bad blocks: %d\n", bad);
    close(fd);
    return 0;
}

int surface_scan_deep(const char *device) {
    int fd = open(device, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    uint8_t buf[512];
    off_t offset = 0;
    int bad = 0;
    for (int i = 0; i < 4096; ++i) {
        ssize_t r = pread(fd, buf, sizeof(buf), offset);
        if (r < 0) {
            printf("Bad sector at offset %lld\n", (long long)offset);
            bad++;
        }
        offset += sizeof(buf);
    }
    printf("Deep scan complete. Bad sectors: %d\n", bad);
    close(fd);
    return 0;
}
