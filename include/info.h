#ifndef INFO_H
#define INFO_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define MAX_DRIVES 16
#define MAX_ATTRIBUTES 30

// Estrutura usada para listar drives em menus e tabelas
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
 * @brief Orchestrates the command-line surface scan operation for a device.
 * 
 * This function handles the --surface command. It gets basic drive info,
 * initializes the UI, runs the scan, cleans up the UI, and displays the final report.
 * 
 * @param device_path The system path to the device (e.g., \\.\PhysicalDrive0).
 */
void run_surface_scan_command(const char *device_path);

#endif // INFO_H
