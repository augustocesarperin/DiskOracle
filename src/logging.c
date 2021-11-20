#include "logging.h"
#include <stdio.h>
#include <time.h>

int log_event(const char *event) {
    FILE *f = NULL; 
    errno_t err_fopen = fopen_s(&f, "logs/diskoracle.log", "a");
    if (err_fopen != 0 || !f) return 1;

    time_t t = time(NULL);
    struct tm tm_s;
    errno_t err_localtime = localtime_s(&tm_s, &t);
    if (err_localtime != 0) {
        if (f) fclose(f);
        return 1; // Failed to get local time
    }

    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", &tm_s);
    fprintf(f, "%s %s\n", ts, event);
    fclose(f);
    return 0;
}
