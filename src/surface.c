#include "surface.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "pal.h"

#ifdef _WIN32
#include <windows.h>
#include <malloc.h> // Para _aligned_malloc e _aligned_free
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#endif

#define BUFFER_SIZE 4096
#define BUFFER_ALIGNMENT 4096

// Função interna para o scan rápido
static int surface_scan_quick(const char *device, SurfaceScanResult *result) {
#ifdef _WIN32
    LARGE_INTEGER freq, start_time, end_time;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start_time);
#else
    clock_t start_time = clock();
#endif
    result->scan_performed = true;

    HANDLE hFile = INVALID_HANDLE_VALUE;
#ifndef _WIN32
    int fd = -1;
#endif

#ifdef _WIN32
    hFile = CreateFileA(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Could not open device (quick). Error: %lu", GetLastError());
        return 1;
    }
#else
    fd = open(device, O_RDONLY);
    if (fd < 0) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Could not open device (quick).");
        return 1;
    }
#endif

    int64_t device_size = pal_get_device_size(device);
    if (device_size <= 0) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Could not get device size (quick).");
#ifdef _WIN32
        CloseHandle(hFile);
#else
        close(fd);
#endif
        return 1;
    }

#ifdef _WIN32
    uint8_t *buf = (uint8_t *)_aligned_malloc(BUFFER_SIZE, BUFFER_ALIGNMENT);
#else
    uint8_t *buf = (uint8_t *)malloc(BUFFER_SIZE);
#endif

    if (buf == NULL) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Memory allocation failed (quick).");
#ifdef _WIN32
        CloseHandle(hFile);
#else
        close(fd);
#endif
        return 1;
    }

    const int64_t total_blocks_to_check = 2048;
    const int64_t jump_increment_blocks = (device_size / BUFFER_SIZE) / total_blocks_to_check > 0 ? (device_size / BUFFER_SIZE) / total_blocks_to_check : 1;
    int64_t offset = 0;

    printf("DiskOracle Quick Scan started on %s. Checking %lld blocks...\n", device, (long long)total_blocks_to_check);
    fflush(stdout);

    for (int64_t i = 0; i < total_blocks_to_check && offset < device_size; ++i) {
        ssize_t bytes_read = -1;
#ifdef _WIN32
        LARGE_INTEGER li_offset;
        li_offset.QuadPart = offset;
        DWORD win_bytes_read = 0;
        if (SetFilePointerEx(hFile, li_offset, NULL, FILE_BEGIN) && ReadFile(hFile, buf, BUFFER_SIZE, &win_bytes_read, NULL)) {
            bytes_read = win_bytes_read;
        } else {
            result->read_errors++;
        }
#else
        bytes_read = pread(fd, buf, BUFFER_SIZE, offset);
        if (bytes_read < 0) result->read_errors++;
#endif
        result->total_sectors_scanned++;

        if (bytes_read > 0 && bytes_read < BUFFER_SIZE) {
            result->bad_sectors_found++;
        } else if (bytes_read < 0) {
            result->bad_sectors_found++;
        }
        
        offset += jump_increment_blocks * BUFFER_SIZE;
        if (offset >= device_size) offset = device_size - BUFFER_SIZE;

        if (i % 100 == 0 || i == total_blocks_to_check - 1) {
            printf("DiskOracle Quick Scan progress: %.2f%%\r", (float)(i + 1) * 100.0f / total_blocks_to_check);
            fflush(stdout);
        }
    }

    printf("\n"); // Newline after progress bar
    snprintf(result->status_message, sizeof(result->status_message), "Quick scan completed. Blocks checked: %llu, Bad blocks: %llu.", result->total_sectors_scanned, result->bad_sectors_found);

#ifdef _WIN32
    if (buf) _aligned_free(buf);
    CloseHandle(hFile);
#else
    if (buf) free(buf);
    close(fd);
#endif

#ifdef _WIN32
    QueryPerformanceCounter(&end_time);
    result->scan_time_seconds = (double)(end_time.QuadPart - start_time.QuadPart) / freq.QuadPart;
#else
    result->scan_time_seconds = (double)(clock() - start_time) / CLOCKS_PER_SEC;
#endif
    return 0;
}

// Função interna para o scan profundo
static int surface_scan_deep(const char *device, SurfaceScanResult *result) {
#ifdef _WIN32
    LARGE_INTEGER freq, start_time, end_time;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start_time);
#else
    clock_t start_time = clock();
#endif
    result->scan_performed = true;

    HANDLE hFile = INVALID_HANDLE_VALUE;
#ifndef _WIN32
    int fd = -1;
#endif

#ifdef _WIN32
    hFile = CreateFileA(device, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Could not open device (deep). Error: %lu", GetLastError());
        return 1;
    }
#else
    fd = open(device, O_RDONLY);
    if (fd < 0) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Could not open device (deep).");
        return 1;
    }
#endif

    int64_t device_size = pal_get_device_size(device);
    if (device_size <= 0) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Could not get device size (deep).");
#ifdef _WIN32
        CloseHandle(hFile);
#else
        close(fd);
#endif
        return 1;
    }

#ifdef _WIN32
    uint8_t *buf = (uint8_t *)_aligned_malloc(BUFFER_SIZE, BUFFER_ALIGNMENT);
#else
    uint8_t *buf = (uint8_t *)malloc(BUFFER_SIZE);
#endif

    if (buf == NULL) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Memory allocation failed (deep).");
#ifdef _WIN32
        CloseHandle(hFile);
#else
        close(fd);
#endif
        return 1;
    }

    int64_t total_sectors = device_size / BUFFER_SIZE;
    int64_t offset = 0;

    printf("DiskOracle Deep Scan started on %s. Total sectors: %lld...\n", device, (long long)total_sectors);
    fflush(stdout);

    for (int64_t i = 0; i < total_sectors; ++i) {
        ssize_t bytes_read = -1;
#ifdef _WIN32
        LARGE_INTEGER li_offset;
        li_offset.QuadPart = offset;
        DWORD win_bytes_read = 0;
        if (SetFilePointerEx(hFile, li_offset, NULL, FILE_BEGIN) && ReadFile(hFile, buf, BUFFER_SIZE, &win_bytes_read, NULL)) {
            bytes_read = win_bytes_read;
        } else {
            result->read_errors++;
        }
#else
        bytes_read = pread(fd, buf, BUFFER_SIZE, offset);
        if (bytes_read < 0) result->read_errors++;
#endif
        result->total_sectors_scanned++;

        if (bytes_read > 0 && bytes_read < BUFFER_SIZE) {
            result->bad_sectors_found++;
        } else if (bytes_read < 0) {
            result->bad_sectors_found++;
        }
        
        offset += BUFFER_SIZE;

        if (i > 0 && (total_sectors < 100 || i % (total_sectors / 100) == 0 || i == total_sectors - 1)) {
            printf("DiskOracle Deep Scan progress: %.2f%%\r", (float)(i + 1) * 100.0f / total_sectors);
            fflush(stdout);
        }
    }

    printf("\n"); // Newline after progress bar
    snprintf(result->status_message, sizeof(result->status_message), "Deep scan completed. Sectors checked: %llu, Bad sectors: %llu.", result->total_sectors_scanned, result->bad_sectors_found);

#ifdef _WIN32
    if (buf) _aligned_free(buf);
    CloseHandle(hFile);
#else
    if (buf) free(buf);
    close(fd);
#endif

#ifdef _WIN32
    QueryPerformanceCounter(&end_time);
    result->scan_time_seconds = (double)(end_time.QuadPart - start_time.QuadPart) / freq.QuadPart;
#else
    result->scan_time_seconds = (double)(clock() - start_time) / CLOCKS_PER_SEC;
#endif
    return 0;
}


// Função principal exportada, que chama as funções internas
int surface_scan(const char *device_path, const char *scan_type, SurfaceScanResult *result) {
    if (result == NULL) {
        fprintf(stderr, "Error: surface_scan called with NULL result pointer.\n");
        return 1;
    }
    memset(result, 0, sizeof(SurfaceScanResult));

    if (device_path == NULL) {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Device path is NULL.");
        return 1;
    }

    const char *type_to_run = (scan_type == NULL || strlen(scan_type) == 0) ? "quick" : scan_type;

    if (strcmp(type_to_run, "quick") == 0) {
        return surface_scan_quick(device_path, result);
    } else if (strcmp(type_to_run, "deep") == 0) {
        return surface_scan_deep(device_path, result);
    } else {
        snprintf(result->status_message, sizeof(result->status_message), "Error: Unknown scan type '%s'.", type_to_run);
        return 1;
    }
}
