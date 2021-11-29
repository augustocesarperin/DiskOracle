#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdbool.h>

/**
 * @brief Executes the S.M.A.R.T. data analysis for a specified device.
 *
 * This function orchestrates the process of fetching, analyzing, and displaying
 * S.M.A.R.T. data for the given device path. It serves as the primary entry
 * point for the "--smart" command-line action.
 *
 * @param device_path The platform-specific path to the target device 
 *                    (e.g., "\\\\.\\PhysicalDrive0" or "/dev/sda").
 * @return int Returns EXIT_SUCCESS (0) on successful execution and data display,
 *             or EXIT_FAILURE (1) if any error occurs during the process.
 */
int execute_smart_command(const char* device_path);

/**
 * @brief Exports SMART, device, and health data to a JSON file or stdout.
 *
 * This function handles the "--smart-json" command. It gathers all necessary
 * device information and SMART data before calling the JSON export utility.
 *
 * @param device_path The platform-specific path to the target device.
 * @param output_file The path to the output JSON file. If NULL, output is
 *                    printed to the standard output.
 * @return int Returns EXIT_SUCCESS (0) on success, or EXIT_FAILURE (1) on error.
 */
int execute_json_export_command(const char* device_path, const char* output_file);

#endif // COMMANDS_H 