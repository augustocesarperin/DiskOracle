#include "predict.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

int predict_failure(const char *device) {
    srand((unsigned)time(NULL));
    int risk = rand() % 100;
    if (risk < 10) {
        printf("Prediction: Aposentadoria eminente para %s\n", device);
        return 2;
    } else if (risk < 30) {
        printf("Prediction: RED FLAG for %s\n", device);
        return 1;
    } else {
        printf("Prediction: Suave para %s\n", device);
        return 0;
    }
}
