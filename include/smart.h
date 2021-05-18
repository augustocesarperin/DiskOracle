#ifndef SMART_H
#define SMART_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SMART_ATTRIBUTES 30

typedef enum {
    SMART_HEALTH_UNKNOWN,
    SMART_HEALTH_GOOD,
    SMART_HEALTH_WARNING,
    SMART_HEALTH_FAILING,
    SMART_HEALTH_PREFAIL
} SmartHealthStatus;

struct smart_attr {
    uint8_t id;
    uint16_t flags;
    uint8_t value;
    uint8_t worst;
    uint8_t raw[6];
    uint8_t threshold; 
};

struct smart_nvme {
    uint8_t critical_warning;
    uint16_t temperature;
    uint8_t avail_spare;
    uint8_t spare_thresh;
    uint8_t percent_used;
    uint64_t data_units_read;
    uint64_t data_units_written;
    uint64_t host_read_commands;   
    uint64_t host_write_commands;  
    uint64_t controller_busy_time; 
    uint64_t power_cycles;
    uint64_t power_on_hours;
    uint64_t unsafe_shutdowns;
    uint64_t media_errors;
    uint64_t num_err_log_entries;
};

struct smart_data {
    bool is_nvme; 
    union {       
        struct smart_attr attrs[MAX_SMART_ATTRIBUTES];
        struct smart_nvme nvme;
    } data;       
    int attr_count; 
};

int smart_read(const char *device, struct smart_data *out);
int smart_interpret(const char *device, struct smart_data *data);
SmartHealthStatus smart_get_health_summary(const struct smart_data *data);

#endif
