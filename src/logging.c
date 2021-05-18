#include "logging.h"
#include <stdio.h>
#include <time.h>

int log_event(const char *event) {
    FILE *f = fopen("logs/diskoracle.log", "a");
    if (!f) return 1;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S", tm);
    fprintf(f, "%s %s\n", ts, event);
    fclose(f);
    return 0;
}
