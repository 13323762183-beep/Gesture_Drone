import argparse
import hashlib
import json
from pathlib import Path

import cv2
import numpy as np


LABELS = ("V", "OPEN", "FIST")
ALIASES = {
    "v": 0, "peace": 0, "scissors": 0,
    "open": 1, "palm": 1, "paper": 1,
    "fist": 2, "rock": 2,
}
EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp", ".webp"}


def largest_center_component(mask):
    count, labels, stats, centers = cv2.connectedComponentsWithStats(mask, 8)
    if count <= 1:
        return mask
    h, w = mask.shape
    choices = []
    for index in range(1, count):
        area = stats[index, cv2.CC_STAT_AREA]
        x, y = centers[index]
        distance = ((x - w / 2) / w) ** 2 + ((y - h / 2) / h) ** 2
        choices.append((area / (1 + 3 * distance), index))
    selected = max(choices)[1]
    return np.where(labels == selected, 255, 0).astype(np.uint8)


def crop_hand(image, bbox):
    height, width = image.shape[:2]
    x, y, box_w, box_h = bbox
    x, y, box_w, box_h = x * width, y * height, box_w * width, box_h * height
    center_x, center_y = x + box_w / 2, y + box_h / 2
    side = max(box_w, box_h) * 1.7
    x1 = max(0, int(round(center_x - side / 2)))
    y1 = max(0, int(round(center_y - side / 2)))
    x2 = min(width, int(round(center_x + side / 2)))
    y2 = min(height, int(round(center_y + side / 2)))
    if x2 - x1 < 8 or y2 - y1 < 8:
        raise ValueError("invalid hand bounding box")

    crop = image[y1:y2, x1:x2]
    grow_x, grow_y = box_w * 0.06, box_h * 0.06
    rect_x1 = max(0, x - grow_x - x1)
    rect_y1 = max(0, y - grow_y - y1)
    rect_x2 = min(crop.shape[1], x + box_w + grow_x - x1)
    rect_y2 = min(crop.shape[0], y + box_h + grow_y - y1)
    return crop, (rect_x1, rect_y1, rect_x2 - rect_x1, rect_y2 - rect_y1)


def make_mask(crop, hand_rect):
    scale = min(1.0, 320.0 / max(crop.shape[:2]))
    small = cv2.resize(crop, None, fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
    x, y, width, height = (int(round(value * scale)) for value in hand_rect)
    x, y = max(0, x), max(0, y)
    rect = (x, y, min(small.shape[1] - x, max(2, width)),
            min(small.shape[0] - y, max(2, height)))
    if rect[2] < 2 or rect[3] < 2:
        raise ValueError("invalid scaled hand bounding box")

    seed = np.zeros(small.shape[:2], np.uint8)
    bg = np.zeros((1, 65), np.float64)
    fg = np.zeros((1, 65), np.float64)
    cv2.grabCut(small, seed, rect, bg, fg, 3, cv2.GC_INIT_WITH_RECT)
    mask = np.where((seed == cv2.GC_FGD) | (seed == cv2.GC_PR_FGD), 255, 0).astype(np.uint8)
    mask = largest_center_component(mask)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    mask = cv2.resize(mask, (96, 72), interpolation=cv2.INTER_AREA)
    mask = np.where(mask >= 96, 255, 0).astype(np.uint8)
    return mask, float(np.count_nonzero(mask) / mask.size)


def class_files(root, maximum):
    found = {label: [] for label in range(len(LABELS))}
    for directory in root.iterdir():
        if directory.is_dir() and directory.name.lower() in ALIASES:
            label = ALIASES[directory.name.lower()]
            files = sorted(path for path in directory.rglob("*") if path.suffix.lower() in EXTENSIONS)
            found[label].extend(files[:maximum])
    return found


def matching_bbox(annotations, path, target):
    image_id = path.stem.removesuffix("-checkpoint")
    entry = next((items[image_id] for items in annotations.values() if image_id in items), None)
    if entry is None:
        raise KeyError(image_id)
    candidates = [bbox for label, bbox in zip(entry["labels"], entry["bboxes"])
                  if ALIASES.get(label.lower()) == target]
    if not candidates:
        raise KeyError(f"{image_id}: target label not found")
    return max(candidates, key=lambda box: box[2] * box[3])


def review_sheet(samples, output):
    rows = []
    for label in range(len(LABELS)):
        cells = []
        for crop, mask in samples[label][:6]:
            left = cv2.resize(crop, (160, 120), interpolation=cv2.INTER_AREA)
            right = cv2.cvtColor(cv2.resize(mask, (160, 120),
                                            interpolation=cv2.INTER_NEAREST), cv2.COLOR_GRAY2BGR)
            cells.append(np.hstack((left, right)))
        while len(cells) < 6:
            cells.append(np.zeros((120, 320, 3), np.uint8))
        row = np.hstack(cells)
        cv2.putText(row, LABELS[label], (8, 25), cv2.FONT_HERSHEY_SIMPLEX,
                    0.75, (0, 0, 255), 2, cv2.LINE_AA)
        rows.append(row)
    output.parent.mkdir(parents=True, exist_ok=True)
    ok, encoded = cv2.imencode(".png", np.vstack(rows))
    if not ok:
        raise RuntimeError("failed to encode review image")
    encoded.tofile(output)


def self_test():
    image = np.full((180, 240, 3), 230, np.uint8)
    cv2.circle(image, (120, 90), 35, (40, 80, 130), -1)
    crop, rect = crop_hand(image, [85 / 240, 55 / 180, 70 / 240, 70 / 180])
    mask, ratio = make_mask(crop, rect)
    assert crop.size and mask.shape == (72, 96) and mask.dtype == np.uint8
    assert 0.02 < ratio < 0.75
    print("PASS crop and mask self-test")


def main():
    parser = argparse.ArgumentParser(description="Crop HaGRID hands and build board-like masks")
    parser.add_argument("input_dir", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--review", type=Path, default=Path("mask_review.png"))
    parser.add_argument("--max-per-class", type=int, default=800)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.input_dir is None or args.output is None:
        parser.error("input_dir and output are required")

    annotation_path = args.input_dir / "minimal-annotations.json"
    if not annotation_path.exists():
        raise SystemExit(f"missing annotation file: {annotation_path}")
    annotations = json.loads(annotation_path.read_text(encoding="utf-8"))
    files = class_files(args.input_dir, args.max_per_class)
    missing = [LABELS[label] for label, paths in files.items() if not paths]
    if missing:
        raise SystemExit("missing or empty class folders: " + ", ".join(missing))

    images, labels, groups, frames = [], [], [], []
    review = {label: [] for label in range(len(LABELS))}
    rejected = {label: 0 for label in range(len(LABELS))}
    for label, paths in files.items():
        for number, path in enumerate(paths):
            if number % 25 == 0:
                print(f"cropping {LABELS[label]} {number + 1}/{len(paths)}", flush=True)
            image = cv2.imread(str(path))
            try:
                crop, rect = crop_hand(image, matching_bbox(annotations, path, label))
                mask, ratio = make_mask(crop, rect)
            except (KeyError, ValueError, cv2.error, TypeError):
                rejected[label] += 1
                continue
            if not 0.02 <= ratio <= 0.75:
                rejected[label] += 1
                continue
            color = cv2.resize(crop, (96, 72), interpolation=cv2.INTER_AREA)
            color[mask == 0] = 0
            images.append(np.dstack((color, mask)))
            labels.append(label)
            digest = hashlib.sha1(str(path.relative_to(args.input_dir)).encode("utf-8")).digest()
            groups.append(digest[0] % 5)
            frames.append(number)
            if len(review[label]) < 6:
                review[label].append((crop, mask))

    x = np.asarray(images, np.uint8)
    y = np.asarray(labels, np.int64)
    g = np.asarray(groups, np.int64)
    f = np.asarray(frames, np.int64)
    assert x.ndim == 4 and x.shape[1:] == (72, 96, 4)
    assert set(y.tolist()) == {0, 1, 2}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    np.savez_compressed(args.output, images=x, labels=y, groups=g, frames=f)
    review_sheet(review, args.review)
    counts = {LABELS[i]: int(np.count_nonzero(y == i)) for i in range(len(LABELS))}
    print(f"saved={args.output} samples={len(y)} counts={counts} rejected={rejected}")
    print(f"CHECK THIS BEFORE TRAINING: {args.review}")


if __name__ == "__main__":
    main()
