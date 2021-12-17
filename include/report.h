#ifndef REPORT_H
#define REPORT_H

#include <stdio.h>
#include "smart.h"
#include "nvme_hybrid.h"

/**
 * @brief Generates a human-readable S.M.A.R.T. report to a specified output stream.
 *
 * This function takes the interpreted S.M.A.R.T. data and prints a formatted
 * report. The output can be directed to the console (stdout) or a file.
 *
 * @param output_stream The stream to write the report to (e.g., stdout or a file handle).
 * @param device_path The system path of the device being reported on.
 * @param data A pointer to the smart_data structure containing the S.M.A.R.T. info.
 * @param firmware_rev A string containing the device's firmware revision.
 * @return 0 on success, non-zero on failure.
 */
int report_smart_data(FILE* output_stream, const char *device_path, struct smart_data *data, const char* firmware_rev);


int report_generate(const char *device_path_in, const struct smart_data *data, const char *format, const char *output_filepath);
void display_nvme_alerts(const nvme_health_alerts_t* health_alerts);

#endif
