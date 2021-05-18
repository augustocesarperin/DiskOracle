#include "pal.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "smart.h"

#if defined(__linux__)
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/nvme_ioctl.h>
#include <scsi/sg.h>

static char* read_sysfs_line(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *line = NULL;
    size_t len = 0;
    ssize_t read_len = getline(&line, &len, f);
    fclose(f);
    if (read_len > 0) {
        if (line[read_len - 1] == '\n') {
            line[read_len - 1] = '\0';
        }
        return line;
    } else {
        if (line) free(line);
        return NULL;
    }
}

static void trim_whitespace(char *str) {
    if (!str || *str == '\0') return;
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    
    char *end = str + strlen(str) - 1;
    while (end >= start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    
    if (start != str) memmove(str, start, strlen(start) + 1);
}

int64_t pal_get_device_size(const char *device_path) {
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    uint64_t size_in_bytes = 0;
    if (ioctl(fd, BLKGETSIZE64, &size_in_bytes) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return (int64_t)size_in_bytes;
}

int pal_list_drives() {
    printf("Available physical drives (Linux):\n");
    printf("%-15s | %-30s | %-25s | %-10s | %s\n", "Device", "Model", "Serial", "Size (GB)", "Type");
    printf("----------------------------------------------------------------------------------------------------\n");

    DIR *dir = opendir("/sys/block");
    if (!dir) {
        perror("pal_list_drives (opendir /sys/block)");
        return 1;
    }

    struct dirent *entry;
    int found_any = 0;
    while ((entry = readdir(dir)) != NULL) {
        const char *dev_name = entry->d_name;
        if (dev_name[0] == '.') continue;
        if (strncmp(dev_name, "loop", 4) == 0 || strncmp(dev_name, "ram", 3) == 0 || strncmp(dev_name, "sr", 2) == 0) {
            continue;
        }

        char dev_path[256];
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", dev_name);

        char model_path[512], serial_path[512], rotational_path[512];
        snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", dev_name);
        snprintf(serial_path, sizeof(serial_path), "/sys/block/%s/device/serial", dev_name);
        snprintf(rotational_path, sizeof(rotational_path), "/sys/block/%s/queue/rotational", dev_name);

        char *model = read_sysfs_line(model_path);
        char *serial = read_sysfs_line(serial_path);
        char *rotational_str = read_sysfs_line(rotational_path);

        char model_disp[31] = "N/A";
        char serial_disp[26] = "N/A";

        if (model) {
            trim_whitespace(model);
            strncpy(model_disp, model, sizeof(model_disp) -1);
            model_disp[sizeof(model_disp)-1] = '\0';
            free(model);
        }
        if (serial) {
            trim_whitespace(serial);
            strncpy(serial_disp, serial, sizeof(serial_disp) -1);
            serial_disp[sizeof(serial_disp)-1] = '\0';
            free(serial);
        }

        const char *type_str = "Unknown";
        if (strncmp(dev_name, "nvme", 4) == 0) {
            type_str = "NVMe";
        } else if (rotational_str) {
            trim_whitespace(rotational_str);
            if (strcmp(rotational_str, "0") == 0) type_str = "SSD";
            else if (strcmp(rotational_str, "1") == 0) type_str = "HDD";
            free(rotational_str);
        }
        else if (strncmp(dev_name, "sd", 2) == 0 || strncmp(dev_name, "hd", 2) == 0) {
             type_str = "SATA/SCSI";
        }

        int64_t size_bytes = pal_get_device_size(dev_path);
        double size_gb = -1.0;
        if (size_bytes > 0) {
            size_gb = (double)size_bytes / (1024.0 * 1024.0 * 1024.0);
        }
        
        printf("%-15s | %-30s | %-25s | %-10.2f | %s\n",
               dev_path, model_disp, serial_disp, size_gb > 0.0 ? size_gb : 0.0, type_str);
        found_any = 1;
    }
    closedir(dir);
    if (!found_any) {
        printf("No suitable physical drives found in /sys/block.\n");
    }
    return 0;
}

bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    if (!device_path || !info) {
        return false;
    }

    memset(info, 0, sizeof(BasicDriveInfo));
    strncpy(info->model, "Unknown", sizeof(info->model) - 1);
    info->model[sizeof(info->model)-1] = '\0';
    strncpy(info->serial, "Unknown", sizeof(info->serial) - 1);
    info->serial[sizeof(info->serial)-1] = '\0';
    strncpy(info->type, "Unknown", sizeof(info->type) - 1);
    info->type[sizeof(info->type)-1] = '\0';
    strncpy(info->bus_type, "Unknown", sizeof(info->bus_type) - 1);
    info->bus_type[sizeof(info->bus_type)-1] = '\0';

    const char *dev_name_start = strrchr(device_path, '/');
    if (dev_name_start) {
        dev_name_start++;
    } else {
        dev_name_start = device_path;
    }

    if (strncmp(dev_name_start, "sd", 2) == 0) {
        strncpy(info->bus_type, "SATA/SCSI", sizeof(info->bus_type) - 1);
    } else if (strncmp(dev_name_start, "hd", 2) == 0) {
        strncpy(info->bus_type, "IDE", sizeof(info->bus_type) - 1);
    } else if (strncmp(dev_name_start, "nvme", 4) == 0) {
        strncpy(info->bus_type, "NVMe", sizeof(info->bus_type) - 1);
    }
    info->bus_type[sizeof(info->bus_type)-1] = '\0';

    char model_path[512], serial_path[512], rotational_path[512], vendor_path[512];
    snprintf(model_path, sizeof(model_path), "/sys/block/%s/device/model", dev_name_start);
    snprintf(serial_path, sizeof(serial_path), "/sys/block/%s/device/serial", dev_name_start);
    snprintf(vendor_path, sizeof(vendor_path), "/sys/block/%s/device/vendor", dev_name_start);
    snprintf(rotational_path, sizeof(rotational_path), "/sys/block/%s/queue/rotational", dev_name_start);

    char *model_str = read_sysfs_line(model_path);
    char *serial_str = read_sysfs_line(serial_path);
    char *vendor_str = read_sysfs_line(vendor_path);
    char *rotational_str = read_sysfs_line(rotational_path);

    if (model_str) {
        trim_whitespace(model_str);
        strncpy(info->model, model_str, sizeof(info->model) - 1);
        info->model[sizeof(info->model)-1] = '\0';
        free(model_str);
    } else if (vendor_str) {
        trim_whitespace(vendor_str);
        if (strcmp(info->model, "Unknown") == 0 || info->model[0] == '\0') {
            strncpy(info->model, vendor_str, sizeof(info->model) - 1);
            info->model[sizeof(info->model)-1] = '\0';
        }
    }
    if (vendor_str) free(vendor_str);

    if (serial_str) {
        trim_whitespace(serial_str);
        strncpy(info->serial, serial_str, sizeof(info->serial) - 1);
        info->serial[sizeof(info->serial)-1] = '\0';
        free(serial_str);
    }

    if (strncmp(dev_name_start, "nvme", 4) == 0) {
        strncpy(info->type, "NVMe", sizeof(info->type) - 1);
    } else if (rotational_str) {
        trim_whitespace(rotational_str);
        if (strcmp(rotational_str, "0") == 0) {
            strncpy(info->type, "SSD", sizeof(info->type) - 1);
        } else if (strcmp(rotational_str, "1") == 0) {
            strncpy(info->type, "HDD", sizeof(info->type) - 1);
        }
        free(rotational_str);
    } else {
        
    }
    info->type[sizeof(info->type)-1] = '\0';
    
    info->size_bytes = pal_get_device_size(device_path);
    info->smart_capable = false; 
    return true;
}

static int nvme_get_health_log(int fd, struct smart_data *out) {
    struct nvme_admin_cmd cmd;
    NVME_HEALTH_INFO_LOG health_log_payload;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = nvme_admin_get_log_page;
    cmd.nsid = NVME_NSID_GLOBAL; 
    cmd.addr = (uint64_t)(uintptr_t)&health_log_payload;
    cmd.data_len = sizeof(health_log_payload);
    cmd.cdw10 = (NVME_LOG_PAGE_HEALTH_INFO << 16) | ((sizeof(health_log_payload) / 4) - 1); 

    if (ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd) < 0) {
        perror("pal_linux: NVME_IOCTL_ADMIN_CMD for Health Info Log failed");
        return 1;
    }

    out->is_nvme = 1;
    out->nvme.critical_warning = health_log_payload.CriticalWarning.AsUchar;
    out->nvme.temperature = (health_log_payload.Temperature[1] << 8) | health_log_payload.Temperature[0];
    out->nvme.avail_spare = health_log_payload.AvailableSpare;
    out->nvme.spare_thresh = health_log_payload.AvailableSpareThreshold;
    out->nvme.percent_used = health_log_payload.PercentageUsed;
    memcpy(out->nvme.data_units_read, health_log_payload.DataUnitRead, sizeof(out->nvme.data_units_read));
    memcpy(out->nvme.data_units_written, health_log_payload.DataUnitWritten, sizeof(out->nvme.data_units_written));
    memcpy(out->nvme.host_reads, health_log_payload.HostReadCommands, sizeof(out->nvme.host_reads));
    memcpy(out->nvme.host_writes, health_log_payload.HostWriteCommands, sizeof(out->nvme.host_writes));
    memcpy(out->nvme.controller_busy_time, health_log_payload.ControllerBusyTime, sizeof(out->nvme.controller_busy_time));
    memcpy(out->nvme.power_cycles, health_log_payload.PowerCycle, sizeof(out->nvme.power_cycles));
    memcpy(out->nvme.power_on_hours, health_log_payload.PowerOnHours, sizeof(out->nvme.power_on_hours));
    memcpy(out->nvme.unsafe_shutdowns, health_log_payload.UnsafeShutdowns, sizeof(out->nvme.unsafe_shutdowns));
    memcpy(out->nvme.media_errors, health_log_payload.MediaError, sizeof(out->nvme.media_errors));
    memcpy(out->nvme.num_err_log_entries, health_log_payload.ErrorLogEntryCount, sizeof(out->nvme.num_err_log_entries));
    return 0;
}

static int ata_sgio_cmd(int fd, uint8_t ata_cmd_code, uint8_t feature_reg, uint8_t sector_count_val, unsigned char *data_buf, unsigned int timeout_val_ms) {
    unsigned char sense_b[32];
    struct sg_io_hdr io_hdr_s;
    unsigned char cdb_s[16]; 

    memset(&io_hdr_s, 0, sizeof(io_hdr_s));
    memset(cdb_s, 0, sizeof(cdb_s));
    memset(sense_b, 0, sizeof(sense_b));

    cdb_s[0] = 0x85; 
    cdb_s[1] = (4 << 1); 
    cdb_s[2] = (1 << 2) | (1 << 1); 
    cdb_s[4] = feature_reg;  
    cdb_s[7] = sector_count_val; 
    cdb_s[10] = 1;    
    cdb_s[12] = 0x4F; 
    cdb_s[14] = 0xC2; 
    cdb_s[15] = ata_cmd_code; 

    io_hdr_s.interface_id = 'S';
    io_hdr_s.cmd_len = sizeof(cdb_s);
    io_hdr_s.mx_sb_len = sizeof(sense_b);
    io_hdr_s.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr_s.dxfer_len = 512;
    io_hdr_s.dxferp = data_buf;
    io_hdr_s.cmdp = cdb_s;
    io_hdr_s.sbp = sense_b;
    io_hdr_s.timeout = timeout_val_ms;

    if (ioctl(fd, SG_IO, &io_hdr_s) < 0) {
        perror("pal_linux: SG_IO ioctl failed");
        return 1;
    }

    if ((io_hdr_s.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
        fprintf(stderr, "pal_linux: SG_IO command error (status=0x%x, host_status=0x%x, driver_status=0x%x)\n",
                io_hdr_s.status, io_hdr_s.host_status, io_hdr_s.driver_status);
        if (io_hdr_s.sb_len_wr > 0) {
            fprintf(stderr, "Sense data: ");
            for (int k = 0; k < io_hdr_s.sb_len_wr; ++k) fprintf(stderr, "%02x ", sense_b[k]);
            fprintf(stderr, "\n");
        }
        return 1;
    }
    return 0;
}

int pal_get_smart_data(const char *device_path, struct smart_data *out) {
    if (!device_path || !out) {
        fprintf(stderr, "pal_get_smart_data (Linux): Invalid parameters.\n");
        return 1;
    }
    memset(out, 0, sizeof(struct smart_data));

    int fd = open(device_path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        fd = open(device_path, O_RDONLY | O_NONBLOCK); 
        if (fd < 0) {
            perror("pal_get_smart_data (Linux): Failed to open device");
            fprintf(stderr, "Device: %s\n", device_path);
            return 1;
        }
    }

    out->is_nvme = 0;
    if (strncmp(device_path, "/dev/nvme", 9) == 0) { 
        if (nvme_get_health_log(fd, out) != 0) {
            close(fd);
            return 2; 
        }
    } else {
        
        unsigned char smart_buffer[512];

        
        if (ata_sgio_cmd(fd, 0xB0, 0xD0, 1, smart_buffer, 5000) != 0) {
            fprintf(stderr, "pal_linux: Failed to read SMART data via SG_IO for %s\n", device_path);
            
            close(fd);
            return 3; 
        }

        out->attr_count = 0;
        for (int i = 2; (i + 11 < 512) && (out->attr_count < MAX_SMART_ATTRIBUTES); i += 12) {
            uint8_t id = smart_buffer[i];
            if (id == 0) continue;
            out->attrs[out->attr_count].id = id;
            out->attrs[out->attr_count].flags = (smart_buffer[i+2] << 8) | smart_buffer[i+1]; 
            out->attrs[out->attr_count].value = smart_buffer[i+3];
            out->attrs[out->attr_count].worst = smart_buffer[i+4];
            memcpy(out->attrs[out->attr_count].raw, &smart_buffer[i+5], 6);
            out->attrs[out->attr_count].threshold = 0; 
            out->attr_count++;
        }

        
        if (ata_sgio_cmd(fd, 0xB0, 0xD1, 1, smart_buffer, 5000) != 0) {
            fprintf(stderr, "pal_linux: Failed to read SMART thresholds via SG_IO for %s (continuing without them).\n", device_path);
            
        } else {
            for (int i = 0; i < out->attr_count; ++i) {
                for (int j = 2; (j + 1 < 512); j += 12) { 
                    uint8_t thresh_id = smart_buffer[j];
                    if (thresh_id == 0) continue;
                    if (out->attrs[i].id == thresh_id) {
                        out->attrs[i].threshold = smart_buffer[j+1];
                        break;
                    }
                }
            }
        }
    }

    close(fd);
    return 0; 
}

#else 

int pal_list_drives() {
    printf("PAL Linux: Not available (not compiling for Linux).\n");
    return 1;
}

int64_t pal_get_device_size(const char *device_path) {
    (void)device_path;
    fprintf(stderr, "pal_get_device_size: Linux PAL not compiled.\n");
    return -1;
}

bool pal_get_basic_drive_info(const char *device_path, BasicDriveInfo *info) {
    (void)device_path;
    if (info) {
        memset(info, 0, sizeof(BasicDriveInfo));
        strncpy(info->model, "N/A Linux", sizeof(info->model) - 1);
        strncpy(info->serial, "N/A Linux", sizeof(info->serial) - 1);
        strncpy(info->type, "N/A Linux", sizeof(info->type) - 1);
        strncpy(info->bus_type, "N/A Linux", sizeof(info->bus_type) - 1);
    }
    fprintf(stderr, "pal_get_basic_drive_info: Linux PAL not compiled.\n");
    return false;
}

int pal_get_smart_data(const char *device_path, struct smart_data *out) {
    (void)device_path;
    if (out) {
        memset(out, 0, sizeof(struct smart_data));
    }
    fprintf(stderr, "pal_get_smart_data: Linux PAL not compiled, function unavailable on this OS.\n");
    return 1; 
}

#endif 
