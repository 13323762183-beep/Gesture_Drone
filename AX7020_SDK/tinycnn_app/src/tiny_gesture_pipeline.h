#ifndef TINY_GESTURE_PIPELINE_H
#define TINY_GESTURE_PIPELINE_H

#include <stdint.h>
#include "gesture_command_config.h"

#define TINY_GESTURE_WIDTH  96
#define TINY_GESTURE_HEIGHT 72

typedef struct {
    int raw;
    int cx;
    int cy;
    int left;
    int top;
    int right;
    int bottom;
    int touches_edge;
    int foreground_pixels;
    int valid_pixels;
    int calibration_remaining;
} TinyGestureResult;

void tiny_gesture_pipeline_reset(void);
void tiny_gesture_pipeline_request_recalibration(void);
int tiny_gesture_pipeline_calibration_remaining(void);
int tiny_gesture_pipeline_last_foreground_pixels(void);
int tiny_gesture_pipeline_last_valid_pixels(void);

/* frame is 1280x720 BGR888. Returns -1 while calibrating, otherwise 0=NONE, 1=V, 2=OPEN, 3=FIST. */
int tiny_gesture_pipeline_process_ex(const uint8_t *frame, int stride,
                                     float logits[3], TinyGestureResult *result);
int tiny_gesture_pipeline_process(const uint8_t *frame, int stride, float logits[3]);

#endif
