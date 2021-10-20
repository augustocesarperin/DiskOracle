#ifndef REPORT_H
#define REPORT_H

#include "smart.h"
#include "nvme_hybrid.h"

// Generates a health report for the specified device.
// device_path_in: The path to the device.
// data: Pointer to the SMART data structure for the device.
// format: The desired report format ("json", "txt", "csv").
// output_filepath: Optional path to save the report. If NULL or empty, a default name is generated.
// Returns 0 on success, non-zero on error.
int report_generate(const char *device_path_in, const struct smart_data *data, const char *format, const char *output_filepath);

// Function to display detailed NVMe health alerts
void display_nvme_alerts(const nvme_health_alerts_t* health_alerts);

#endif
