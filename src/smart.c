#include "smart.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/hdreg.h>
#include <linux/types.h>
#include <scsi/sg.h>
#include <stdio.h>
#include <string.h>

#define ATA_SMART_CMD 0xB0
#define ATA_SMART_READ_DATA 0xD0
#define ATA_IDENTIFY_DEVICE 0xEC
#define SG_ATA_16 0x85
#define SG_CDB_LEN 16
#define SG_IO_HDR_SIZE sizeof(struct sg_io_hdr)

static int is_nvme(const char *device) {
    return strstr(device, "nvme") != NULL;
}

int smart_read(const char *device, struct smart_data *out) {
    if (is_nvme(device)) {
        int fd = open(device, O_RDWR);
        if (fd < 0) return 1;
        unsigned char nvme_log[512];
        memset(nvme_log, 0, sizeof(nvme_log));
        struct {
            __u8 opcode;
            __u8 flags;
            __u16 rsvd1;
            __u32 nsid;
            __u64 rsvd2[2];
            __u64 metadata;
            __u64 addr;
            __u32 data_len;
            __u32 cdw10;
            __u32 cdw11;
            __u32 cdw12;
            __u32 cdw13;
            __u32 cdw14;
            __u32 cdw15;
            __u32 timeout_ms;
            __u32 result;
        } cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opcode = 0x02;
        cmd.addr = (__u64)(uintptr_t)nvme_log;
        cmd.data_len = sizeof(nvme_log);
        cmd.nsid = 0xFFFFFFFF;
        cmd.cdw10 = (0x02 << 16) | (sizeof(nvme_log) / 4);
        int err = ioctl(fd, 0xC0484E41, &cmd);
        close(fd);
        if (err < 0) return 1;
        out->is_nvme = 1;
        out->nvme.critical_warning = nvme_log[0];
        out->nvme.temperature = nvme_log[1] | (nvme_log[2]<<8);
        out->nvme.avail_spare = nvme_log[3];
        out->nvme.spare_thresh = nvme_log[4];
        out->nvme.percent_used = nvme_log[5];
        out->nvme.data_units_read = *(uint64_t*)&nvme_log[32];
        out->nvme.data_units_written = *(uint64_t*)&nvme_log[48];
        out->nvme.host_reads = *(uint64_t*)&nvme_log[64];
        out->nvme.host_writes = *(uint64_t*)&nvme_log[80];
        out->nvme.power_cycles = *(uint64_t*)&nvme_log[96];
        out->nvme.power_on_hours = *(uint64_t*)&nvme_log[104];
        out->nvme.unsafe_shutdowns = *(uint64_t*)&nvme_log[112];
        out->nvme.media_errors = *(uint64_t*)&nvme_log[120];
        out->nvme.num_err_log_entries = *(uint64_t*)&nvme_log[128];
        return 0;
    }
    int fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    unsigned char cdb[SG_CDB_LEN];
    memset(cdb, 0, SG_CDB_LEN);
    cdb[0] = SG_ATA_16;
    cdb[1] = 0x08;
    cdb[2] = ATA_SMART_CMD;
    cdb[4] = ATA_SMART_READ_DATA;
    cdb[6] = 0x4F;
    cdb[8] = 0xC2;
    cdb[14] = ATA_SMART_CMD;
    unsigned char data[512];
    memset(data, 0, sizeof(data));
    struct sg_io_hdr io_hdr;
    memset(&io_hdr, 0, sizeof(io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmd_len = SG_CDB_LEN;
    io_hdr.mx_sb_len = 0;
    io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
    io_hdr.dxfer_len = sizeof(data);
    io_hdr.dxferp = data;
    io_hdr.cmdp = cdb;
    io_hdr.timeout = 5000;
    int ret = ioctl(fd, SG_IO, &io_hdr);
    if (ret < 0) {
        perror("ioctl SG_IO");
        close(fd);
        return 1;
    }
    close(fd);
    int count = 0;
    for (int i = 2; i + 12 <= 362 && count < 30; i += 12) {
        uint8_t id = data[i];
        if (id == 0) continue;
        out->attrs[count].id = id;
        out->attrs[count].flags = data[i+1] | (data[i+2]<<8);
        out->attrs[count].value = data[i+3];
        out->attrs[count].worst = data[i+4];
        memcpy(out->attrs[count].raw, &data[i+5], 6);
        count++;
    }
    out->attr_count = count;
    out->is_nvme = 0;
    return 0;
}

int smart_interpret(const char *device, struct smart_data *data) {
    if (data->is_nvme) {
        printf("Diagnóstico SMART NVMe para %s:\n", device);
        printf("Critical Warning: 0x%02X\n", data->nvme.critical_warning);
        printf("Temperature: %u K (%.2f °C)\n", data->nvme.temperature, data->nvme.temperature - 273.15);
        printf("Available Spare: %u%%\n", data->nvme.avail_spare);
        printf("Spare Threshold: %u%%\n", data->nvme.spare_thresh);
        printf("Percentage Used: %u%%\n", data->nvme.percent_used);
        printf("Power On Hours: %llu\n", data->nvme.power_on_hours);
        printf("Unsafe Shutdowns: %llu\n", data->nvme.unsafe_shutdowns);
        printf("Media Errors: %llu\n", data->nvme.media_errors);
        printf("Data Units Read: %llu\n", data->nvme.data_units_read);
        printf("Data Units Written: %llu\n", data->nvme.data_units_written);
        if (data->nvme.percent_used > 90)
            printf("ALERTA: SSD NVMe próximo do fim da vida útil!\n");
        if (data->nvme.critical_warning)
            printf("ALERTA: Critical Warning ativo!\n");
        if (data->nvme.media_errors > 0)
            printf("ALERTA: Media Errors detectados!\n");
        return 0;
    }
    printf("Diagnóstico SMART real para %s:\n", device);
    int reallocated = -1, pending = -1, poweron = -1, offline_unc = -1;
    for (int i = 0; i < data->attr_count; ++i) {
        uint8_t id = data->attrs[i].id;
        uint64_t raw = 0;
        for (int j = 0; j < 6; ++j) raw |= ((uint64_t)data->attrs[i].raw[j]) << (8*j);
        if (id == 5) reallocated = raw;
        if (id == 9) poweron = raw;
        if (id == 197) pending = raw;
        if (id == 198) offline_unc = raw;
        printf("ID %3d | Value %3d | Worst %3d | RAW %-10llu\n", id, data->attrs[i].value, data->attrs[i].worst, raw);
    }
    if (reallocated >= 0) {
        if (reallocated > 10)
            printf("Setores realocados: %d. ALERTA VERMELHO!\n", reallocated);
        else if (reallocated > 0)
            printf("Setores realocados: %d. Atenção!\n", reallocated);
        else
            printf("Setores realocados: %d. OK.\n", reallocated);
    }
    if (pending >= 0) {
        if (pending > 0)
            printf("Pending sectors: %d. ALERTA!\n", pending);
        else
            printf("Pending sectors: %d. OK.\n", pending);
    }
    if (offline_unc >= 0) {
        if (offline_unc > 0)
            printf("Uncorrectable sectors: %d. ALERTA!\n", offline_unc);
        else
            printf("Uncorrectable sectors: %d. OK.\n", offline_unc);
    }
    if (poweron >= 0)
        printf("Horas ligadas: %d\n", poweron);
    return 0;
}
