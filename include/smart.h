#ifndef SMART_H
#define SMART_H

#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32) || defined(__MINGW32__)
    #include <windows.h>
    #include <nvme.h>
#endif

#define MAX_SMART_ATTRIBUTES 30

#define DRIVE_TYPE_UNKNOWN 0
#define DRIVE_TYPE_ATA 1
#define DRIVE_TYPE_NVME 2

typedef enum {
    SMART_HEALTH_OK,         // 0
    SMART_HEALTH_WARNING,    // 1
    SMART_HEALTH_PREFAIL,    // 2
    SMART_HEALTH_FAILING,    // 3
    SMART_HEALTH_UNKNOWN     // 4
} SmartStatus; // Renamed from SmartHealthStatus 

struct smart_attr {
    uint8_t id;
    char name[32];
    uint16_t flags;
    uint8_t value;
    uint8_t worst;
    uint8_t raw[6];
    uint8_t threshold;
};

// Common SMART Attribute Flags 
#define SMART_ATTR_FLAG_PREFAIL             0x0001 
#define SMART_ATTR_FLAG_ONLINE_COLLECTION   0x0002 
#define SMART_ATTR_FLAG_PERFORMANCE         0x0004 
#define SMART_ATTR_FLAG_ERROR_RATE          0x0008 
#define SMART_ATTR_FLAG_EVENT_COUNT         0x0010 
#define SMART_ATTR_FLAG_SELF_PRESERVING     0x0020 // Attribute is self-preserving (value should not decrease)

struct smart_nvme { 
    NVME_HEALTH_INFO_LOG raw_health_log; 
    uint8_t critical_warning;
    uint8_t temperature[2]; // Kelvin
    uint8_t avail_spare;
    uint8_t spare_thresh;
    uint8_t percent_used;
    uint8_t firmware[8]; // Added Firmware Revision
    uint8_t data_units_read[16];
    uint8_t data_units_written[16];
    uint8_t host_read_commands[16];
    uint8_t host_write_commands[16];
    uint8_t controller_busy_time[16];
    uint8_t power_cycles[16];
    uint8_t power_on_hours[16];
    uint8_t unsafe_shutdowns[16];
    uint8_t media_errors[16];
    uint8_t num_err_log_entries[16];
};

struct nvme_smart_log {
    uint8_t critical_warning;
    uint16_t temperature; 
    uint8_t avail_spare;
    uint8_t spare_thresh;
    uint8_t percent_used;
    uint8_t data_units_read[16];       
    uint8_t data_units_written[16];     
    uint8_t host_read_commands[16];     // Number of read commands completed by the controller, LSB first
    uint8_t host_write_commands[16];    // Number of write commands completed by the controller, LSB first
    uint8_t controller_busy_time[16];   
    uint8_t power_cycles[16];           
    uint8_t power_on_hours[16];         
    uint8_t unsafe_shutdowns[16];       
    uint8_t media_errors[16];          
    uint8_t num_err_log_entries[16];    
    uint32_t warning_composite_temp_time;    
    uint32_t critical_composite_temp_time;   
    uint16_t temp_sensor_1_trans_count;      
    uint16_t temp_sensor_2_trans_count;      
    uint32_t temp_sensor_1_total_time;       
    uint32_t temp_sensor_2_total_time;       
};

union smart_device_data {
    struct smart_attr attrs[MAX_SMART_ATTRIBUTES]; 
    struct smart_nvme nvme;
    struct nvme_smart_log nvme_log;
};

struct smart_data {
    int is_nvme;       
    int drive_type;
    int attr_count;    // Only relevant for ATA
    union smart_device_data data; 
    char device_name[256]; 
};

// Function prototypes

int smart_read(const char *device_path, const char *model, const char *serial, struct smart_data *out);

int smart_interpret(const char* device_path, struct smart_data* data, const char* firmware_rev); 
const char* ata_attribute_name(uint8_t attr_id);
SmartStatus smart_get_health_summary(const struct smart_data *data);

void smart_get_ata_attribute_name(uint8_t id, char* buffer, int buffer_len);

/**
 * @brief Converts the 6-byte raw value array from a S.M.A.R.T. attribute into a 64-bit integer.
 * 
 * @param raw_value A pointer to the 6-byte array.
 * @return The calculated 64-bit integer value.
 */
uint64_t raw_to_uint64(const unsigned char* raw_value);


#endif // SMART_H

