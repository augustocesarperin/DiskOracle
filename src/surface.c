#include "surface.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "pal.h"
#include <stdlib.h> // Para malloc/free
#include "logging.h" // Para DEBUG_PRINT

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

static int surface_scan_quick(const char *device, SurfaceScanResult *result, scan_callback_t callback, void* user_data, scan_state_t* out_final_state) {
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

    scan_state_t state;
    memset(&state, 0, sizeof(state));
    state.total_blocks = total_blocks_to_check;
    state.start_time = time(NULL);

#ifdef _WIN32
    LARGE_INTEGER last_update_time, current_time;
    QueryPerformanceCounter(&last_update_time);
#else
    struct timespec last_update_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &last_update_time);
#endif
    const double update_interval_ms = 50.0;
    int64_t bytes_since_last_update = 0;

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
        
        state.scanned_blocks = i + 1;
        if (bytes_read < 0) {
            state.bad_blocks++;
        }
        
        offset += jump_increment_blocks * BUFFER_SIZE;
        if (offset >= device_size) offset = device_size - BUFFER_SIZE;

        bool should_update = false;
#ifdef _WIN32
        QueryPerformanceCounter(&current_time);
        double elapsed_ms = (double)(current_time.QuadPart - last_update_time.QuadPart) * 1000.0 / freq.QuadPart;
        if (elapsed_ms >= update_interval_ms || i == total_blocks_to_check - 1) {
            should_update = true;
            last_update_time = current_time;
        }
#else
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed_ms = (current_time.tv_sec - last_update_time.tv_sec) * 1000.0 + (current_time.tv_nsec - last_update_time.tv_nsec) / 1000000.0;
        if (elapsed_ms >= update_interval_ms || i == total_blocks_to_check - 1) {
            should_update = true;
            last_update_time = current_time;
        }
#endif
        
        if (bytes_read > 0) {
            bytes_since_last_update += bytes_read;
        }

        if (callback && should_update) {
            double elapsed_sec = elapsed_ms / 1000.0;
            if (elapsed_sec > 0) {
                state.current_speed_mbps = (bytes_since_last_update / (1024.0 * 1024.0)) / elapsed_sec;
                bytes_since_last_update = 0; // Zera para o próximo intervalo
            }
            callback(&state, user_data);
        }
    }

    // A mensagem de status final é preparada, mas não impressa aqui
    snprintf(result->status_message, sizeof(result->status_message), "Quick scan completed.");

    if (callback) {
        state.current_speed_mbps = 0; // Final update with final numbers
        callback(&state, user_data);
    }
    
    // Copia o estado final para o ponteiro de saída, se fornecido
    if (out_final_state) {
        memcpy(out_final_state, &state, sizeof(scan_state_t));
    }

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
int surface_scan(const char *device_path, const char *scan_type, scan_callback_t callback, void* user_data, scan_state_t* out_final_state) {
    SurfaceScanResult result = {0};

    if (device_path == NULL) {
        fprintf(stderr, "Error: Device path is NULL.\n");
        return 1;
    }

    const char *type_to_run = (scan_type == NULL || strlen(scan_type) == 0) ? "quick" : scan_type;

    if (strcmp(type_to_run, "quick") == 0) {
        return surface_scan_quick(device_path, &result, callback, user_data, out_final_state);
    } else if (strcmp(type_to_run, "deep") == 0) {
        return surface_scan_deep(device_path, &result);
    } else {
        fprintf(stderr, "Unknown scan type '%s'.\n", type_to_run);
        return 1;
    }
}
