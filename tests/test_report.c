#include "../include/report.h"
#include <assert.h>
#include <stdio.h>

int main() {
    int r = report_generate("/dev/null", "json");
    assert(r == 0);
    r = report_generate("/dev/null", "xml");
    assert(r == 0);
    r = report_generate("/dev/null", "csv");
    assert(r == 0);
    printf("test_report OK\n");
    return 0;
}
