import argparse
import json
from pathlib import Path

import cv2
import numpy as np


LABELS = ("V", "OPEN", "FIST")


def board_samples(root):
    images, labels, groups = [], [], []
    burst_number = 0
    for label, name in enumerate(LABELS):
        previous_time = None
        for mask_path in sorted((root / name).glob("*_mask.png")):
            image_path = mask_path.with_name(mask_path.name.replace("_mask.png", "_image.png"))
            image = cv2.imread(str(image_path), cv2.IMREAD_COLOR)
            mask = cv2.imread(str(mask_path), cv2.IMREAD_GRAYSCALE)
            if image is None or mask is None:
                continue
            if image.shape[:2] != (72, 96):
                image = cv2.resize(image, (96, 72), interpolation=cv2.INTER_AREA)
            if mask.shape != (72, 96):
                mask = cv2.resize(mask, (96, 72), interpolation=cv2.INTER_NEAREST)
            mask = np.where(mask >= 128, 255, 0).astype(np.uint8)

            stamp = mask_path.stat().st_mtime
            if previous_time is None or stamp - previous_time > 0.5:
                burst_number += 1
            previous_time = stamp
            images.append(np.dstack((image, mask)))
            labels.append(label)
            groups.append(burst_number % 5)
    return (np.asarray(images, np.uint8), np.asarray(labels, np.int64),
            np.asarray(groups, np.int64))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("public_dataset", type=Path)
    parser.add_argument("board_root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    public = np.load(args.public_dataset)
    board_x, board_y, board_g = board_samples(args.board_root)
    assert board_x.shape[1:] == (72, 96, 4)
    assert set(board_y.tolist()) == {0, 1, 2}

    images = np.concatenate((public["images"], board_x))
    labels = np.concatenate((public["labels"], board_y))
    groups = np.concatenate((public["groups"], board_g))
    frames = np.arange(len(labels), dtype=np.int64)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.output, images=images, labels=labels,
                        groups=groups, frames=frames)

    report = {
        "labels": LABELS,
        "public": np.bincount(public["labels"], minlength=3).tolist(),
        "board": np.bincount(board_y, minlength=3).tolist(),
        "total": np.bincount(labels, minlength=3).tolist(),
        "samples": int(len(labels)),
    }
    args.output.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
