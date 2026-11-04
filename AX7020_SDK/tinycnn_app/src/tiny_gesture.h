#ifndef TINY_GESTURE_H
#define TINY_GESTURE_H

#define TINY_GESTURE_CLASSES 3
#define TINY_GESTURE_INPUT_CHANNELS 1
#define TINY_GESTURE_HEIGHT 72
#define TINY_GESTURE_WIDTH 96

/* Input channel 0: foreground mask in {0,1}. */
void tiny_gesture_infer(const float *input, float logits[TINY_GESTURE_CLASSES]);

#endif
