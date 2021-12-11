#ifndef NVME_HYBRID_H
#define NVME_HYBRID_H

#include <windows.h> 
#include <stdint.h>  // uint32_t, uint8_t
#include "smart.h"    // For struct smart_nvme and other SMART definitions

#ifndef NVME_LOG_PAGE_SIZE_BYTES
#define NVME_LOG_PAGE_SIZE_BYTES 512
#endif

#define NVME_LOG_PAGE_HEALTH_INFO 0x02
#define NVME_LOG_PAGE_SIZE_BYTES 512 // Standard size for Health Info Log

// Forward declaration if smart_nvme is complex and defined in smart.h
// struct smart_nvme; 

// Cache-related definitions (NEW)
#define NVME_CACHE_KEY_MAX_LEN 256
#define MAX_CACHE_ENTRIES 16 // Max NVMe devices to cache
#define DEFAULT_NVME_CACHE_AGE_SECONDS (5 * 60) // 5 minutes

// Structure for an individual cache item (NEW)
typedef struct {
    char key[NVME_CACHE_KEY_MAX_LEN];
    struct smart_nvme health_log; // The NVMe SMART data
    time_t timestamp;             // When this entry was updated (Unix time)
    bool is_valid;
} nvme_cache_item_t;

// Structure for the global cache (NEW)
typedef struct {
    nvme_cache_item_t entries[MAX_CACHE_ENTRIES];
    int count; // Number of entries currently in cache
    unsigned int cache_duration_seconds;
    // Consider adding a mutex here for thread-safety in the future
} nvme_global_cache_t;

// Global Cache Management Functions (NEW)
void nvme_cache_global_init(unsigned int duration_seconds);
void nvme_cache_global_cleanup(void); // Cleans up the global cache (invalidates entries, resets init state).
nvme_cache_item_t* nvme_cache_global_get(const char* key);
void nvme_cache_global_update(const char* key, const struct smart_nvme* health_log_to_cache);
void nvme_cache_global_invalidate(const char* key);
void nvme_cache_global_invalidate_all(void);

typedef enum {
    NVME_ACCESS_METHOD_NONE = 0,
    NVME_ACCESS_METHOD_QUERY_PROPERTY = 1,
    NVME_ACCESS_METHOD_PROTOCOL_COMMAND = 2,
    NVME_ACCESS_METHOD_SCSI_PASSTHROUGH = 3,
    NVME_ACCESS_METHOD_ATA_PASSTHROUGH = 4, 
    NVME_ACCESS_METHOD_UNKNOWN = 99,
    NVME_ACCESS_METHOD_CACHE = 100
} nvme_access_method_t;

// Constants for method names
#define NVME_METHOD_NAME_QUERY_PROPERTY "IOCTL_STORAGE_QUERY_PROPERTY"
#define NVME_METHOD_NAME_PROTOCOL_COMMAND "IOCTL_STORAGE_PROTOCOL_COMMAND"
#define NVME_METHOD_NAME_SCSI_PASSTHROUGH "IOCTL_SCSI_PASS_THROUGH_DIRECT"
#define NVME_METHOD_NAME_ATA_PASSTHROUGH "IOCTL_ATA_PASS_THROUGH"
#define NVME_METHOD_NAME_CACHE "Cached Data"
#define NVME_METHOD_NAME_NONE "None"
#define NVME_METHOD_NAME_UNKNOWN "Unknown"

#define MAX_NVME_ACCESS_METHODS 10 

typedef struct {
    nvme_access_method_t method_used;
    char method_name[64];
    DWORD execution_time_ms; 
    BOOL success;
    DWORD error_code;        
} nvme_access_result_t;

// Cache structure
typedef struct {
    uint8_t cached_data[NVME_LOG_PAGE_SIZE_BYTES];
    SYSTEMTIME last_update_time; 
    BOOL is_valid;
    char device_signature[128];
} nvme_smart_cache_t;


// Hybrid context
typedef struct {
    char device_path[MAX_PATH];  
    char current_device_signature[128];

    // Flags de controle
    BOOL try_query_property;
    BOOL try_protocol_command;
    BOOL try_scsi_passthrough;
    BOOL try_ata_passthrough;
    
    // Flags da CLI
    BOOL prefer_raw_commands;     
    BOOL benchmark_mode;          
    BOOL verbose_logging;      
    BOOL quiet_mode;
    
    // Cache
    BOOL cache_enabled;                         
    DWORD cache_duration_seconds;
    nvme_smart_cache_t smart_cache; 

    nvme_access_result_t last_operation_result; 
    
    // Benchmark
    nvme_access_result_t benchmark_method_results[MAX_NVME_ACCESS_METHODS]; 
    int num_benchmark_results_stored;
    int benchmark_iterations;

} nvme_hybrid_context_t;


// Tipo de ponteiro de função para diferentes métodos
typedef int (*nvme_specific_access_func_t)(const char* device_path, 
                                           const nvme_hybrid_context_t* context,
                                           BYTE* user_buffer, 
                                           DWORD user_buffer_size, 
                                           DWORD* bytes_returned,
                                           nvme_access_result_t* method_result);

// Funções cache (nvme_cache.c)
void nvme_cache_init(nvme_hybrid_context_t* context);
BOOL nvme_cache_get(nvme_hybrid_context_t* context, BYTE* out_buffer, DWORD* out_bytes_returned, nvme_access_result_t* cache_hit_result);
void nvme_cache_update(nvme_hybrid_context_t* context, const BYTE* data_to_cache, DWORD data_size);
void nvme_cache_invalidate(nvme_hybrid_context_t* context);
void nvme_cache_generate_signature(const char* model, const char* serial, char* out_signature, size_t signature_buffer_size);

// Funções benchmark (nvme_benchmark.c)
void nvme_benchmark_init(nvme_hybrid_context_t* context);
void nvme_benchmark_record_result(nvme_hybrid_context_t* context, const nvme_access_result_t* result_to_record);
void nvme_benchmark_print_report(const nvme_hybrid_context_t* context);

// Alertas de saúde NVMe

typedef enum {
    NVME_ALERT_TYPE_NONE = 0,
    NVME_ALERT_TEMPERATURE_HIGH,
    NVME_ALERT_SPARE_LOW,
    NVME_ALERT_PERCENTAGE_USED_HIGH,
    NVME_ALERT_CRITICAL_WARNING_FLAGS,
    NVME_ALERT_UNSAFE_SHUTDOWNS_HIGH,
    NVME_ALERT_MEDIA_ERRORS_HIGH,
    NVME_ALERT_ERROR_LOG_ENTRIES_HIGH
} nvme_alert_type_t;

#define MAX_NVME_ALERT_DESCRIPTION 128
#define MAX_NVME_ALERT_VALUE_STR 64

typedef struct {
    nvme_alert_type_t alert_type;
    char description[MAX_NVME_ALERT_DESCRIPTION];
    char current_value_str[MAX_NVME_ALERT_VALUE_STR];
    char threshold_str[MAX_NVME_ALERT_VALUE_STR];
    BOOL is_critical;
} nvme_alert_info_t;

#define MAX_NVME_ALERTS 10

typedef struct {
    nvme_alert_info_t alerts[MAX_NVME_ALERTS];
    int alert_count;
} nvme_health_alerts_t;

struct BasicDriveInfo;

void nvme_analyze_health_alerts(
    const struct smart_nvme* smart_log, 
    nvme_health_alerts_t* health_alerts_out,
    BYTE device_spare_threshold
);

#endif // NVME_HYBRID_H
