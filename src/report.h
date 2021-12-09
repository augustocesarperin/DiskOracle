#ifndef REPORT_H
#define REPORT_H

#include <stdio.h>                   // For FILE type
#include "../include/nvme_hybrid.h" // For nvme_health_alerts_t
#include "smart.h"                   // For struct smart_data (used by report_generate)

void report_display_nvme_alerts(const nvme_health_alerts_t *alerts_data);

// Function to generate a full report (already implemented in report.c)
int report_generate(const char *device_path_in, 
                    const struct smart_data *data, 
                    const char *format, 
                    const char *output_filepath);

// Function to report SMART data to a specified output stream
int report_smart_data(FILE *output, const char *device_path, 
                      struct smart_data *data, const char* firmware_rev);

#endif 