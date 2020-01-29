#include "surface.h"

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

static void backup_critical_sectors(const char *device) {
    int fd = open(device, O_RDONLY);
    if (fd < 0) return;
    uint8_t buf[1024];
    ssize_t r = read(fd, buf, sizeof(buf));
    close(fd);
    if (r != sizeof(buf)) return;
    const char *dev = strrchr(device, '/');
    if (!dev) dev = device; else dev++;
    char fname[256];
    snprintf(fname, sizeof(fname), "backup/%s_mbr_vbr.bin", dev);
    FILE *f = fopen(fname, "wb");
    if (!f) return;
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);
}

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
    backup_critical_sectors(device);
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
