#include "logging.h"
#include <stdio.h>

int log_event(const char *event) {
    FILE *f = fopen("logs/hdguardian.log", "a");
    if (!f) return 1;
    fprintf(f, "%s\n", event);
    fclose(f);
    return 0;
}
