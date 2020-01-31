#include "report.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "predict.h"

static void print_last_log(FILE *f, const char *format) {
    FILE *log = fopen("logs/hdguardian.log", "r");
    if (!log) return;
    char *lines[5] = {0};
    char buf[256];
    int count = 0;
    while (fgets(buf, sizeof(buf), log)) {
        if (lines[4]) free(lines[4]);
        for (int i = 4; i > 0; --i) lines[i] = lines[i-1];
        lines[0] = strdup(buf);
        if (count < 5) count++;
    }
    fclose(log);
    if (strcmp(format, "json") == 0) {
        fprintf(f, ",\"log\":[");
        for (int i = count-1; i >= 0; --i) {
            fprintf(f, "\"%s\"%s", lines[i] ? lines[i] : "", i ? "," : "");
        }
        fprintf(f, "]");
    } else if (strcmp(format, "xml") == 0) {
        fprintf(f, "<log>");
        for (int i = count-1; i >= 0; --i) {
            fprintf(f, "<line>%s</line>", lines[i] ? lines[i] : "");
        }
        fprintf(f, "</log>");
    } else if (strcmp(format, "csv") == 0) {
        for (int i = count-1; i >= 0; --i) {
            fprintf(f, ",%s", lines[i] ? lines[i] : "");
        }
    }
    for (int i = 0; i < 5; ++i) if (lines[i]) free(lines[i]);
}

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
    int pred = predict_failure(device);
    const char *predstr = pred == 2 ? "FAILURE" : (pred == 1 ? "WARNING" : "OK");
    if (strcmp(format, "json") == 0) {
        fprintf(f, "{\"device\":\"%s\",\"status\":\"%s\"", dev, predstr);
        print_last_log(f, format);
        fprintf(f, "}\n");
    } else if (strcmp(format, "xml") == 0) {
        fprintf(f, "<report><device>%s</device><status>%s</status>", dev, predstr);
        print_last_log(f, format);
        fprintf(f, "</report>\n");
    } else if (strcmp(format, "csv") == 0) {
        fprintf(f, "device,status");
        print_last_log(f, format);
        fprintf(f, "\n%s,%s\n", dev, predstr);
    } else {
        fclose(f);
        return 2;
    }
    fclose(f);
    printf("Report written to %s\n", fname);
    return 0;
}
