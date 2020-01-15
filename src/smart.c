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

int smart_read(const char *device) {
    if (is_nvme(device)) {
        int fd = open(device, O_RDWR);
        if (fd < 0) {
            perror("open");
            return 1;
        }
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
        cmd.opcode = 0x02; // NVMe Admin Get Log Page
        cmd.addr = (__u64)(uintptr_t)nvme_log;
        cmd.data_len = sizeof(nvme_log);
        cmd.nsid = 0xFFFFFFFF;
        cmd.cdw10 = (0x02 << 16) | (sizeof(nvme_log) / 4); // Log page 0x02, length in dwords
        int err = ioctl(fd, 0xC0484E41, &cmd); // NVME_IOCTL_ADMIN_CMD
        if (err < 0) {
            perror("ioctl NVME_IOCTL_ADMIN_CMD");
            close(fd);
            return 1;
        }
        printf("NVMe SMART/Health Log (first 16 bytes): ");
        for (int i = 0; i < 16; ++i) printf("%02X ", nvme_log[i]);
        printf("...\n");
        close(fd);
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
    printf("SMART RAW DATA (first 16 bytes): ");
    for (int i = 0; i < 16; ++i) printf("%02X ", data[i]);
    printf("...\n");
    close(fd);
    return 0;
}

int smart_interpret(const char *device) {
    printf("SMART interpretation for %s: ", device);
    if (is_nvme(device)) {
        printf("NVMe interpretation not implemented yet\n");
        return 1;
    }
    // Exemplo de interpretação simples
    printf("OK (demo)\n");
    return 0;
}
