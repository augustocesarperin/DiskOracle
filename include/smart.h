#ifndef SMART_H
#define SMART_H

#include <stdint.h>

struct smart_attr {
    uint8_t id;
    uint16_t flags;
    uint8_t value;
    uint8_t worst;
    uint8_t raw[6];
};

struct smart_nvme {
    uint8_t critical_warning;
    uint16_t temperature;
    uint8_t avail_spare;
    uint8_t spare_thresh;
    uint8_t percent_used;
    uint64_t data_units_read;
    uint64_t data_units_written;
    uint64_t host_reads;
    uint64_t host_writes;
    uint64_t power_cycles;
    uint64_t power_on_hours;
    uint64_t unsafe_shutdowns;
    uint64_t media_errors;
    uint64_t num_err_log_entries;
};

struct smart_data {
    struct smart_attr attrs[30];
    int attr_count;
    struct smart_nvme nvme;
    int is_nvme;
};

int smart_read(const char *device, struct smart_data *out);
int smart_interpret(const char *device, struct smart_data *data);

#endif
