#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

// For main.c and others
#define MAX_PATH_LEN_REPORT 260
#define MAX_PATH_LEN_LOG 260
#define PROJECT_VERSION "0.1-alpha" // Placeholder version

// Log Levels (Example Enum - adjust if logging.c expects something different)
typedef enum {
    LOG_LEVEL_TRACE,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL,
    LOG_LEVEL_OFF
} log_level_t;

// Forward declaration for a function main.c uses
const char* log_level_to_string(log_level_t level); // Actual definition should be in logging.c or similar

// Basic structure for CommandLineParams based on usage in main.c
// This will need to be expanded as we see more members used.
typedef struct {
    char drive_path_raw[MAX_PATH_LEN_REPORT];
    char drive_path_final[MAX_PATH_LEN_REPORT];
    char report_file_path[MAX_PATH_LEN_REPORT];
    char log_file_path[MAX_PATH_LEN_LOG];
    
    log_level_t log_level_enum;
    char* log_level; // string form from argv

    bool show_version;
    bool list_drives;
    bool show_smart;
    bool run_surface_scan;
    char* surface_scan_mode;
    // SurfaceScanMode enum is not defined in pal.h for pal_do_surface_scan.
    // The function pal_do_surface_scan in pal.h does not take a mode parameter.

    unsigned long long start_lba;
    unsigned long long lba_count;

    bool predict_health;
    bool perform_cache_test;
    bool run_nvme_benchmark;
    int nvme_benchmark_iterations;
    bool health_alerts;
    
    char* export_format;
    bool verbose_debug;
    bool quiet_mode;

    // Add other params as identified from errors
    char bad_blocks_file[MAX_PATH_LEN_REPORT];


} CommandLineParams;

// PredictionResult is defined in predict.h
// SurfaceScanResult is defined in surface.h
// nvme_cache_test_result_t is not currently used.
// ReportData is not currently used by main.c.

#endif // PROJECT_CONFIG_H
