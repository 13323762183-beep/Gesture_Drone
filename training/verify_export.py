import argparse
import json
import re
from pathlib import Path

import numpy as np
import torch
from torch.nn import functional as F


def array(text, name, shape):
    match = re.search(rf"static const float {name}\[\d+\] = \{{(.*?)\}};", text, re.S)
    assert match, name
    values = [float(item.strip().removesuffix("f")) for item in match.group(1).replace("\n", " ").split(",")]
    return torch.tensor(values, dtype=torch.float32).reshape(shape)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deploy", type=Path)
    args = parser.parse_args()
    text = (args.deploy / "tiny_gesture_weights.h").read_text(encoding="ascii")
    expected = json.loads((args.deploy / "test_expected.json").read_text(encoding="utf-8"))
    classes = len(expected["labels"])
    raw = np.fromfile(args.deploy / "test_vectors.bin", np.float32)
    channels = raw.size // (classes * 72 * 96)
    assert channels in (1, 2), channels
    raw = raw.reshape(classes, channels, 72, 96)
    x = torch.from_numpy(raw)

    layers = [
        ("tg_conv0", (12, channels, 3, 3), (12,), 2, 1, 1),
        ("tg_dw1", (12, 1, 3, 3), (12,), 2, 1, 12),
        ("tg_pw1", (24, 12, 1, 1), (24,), 1, 0, 1),
        ("tg_dw2", (24, 1, 3, 3), (24,), 2, 1, 24),
        ("tg_pw2", (40, 24, 1, 1), (40,), 1, 0, 1),
        ("tg_dw3", (40, 1, 3, 3), (40,), 2, 1, 40),
        ("tg_pw3", (64, 40, 1, 1), (64,), 1, 0, 1),
    ]
    for name, w_shape, b_shape, stride, padding, groups in layers:
        weight = array(text, name + "_w", w_shape)
        bias = array(text, name + "_b", b_shape)
        x = F.relu(F.conv2d(x, weight, bias, stride=stride, padding=padding, groups=groups))
    weight = array(text, "tg_fc_w", (classes, 64))
    bias = array(text, "tg_fc_b", (classes,))
    actual = F.linear(x.mean((-2, -1)), weight, bias)
    reference = torch.tensor(expected["expected_logits"])
    error = float((actual - reference).abs().max())
    assert error < 1e-5, error
    assert actual.argmax(1).tolist() == expected["expected_predictions"]
    print(f"PASS max_abs_error={error:.3g} predictions={actual.argmax(1).tolist()}")


if __name__ == "__main__":
    main()
