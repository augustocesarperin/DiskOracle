#ifndef NVME_DEFINES_H
#define NVME_DEFINES_H

// NVMe Admin Command Opcodes
#define NVME_ADMIN_COMMAND_GET_LOG_PAGE 0x02
#define NVME_ADMIN_COMMAND_IDENTIFY     0x06
// Add other opcodes as needed

// NVMe Log Page Identifiers
#define NVME_LOG_PAGE_ERROR_INFO        0x01
#define NVME_LOG_PAGE_HEALTH_INFO       0x02
#define NVME_LOG_PAGE_FIRMWARE_SLOT     0x03
// Add other log page IDs as needed

#endif // NVME_DEFINES_H 