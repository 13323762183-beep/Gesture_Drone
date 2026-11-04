#ifndef GESTURE_COMMAND_CONFIG_H
#define GESTURE_COMMAND_CONFIG_H

/* Camera/background calibration. A stable empty ROI is collected on startup. */
#define GC_WARMUP_STABLE_FRAMES       32
#define GC_CALIBRATION_FRAMES          16
#define GC_CALIBRATION_MAX_MOTION       3
#define GC_CALIBRATION_REQUIRE_STABLE   1 /* Do not learn a moving hand or settling exposure as background. */
#define GC_VOTE_WINDOW                  7
#define GC_MASK_THRESHOLD              28
#define GC_BACKGROUND_SHIFT_LIMIT      96
#define GC_MASK_DILATION_RADIUS         1 /* Training used 1; compare before changing to 0. */
#define GC_MIN_FOREGROUND_PIXELS      600
#define GC_MAX_FOREGROUND_PERCENT      60 /* Reject broad background/exposure changes. */

/* Consecutive classified frames required before a gesture becomes active. */
#define GC_TRIGGER_FRAMES_V              5
#define GC_TRIGGER_FRAMES_OPEN           3
#define GC_TRIGGER_FRAMES_FIST           3

/* Consecutive OPEN-position frames required before changing direction. */
#define GC_TRIGGER_FRAMES_POSITION       2

/* Recognition mask is 96x72. Bounds are inclusive. */
#define GC_POSITION_CENTER_X_MIN        34
#define GC_POSITION_CENTER_X_MAX        61
#define GC_POSITION_CENTER_Y_MIN        25
#define GC_POSITION_CENTER_Y_MAX        46

/* 0: upper image = FORWARD, lower image = BACK; 1: reverse the mapping. */
#define GC_REVERSE_FORWARD_BACK          0

/* Optional edge guard. Keep 0 when forward/back may move partly out of view. */
#define GC_STOP_WHEN_MASK_TOUCHES_EDGE   0
#define GC_MASK_EDGE_MARGIN              2

/* Command semantics. Change these names without touching recognition code. */
#define GC_ACTION_V_NAME          "UNLOCK"
#define GC_ACTION_OPEN_CENTER_NAME "HOLD"
#define GC_ACTION_FIST_NAME       "LAND"

#endif
