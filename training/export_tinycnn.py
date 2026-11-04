import argparse
import importlib.util
import json
from pathlib import Path

import numpy as np
import torch


def load_training_module(path):
    spec = importlib.util.spec_from_file_location("tiny_train", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fuse(state, conv_key, bn_key):
    weight = state[f"{conv_key}.weight"].float()
    scale = state[f"{bn_key}.weight"].float() / torch.sqrt(
        state[f"{bn_key}.running_var"].float() + 1e-5
    )
    fused_weight = weight * scale[:, None, None, None]
    fused_bias = state[f"{bn_key}.bias"].float() - state[f"{bn_key}.running_mean"].float() * scale
    return fused_weight.numpy(), fused_bias.numpy()


def c_array(name, values):
    flat = np.asarray(values, dtype=np.float32).reshape(-1)
    rows = []
    for start in range(0, len(flat), 8):
        rows.append("    " + ", ".join(f"{float(v):.9g}f" for v in flat[start:start + 8]))
    return f"static const float {name}[{len(flat)}] = {{\n" + ",\n".join(rows) + "\n};\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model", type=Path)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()

    train = load_training_module(Path(__file__).with_name("train_tinycnn.py"))
    saved = torch.load(args.model, map_location="cpu", weights_only=True)
    model = train.TinyGestureNet(saved["variant"])
    model.load_state_dict(saved["state_dict"])
    model.eval()
    state = model.state_dict()

    arrays = {}
    arrays["tg_conv0_w"], arrays["tg_conv0_b"] = fuse(state, "features.0", "features.1")
    for number, feature_index in enumerate((3, 4, 5), 1):
        prefix = f"features.{feature_index}.net"
        arrays[f"tg_dw{number}_w"], arrays[f"tg_dw{number}_b"] = fuse(state, f"{prefix}.0", f"{prefix}.1")
        arrays[f"tg_pw{number}_w"], arrays[f"tg_pw{number}_b"] = fuse(state, f"{prefix}.3", f"{prefix}.4")
    arrays["tg_fc_w"] = state["classifier.weight"].float().numpy()
    arrays["tg_fc_b"] = state["classifier.bias"].float().numpy()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    header = ["#ifndef TINY_GESTURE_WEIGHTS_H", "#define TINY_GESTURE_WEIGHTS_H", ""]
    header.extend(c_array(name, value) for name, value in arrays.items())
    header.append("#endif\n")
    (args.output_dir / "tiny_gesture_weights.h").write_text("\n".join(header), encoding="ascii")

    data = np.load(args.dataset)
    images, labels = data["images"], data["labels"]
    indices = [int(np.flatnonzero(labels == label)[0]) for label in range(len(saved["labels"]))]
    selected = torch.from_numpy(images[indices].copy()).permute(0, 3, 1, 2).float() / 255.0
    with torch.no_grad():
        expected = model(selected).numpy()
    if saved["variant"] == "foreground":
        inputs = selected[:, 3:4].numpy().astype(np.float32)
    else:
        gray = 0.114 * selected[:, 0:1] + 0.587 * selected[:, 1:2] + 0.299 * selected[:, 2:3]
        inputs = torch.cat((gray, selected[:, 3:4]), 1).numpy().astype(np.float32)
    inputs.tofile(args.output_dir / "test_vectors.bin")
    test = {
        "labels": saved["labels"],
        "truth": labels[indices].tolist(),
        "expected_logits": expected.tolist(),
        "expected_predictions": expected.argmax(1).tolist(),
    }
    (args.output_dir / "test_expected.json").write_text(json.dumps(test, indent=2), encoding="utf-8")
    print(f"exported {sum(v.size for v in arrays.values())} floats to {args.output_dir}")


if __name__ == "__main__":
    main()
