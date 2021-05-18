#include "surface.h"

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "pal.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <fcntl.h> // For O_RDONLY, F_NOCACHE
#include <unistd.h> // For open, close, pread
#else // Linux and other POSIX
#include <fcntl.h> // For O_RDONLY, O_DIRECT
#include <unistd.h> // For open, close, pread
#endif

static void backup_critical_sectors(const char *device) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    uint8_t buf[1024];
    DWORD bytesRead;
    if (!ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL) || bytesRead != sizeof(buf)) {
        CloseHandle(hFile);
        return;
    }
    CloseHandle(hFile);
#elif defined(__APPLE__)
    int fd = open(device, O_RDONLY);
    if (fd < 0) return;
    // Advise to minimize caching
    (void)fcntl(fd, F_NOCACHE, 1);
    uint8_t buf[1024];
    ssize_t r = read(fd, buf, sizeof(buf)); // read is fine for a small sequential backup
    close(fd);
    if (r != sizeof(buf)) return;
#else // Linux
    int fd = open(device, O_RDONLY); // O_DIRECT not strictly needed for a small backup read
    if (fd < 0) return;
    uint8_t buf[1024];
    ssize_t r = read(fd, buf, sizeof(buf));
    close(fd);
    if (r != sizeof(buf)) return;
#endif

    const char *dev_name_part = strrchr(device, '/');
    if (!dev_name_part) {
        dev_name_part = strrchr(device, '\\');
    }
    if (!dev_name_part) dev_name_part = device; else dev_name_part++;

    char fname[256];
    snprintf(fname, sizeof(fname), "backup/%s_mbr_vbr.bin", dev_name_part);
    FILE *f = fopen(fname, "wb");
    if (!f) return;
    fwrite(buf, 1, sizeof(buf), f);
    fclose(f);
}

int surface_scan_quick(const char *device) {
#ifdef _WIN32
    HANDLE hFile = CreateFileA(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        perror("CreateFileA device for quick scan");
        return 1;
    }
#elif defined(__APPLE__)
    int fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open device for quick scan (macOS)");
        return 1;
    }
    (void)fcntl(fd, F_NOCACHE, 1); // Advise to minimize caching
#else // Linux
    int fd = open(device, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        perror("open device for quick scan (Linux)");
        return 1;
    }
#endif

    int64_t device_size = pal_get_device_size(device);
    if (device_size <= 0) {
        printf("DiskOracle Alert: Could not get device size or device is empty for quick scan on %s.\n", device);
#ifdef _WIN32
        CloseHandle(hFile);
#else // Apple and Linux
        close(fd);
#endif
        return 1;
    }

    uint8_t buf[4096] __attribute__((aligned(4096)));
    off_t offset = 0;
    int bad = 0;
    const int64_t total_blocks_to_check = 2048;
    const int64_t jump_increment_blocks = device_size / (total_blocks_to_check * sizeof(buf)) > 0 ? device_size / (total_blocks_to_check * sizeof(buf)) : 1;
    int64_t blocks_checked = 0;

    printf("DiskOracle Quick Scan started on %s (Size: %lld Bytes). Checking approx %lld blocks.\n", device, (long long)device_size, (long long)total_blocks_to_check);

    for (int64_t i = 0; i < total_blocks_to_check && offset < device_size; ++i) {
        ssize_t bytes_read_count = -1;
#ifdef _WIN32
        LARGE_INTEGER li_offset;
        li_offset.QuadPart = offset;
        DWORD win_bytes_read = 0;
        if (SetFilePointerEx(hFile, li_offset, NULL, FILE_BEGIN) != 0xFFFFFFFF && 
            ReadFile(hFile, buf, sizeof(buf), &win_bytes_read, NULL)) {
            bytes_read_count = win_bytes_read;
        }
#else // Apple and Linux
        bytes_read_count = pread(fd, buf, sizeof(buf), offset);
#endif
        if (bytes_read_count < (ssize_t)sizeof(buf)) {
            // Check if it's an actual error or just EOF for the last block
            if (bytes_read_count < 0 || (offset + (off_t)sizeof(buf) > device_size && bytes_read_count >=0)) {
                 // Potentially normal EOF if bytes_read_count >=0 for the last partial block
                 // However, for block devices and full block reads, a short read before EOF is often an error.
                 // For simplicity here, any short read is treated as suspicious for bad block check.
            } 
            // If bytes_read_count < 0, it's definitely an error.
            // If bytes_read_count >= 0 but less than sizeof(buf), it's a short read.
            // For raw device block reads, a short read before the very end of the disk is usually an error.
            if (bytes_read_count < 0 || (bytes_read_count > 0 && bytes_read_count < (ssize_t)sizeof(buf) && (offset + bytes_read_count < device_size) )) {
                 printf("DiskOracle Warning: Bad block suspected at offset %lld on %s (read %zd bytes).\n", (long long)offset, device, bytes_read_count);
                 bad++;
            } else if (bytes_read_count == 0 && offset < device_size) { // Read 0 bytes before expected EOF
                 printf("DiskOracle Warning: Bad block suspected at offset %lld on %s (read 0 bytes).\n", (long long)offset, device);
                 bad++;
            }
        }

        blocks_checked++;
        offset += jump_increment_blocks * sizeof(buf);
        if(offset >= device_size && i < total_blocks_to_check -1) { 
             offset = device_size - sizeof(buf);
             if(offset < 0) offset = 0; 
        }

        if (i % 100 == 0 || i == total_blocks_to_check - 1) {
            printf("DiskOracle Quick Scan progress: %.2f%%\r", (float)(i + 1) * 100.0f / total_blocks_to_check);
            fflush(stdout);
        }
    }

    printf("\nDiskOracle Quick Scan complete on %s. Total blocks checked: %lld. Bad blocks found: %d\n", device, (long long)blocks_checked, bad);
#ifdef _WIN32
    CloseHandle(hFile);
#else // Apple and Linux
    close(fd);
#endif
    return 0;
}

int surface_scan_deep(const char *device) {
    backup_critical_sectors(device);
#ifdef _WIN32
    HANDLE hFile = CreateFileA(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        perror("CreateFileA device for deep scan");
        return 1;
    }
#elif defined(__APPLE__)
    int fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open device for deep scan (macOS)");
        return 1;
    }
    (void)fcntl(fd, F_NOCACHE, 1); // Advise to minimize caching
#else // Linux
    int fd = open(device, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        perror("open device for deep scan (Linux)");
        return 1;
    }
#endif

    int64_t device_size = pal_get_device_size(device);
    if (device_size <= 0) {
        printf("DiskOracle Alert: Could not get device size or device is empty for deep scan on %s.\n", device);
#ifdef _WIN32
        CloseHandle(hFile);
#else // Apple and Linux
        close(fd);
#endif
        return 1;
    }

    uint8_t buf[512] __attribute__((aligned(512)));
    off_t offset = 0;
    int bad = 0;
    int64_t total_sectors = device_size / sizeof(buf);
    if (device_size > 0 && total_sectors == 0) total_sectors = 1; // Read at least one sector if device_size is less than sector size
    else if (device_size % sizeof(buf) != 0) {
        // Log warning or adjust, for now, we scan full sectors
        printf("DiskOracle Note: Device size %lld on %s is not a perfect multiple of sector size %zu. Deep scan will proceed based on full sectors.\n", (long long)device_size, device, sizeof(buf));
    }

    printf("DiskOracle Deep Scan started on %s (Size: %lld Bytes, Total Sectors: %lld)\n", device, (long long)device_size, (long long)total_sectors);

    for (int64_t i = 0; i < total_sectors; ++i) {
        ssize_t bytes_read_count = -1;
#ifdef _WIN32
        LARGE_INTEGER li_offset;
        li_offset.QuadPart = offset;
        DWORD win_bytes_read = 0;
        if (SetFilePointerEx(hFile, li_offset, NULL, FILE_BEGIN) != 0xFFFFFFFF && 
            ReadFile(hFile, buf, sizeof(buf), &win_bytes_read, NULL)) {
            bytes_read_count = win_bytes_read;
        }
#else // Apple and Linux
        bytes_read_count = pread(fd, buf, sizeof(buf), offset);
#endif

        if (bytes_read_count < (ssize_t)sizeof(buf)) {
            if (bytes_read_count < 0 || (offset + (off_t)sizeof(buf) > device_size && bytes_read_count >=0)) {}
            if (bytes_read_count < 0 || (bytes_read_count > 0 && bytes_read_count < (ssize_t)sizeof(buf) && (offset + bytes_read_count < device_size) )) {
                 printf("DiskOracle Warning: Bad sector suspected at offset %lld (Sector %lld) on %s (read %zd bytes).\n", (long long)offset, (long long)i, device, bytes_read_count);
                 bad++;
            } else if (bytes_read_count == 0 && offset < device_size) { 
                 printf("DiskOracle Warning: Bad sector suspected at offset %lld (Sector %lld) on %s (read 0 bytes).\n", (long long)offset, (long long)i, device);
                 bad++;
            }
        }
        offset += sizeof(buf);

        if (i > 0 && (i % (total_sectors / 100 > 0 ? total_sectors / 100 : 1) == 0 || i == total_sectors - 1)) {
            printf("DiskOracle Deep Scan progress: %.2f%%\r", (float)(i + 1) * 100.0f / total_sectors);
            fflush(stdout);
        }
    }
    printf("\nDiskOracle Deep Scan complete on %s. Total sectors checked: %lld. Bad sectors found: %d\n", device, (long long)total_sectors, bad);
#ifdef _WIN32
    CloseHandle(hFile);
#else // Apple and Linux
    close(fd);
#endif
    return 0;
}

// Updated function to match the signature in surface.h and handle scan type
int surface_scan(const char *device_path, const char *scan_type) {
    if (device_path == NULL) {
        fprintf(stderr, "Oops! DiskOracle needs a device path for the surface scan. Please provide one.\n");
        return 1; // Indicate failure
    }

    // Default to quick scan if scan_type is NULL (though main.c should prevent this)
    const char *type_to_run = (scan_type == NULL) ? "quick" : scan_type;

    printf("Initiating surface scan (type: %s) for: %s\n", type_to_run, device_path);

    if (strcmp(type_to_run, "quick") == 0) {
        return surface_scan_quick(device_path);
    } else if (strcmp(type_to_run, "deep") == 0) {
        return surface_scan_deep(device_path);
    } else {
        fprintf(stderr, "Hmm, DiskOracle doesn't recognize the scan type '%s'. Please use 'quick' or 'deep'.\n", type_to_run);
        return 1; // Indicate failure due to unknown type
    }
}
