#include "report.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int report_generate(const char *device, const char *format) {
    char fname[256];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tm);
    const char *dev = strrchr(device, '/');
    if (!dev) dev = device; else dev++;
    snprintf(fname, sizeof(fname), "reports/%s_%s.%s", dev, ts, format);
    FILE *f = fopen(fname, "w");
    if (!f) return 1;
    if (strcmp(format, "json") == 0) {
        fprintf(f, "{\"device\":\"%s\",\"status\":\"OK\"}\n", dev);
    } else if (strcmp(format, "xml") == 0) {
        fprintf(f, "<report><device>%s</device><status>OK</status></report>\n", dev);
    } else if (strcmp(format, "csv") == 0) {
        fprintf(f, "device,status\n%s,OK\n", dev);
    } else {
        fclose(f);
        return 2;
    }
    fclose(f);
    printf("Report written to %s\n", fname);
    return 0;
}
