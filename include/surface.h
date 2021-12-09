#ifndef SURFACE_H
#define SURFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "info.h" // Para BasicDriveInfo

// Estrutura para manter o estado de um scan de superfície.
// Esta é a definição canônica.
typedef struct {
    uint64_t total_blocks;
    uint64_t scanned_blocks;
    uint64_t bad_blocks;
    uint64_t read_errors;
    double current_speed_mbps;
    time_t start_time;
    time_t last_update_time;
} scan_state_t;

// Callback para o scan de superfície
typedef void (*scan_callback_t)(const scan_state_t* state, void* user_data);

// Estrutura de resultados legada - pode ser mesclada em scan_state_t no futuro
typedef struct {
    bool scan_performed;
    uint64_t total_sectors_scanned;
    uint64_t bad_sectors_found;
    uint64_t read_errors;
    double scan_time_seconds;
    char status_message[256];
} SurfaceScanResult;

// Função principal para iniciar o scan
int surface_scan(const char* device_path, const char* mode, scan_callback_t callback, void* user_data);

#endif
