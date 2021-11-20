#ifndef SURFACE_H
#define SURFACE_H

#include <stdbool.h>
#include <stdint.h>

// Structure to hold surface scan results
typedef struct {
    bool scan_performed;
    uint64_t total_sectors_scanned;
    uint64_t bad_sectors_found;
    uint64_t read_errors;
    double scan_time_seconds;
    char status_message[512];
} SurfaceScanResult;

int surface_scan(const char *device_path, const char *scan_type, SurfaceScanResult *result);

#endif
