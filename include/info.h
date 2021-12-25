#ifndef INFO_H
#define INFO_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "smart.h" // Include smart.h to define 'struct smart_data'

#define MAX_DRIVES 16
#define MAX_ATTRIBUTES 30


typedef struct {
    char device_path[256];
    char model[256];
    char serial[128];
    char type[32];
    int64_t size_bytes;
} DriveInfo;

// Estrutura para informações detalhadas de um drive específico
typedef struct {
    char path[256];
    char model[256];
    char serial[128];
    char firmware_rev[64];
    char type[32];
    char bus_type[32];
    int64_t size_bytes;
    bool is_ssd;
    bool smart_capable;
} BasicDriveInfo;

// Estrutura para um único atributo S.M.A.R.T.
typedef struct {
    uint8_t id;
    char name[48];
    uint32_t current_value;
    uint32_t worst_value;
    uint32_t threshold;
    uint64_t raw_value;
    char status[16];
} SmartAttribute;

// Represents a rule for a critical S.M.A.R.T. attribute
typedef struct {
    uint8_t id;
    const char* name;
    uint64_t threshold;
    const char* recommendation;
} CriticalAttributeRule;


// Função de protótipo que pertence a este módulo
void display_drive_info(const char *device_path);

/**
 * @brief Displays only the S.M.A.R.T. and basic information for a drive.
 * 
 * This function retrieves and prints the drive's model, serial, firmware,
 * and a detailed S.M.A.R.T. report without initiating a surface scan.
 * 
 * @param device_path The system path to the device (e.g., \\.\PhysicalDrive0).
 */
void show_drive_smart_info(const char *device_path);

/**
 * @brief Analyzes S.M.A.R.T. data against a set of critical rules and prints recommendations.
 * 
 * This function is intended to be called after the main S.M.A.R.T. table has been displayed.
 * It provides clear, actionable advice if certain critical attributes have non-zero values.
 * 
 * @param output_stream The file stream to print the analysis to (e.g., stdout).
 * @param data A pointer to the populated smart_data structure for the drive.
 */
void run_smart_analysis(FILE* output_stream, const struct smart_data* data);

/**
 * @brief Analyzes NVMe S.M.A.R.T. data and prints recommendations.
 * 
 * This function checks critical NVMe log fields like 'critical_warning',
 * 'percent_used', and 'media_errors' to provide scannable, actionable advice.
 * 
 * @param output_stream The file stream to print the analysis to (e.g., stdout).
 * @param data A pointer to the populated smart_data structure for the drive.
 */
void run_nvme_analysis(FILE* output_stream, const struct smart_data* data);

/**
 * @brief Orchestrates the command-line surface scan operation for a device.
 * 
 * This function handles the --surface command. It gets basic drive info,
 * initializes the UI, runs the scan, cleans up the UI, and displays the final report.
 * 
 * @param device_path The system path to the device (e.g., \\.\PhysicalDrive0).
 */
void run_surface_scan_command(const char *device_path);

#endif // INFO_H
