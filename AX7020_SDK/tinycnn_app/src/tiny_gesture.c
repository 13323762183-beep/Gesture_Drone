#include "tiny_gesture.h"
#include "tiny_gesture_weights.h"

static float buffer_a[12 * 36 * 48];
static float buffer_b[12 * 36 * 48];

static void conv_relu(const float *input, int in_c, int in_h, int in_w,
                      const float *weight, const float *bias, int out_c,
                      int kernel, int stride, int padding, int groups,
                      float *output, int out_h, int out_w)
{
    const int group_in = in_c / groups;
    const int group_out = out_c / groups;
    for (int oc = 0; oc < out_c; ++oc) {
        const int group = oc / group_out;
        for (int oy = 0; oy < out_h; ++oy) {
            for (int ox = 0; ox < out_w; ++ox) {
                float sum = bias[oc];
                for (int local_ic = 0; local_ic < group_in; ++local_ic) {
                    const int ic = group * group_in + local_ic;
                    for (int ky = 0; ky < kernel; ++ky) {
                        const int iy = oy * stride + ky - padding;
                        if ((unsigned)iy >= (unsigned)in_h) continue;
                        for (int kx = 0; kx < kernel; ++kx) {
                            const int ix = ox * stride + kx - padding;
                            if ((unsigned)ix >= (unsigned)in_w) continue;
                            const int wi = ((oc * group_in + local_ic) * kernel + ky) * kernel + kx;
                            sum += input[(ic * in_h + iy) * in_w + ix] * weight[wi];
                        }
                    }
                }
                output[(oc * out_h + oy) * out_w + ox] = sum > 0.0f ? sum : 0.0f;
            }
        }
    }
}

void tiny_gesture_infer(const float *input, float logits[TINY_GESTURE_CLASSES])
{
    conv_relu(input, TINY_GESTURE_INPUT_CHANNELS, 72, 96, tg_conv0_w, tg_conv0_b, 12, 3, 2, 1, 1, buffer_a, 36, 48);
    conv_relu(buffer_a, 12, 36, 48, tg_dw1_w, tg_dw1_b, 12, 3, 2, 1, 12, buffer_b, 18, 24);
    conv_relu(buffer_b, 12, 18, 24, tg_pw1_w, tg_pw1_b, 24, 1, 1, 0, 1, buffer_a, 18, 24);
    conv_relu(buffer_a, 24, 18, 24, tg_dw2_w, tg_dw2_b, 24, 3, 2, 1, 24, buffer_b, 9, 12);
    conv_relu(buffer_b, 24, 9, 12, tg_pw2_w, tg_pw2_b, 40, 1, 1, 0, 1, buffer_a, 9, 12);
    conv_relu(buffer_a, 40, 9, 12, tg_dw3_w, tg_dw3_b, 40, 3, 2, 1, 40, buffer_b, 5, 6);
    conv_relu(buffer_b, 40, 5, 6, tg_pw3_w, tg_pw3_b, 64, 1, 1, 0, 1, buffer_a, 5, 6);

    float pooled[64];
    for (int channel = 0; channel < 64; ++channel) {
        float sum = 0.0f;
        for (int i = 0; i < 30; ++i) sum += buffer_a[channel * 30 + i];
        pooled[channel] = sum / 30.0f;
    }
    for (int output = 0; output < TINY_GESTURE_CLASSES; ++output) {
        float sum = tg_fc_b[output];
        for (int input_index = 0; input_index < 64; ++input_index)
            sum += pooled[input_index] * tg_fc_w[output * 64 + input_index];
        logits[output] = sum;
    }
}
