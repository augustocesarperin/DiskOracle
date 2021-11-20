#ifndef PREDICT_H
#define PREDICT_H

// Forward declaration of smart_data to avoid circular dependency if predict.h is included by smart.h users
// However, smart_data is used in the function signature, so smart.h should typically be included before predict.h
// or predict.h should include smart.h if it's a standalone module using smart_data.
// For now, assuming smart.h is included before predict.h where predict_failure is called with smart_data.
struct smart_data; // Forward declaration

typedef enum {
    PREDICT_OK = 0,
    PREDICT_WARNING = 1,
    PREDICT_FAILURE = 2,
    PREDICT_UNKNOWN = 3
} PredictionResult;

// Changed signature to accept smart_data for direct testing
PredictionResult predict_failure(const char *device_context, const struct smart_data *data);

#endif // PREDICT_H
