#include "tiny_gesture_pipeline.h"

#include <stdlib.h>
#include <string.h>

#include "tiny_gesture.h"

#define TG_W TINY_GESTURE_WIDTH
#define TG_H TINY_GESTURE_HEIGHT
#define TG_N (TG_W * TG_H)
#define TG_THRESHOLD GC_MASK_THRESHOLD
#define TG_MIN_FOREGROUND_PIXELS GC_MIN_FOREGROUND_PIXELS

#if GC_CALIBRATION_FRAMES < 1 || GC_CALIBRATION_FRAMES > 257
#error GC_CALIBRATION_FRAMES must be in 1..257 for the 16-bit accumulator
#endif

static uint8_t current[TG_N][3];
static uint8_t previous[TG_N][3];
static uint8_t background[TG_N][3];
static uint8_t mask[TG_N];
static uint8_t dilated[TG_N];
static uint16_t background_sum[TG_N][3];
static float model_input[2 * TG_N];
static int calibration_frames;
static int warmup_frames;
static int have_previous;
static int last_foreground_pixels;
static int last_valid_pixels;

static void downsample(const uint8_t *frame, int stride)
{
    int y, x, yy, xx, channel;
    for (y = 0; y < TG_H; ++y) {
        for (x = 0; x < TG_W; ++x) {
            int sum[3] = {0, 0, 0};
            for (yy = 0; yy < 10; ++yy) {
                const uint8_t *row = frame + (y * 10 + yy) * stride + (300 + x * 10) * 3;
                for (xx = 0; xx < 10; ++xx) {
                    sum[0] += row[xx * 3 + 0];
                    sum[1] += row[xx * 3 + 1];
                    sum[2] += row[xx * 3 + 2];
                }
            }
            for (channel = 0; channel < 3; ++channel)
                current[y * TG_W + x][channel] = (uint8_t)((sum[channel] + 50) / 100);
        }
    }
}

void tiny_gesture_pipeline_reset(void)
{
    calibration_frames = 0;
    warmup_frames = 0;
    have_previous = 0;
    last_foreground_pixels = 0;
    last_valid_pixels = 0;
    memset(background_sum, 0, sizeof(background_sum));
    memset(background, 0, sizeof(background));
    memset(previous, 0, sizeof(previous));
}

void tiny_gesture_pipeline_request_recalibration(void) { tiny_gesture_pipeline_reset(); }

int tiny_gesture_pipeline_calibration_remaining(void)
{
    return (GC_WARMUP_STABLE_FRAMES - warmup_frames) +
           (GC_CALIBRATION_FRAMES - calibration_frames);
}

int tiny_gesture_pipeline_last_foreground_pixels(void) { return last_foreground_pixels; }
int tiny_gesture_pipeline_last_valid_pixels(void) { return last_valid_pixels; }

static int median_shift(int channel)
{
    int histogram[511] = {0};
    int i, value, count = 0;
    for (i = 0; i < TG_N; ++i)
        ++histogram[(int)current[i][channel] - (int)background[i][channel] + 255];
    for (value = 0; value < 511; ++value) {
        count += histogram[value];
        if (count > TG_N / 2) {
            int shift = value - 255;
            return shift < -GC_BACKGROUND_SHIFT_LIMIT ? -GC_BACKGROUND_SHIFT_LIMIT :
                   shift > GC_BACKGROUND_SHIFT_LIMIT ? GC_BACKGROUND_SHIFT_LIMIT : shift;
        }
    }
    return 0;
}

static int frame_motion(void)
{
    long total = 0;
    int i, channel;
    if (!have_previous) return 0;
    for (i = 0; i < TG_N; ++i)
        for (channel = 0; channel < 3; ++channel)
            total += abs((int)current[i][channel] - (int)previous[i][channel]);
    return (int)(total / (TG_N * 3));
}

static void result_clear(TinyGestureResult *result)
{
    if (!result) return;
    result->raw = -1;
    result->cx = result->cy = -1;
    result->left = result->top = result->right = result->bottom = -1;
    result->touches_edge = 0;
    result->foreground_pixels = last_foreground_pixels;
    result->valid_pixels = last_valid_pixels;
    result->calibration_remaining = tiny_gesture_pipeline_calibration_remaining();
}

int tiny_gesture_pipeline_process_ex(const uint8_t *frame, int stride,
                                     float logits[3], TinyGestureResult *result)
{
    int valid_pixels = 0;
    int i, channel, x, y;
    int motion;
    long sum_x = 0, sum_y = 0;
    int left = TG_W, top = TG_H, right = -1, bottom = -1;

    downsample(frame, stride);
    result_clear(result);
    for (i = 0; i < TG_N; ++i)
        valid_pixels += current[i][0] > 4 || current[i][1] > 4 || current[i][2] > 4;
    last_valid_pixels = valid_pixels;
    if (valid_pixels < TG_N / 10) {
        tiny_gesture_pipeline_reset();
        if (result) result->valid_pixels = valid_pixels;
        return -1;
    }

    motion = frame_motion();
    memcpy(previous, current, sizeof(previous));
    have_previous = 1;

    /* Startup calibration advances only while the empty scene is stable. */
    if (warmup_frames < GC_WARMUP_STABLE_FRAMES) {
        warmup_frames = !GC_CALIBRATION_REQUIRE_STABLE || motion <= GC_CALIBRATION_MAX_MOTION ? warmup_frames + 1 : 0;
        if (result) result->calibration_remaining = tiny_gesture_pipeline_calibration_remaining();
        return -1;
    }
    if (calibration_frames < GC_CALIBRATION_FRAMES) {
        if (GC_CALIBRATION_REQUIRE_STABLE && motion > GC_CALIBRATION_MAX_MOTION) {
            calibration_frames = 0;
            memset(background_sum, 0, sizeof(background_sum));
            if (result) result->calibration_remaining = tiny_gesture_pipeline_calibration_remaining();
            return -1;
        }
        for (i = 0; i < TG_N; ++i)
            for (channel = 0; channel < 3; ++channel)
                background_sum[i][channel] += current[i][channel];
        ++calibration_frames;
        if (calibration_frames == GC_CALIBRATION_FRAMES) {
            for (i = 0; i < TG_N; ++i)
                for (channel = 0; channel < 3; ++channel)
                    background[i][channel] = (uint8_t)((background_sum[i][channel] +
                        GC_CALIBRATION_FRAMES / 2) / GC_CALIBRATION_FRAMES);
        }
        if (result) result->calibration_remaining = tiny_gesture_pipeline_calibration_remaining();
        return -1;
    }

    {
        int shift[3] = {median_shift(0), median_shift(1), median_shift(2)};
        for (i = 0; i < TG_N; ++i) {
            int largest = 0;
            for (channel = 0; channel < 3; ++channel) {
                int reference = (int)background[i][channel] + shift[channel];
                int difference;
                if (reference < 0) reference = 0;
                if (reference > 255) reference = 255;
                difference = abs((int)current[i][channel] - reference);
                if (difference > largest) largest = difference;
            }
            mask[i] = largest >= TG_THRESHOLD;
        }
    }

    for (y = 0; y < TG_H; ++y) {
        for (x = 0; x < TG_W; ++x) {
            int value = 0, dy, dx;
            for (dy = -GC_MASK_DILATION_RADIUS; dy <= GC_MASK_DILATION_RADIUS && !value; ++dy) {
                int yy = y + dy;
                if ((unsigned)yy >= TG_H) continue;
                for (dx = -GC_MASK_DILATION_RADIUS; dx <= GC_MASK_DILATION_RADIUS; ++dx) {
                    int xx = x + dx;
                    if ((unsigned)xx < TG_W && mask[yy * TG_W + xx]) {
                        value = 1;
                        break;
                    }
                }
            }
            dilated[y * TG_W + x] = (uint8_t)value;
        }
    }

    last_foreground_pixels = 0;
    for (y = 0; y < TG_H; ++y) {
        for (x = 0; x < TG_W; ++x) {
            if (!dilated[y * TG_W + x]) continue;
            ++last_foreground_pixels;
            sum_x += x;
            sum_y += y;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
        }
    }
    if (result) {
        result->foreground_pixels = last_foreground_pixels;
        result->valid_pixels = last_valid_pixels;
        result->calibration_remaining = 0;
    }
    if (last_foreground_pixels < TG_MIN_FOREGROUND_PIXELS) {
        logits[0] = logits[1] = logits[2] = 0.0f;
        if (result) result->raw = 0;
        return 0;
    }

    if (result) {
        result->cx = (int)(sum_x / last_foreground_pixels);
        result->cy = (int)(sum_y / last_foreground_pixels);
        result->left = left; result->top = top; result->right = right; result->bottom = bottom;
        result->touches_edge = left <= GC_MASK_EDGE_MARGIN || top <= GC_MASK_EDGE_MARGIN ||
                               right >= TG_W - 1 - GC_MASK_EDGE_MARGIN ||
                               bottom >= TG_H - 1 - GC_MASK_EDGE_MARGIN;
    }

    for (i = 0; i < TG_N; ++i) {
        int b = current[i][0], g = current[i][1], r = current[i][2];
        model_input[i] = (0.114f * b + 0.587f * g + 0.299f * r) / 255.0f;
        model_input[TG_N + i] = (float)dilated[i];
    }
    tiny_gesture_infer(model_input + TG_N, logits);
    {
        int prediction = 0;
        for (i = 1; i < 3; ++i)
            if (logits[i] > logits[prediction]) prediction = i;
        if (result) result->raw = prediction + 1;
        return prediction + 1;
    }
}

int tiny_gesture_pipeline_process(const uint8_t *frame, int stride, float logits[3])
{
    return tiny_gesture_pipeline_process_ex(frame, stride, logits, 0);
}
