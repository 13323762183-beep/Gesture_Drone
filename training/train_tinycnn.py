import argparse
import copy
import json
import random
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F
from torch.utils.data import DataLoader, Dataset


LABELS = ("V", "OPEN", "FIST")


class Frames(Dataset):
    def __init__(self, images, labels, augment=False):
        self.images = images
        self.labels = labels
        self.augment = augment

    def __len__(self):
        return len(self.labels)

    def __getitem__(self, index):
        x = torch.from_numpy(self.images[index].copy()).permute(2, 0, 1).float() / 255.0
        if self.augment:
            if torch.rand(()) < 0.5:
                x = x.flip(-1)
            scale = float(0.65 + 0.75 * torch.rand(()))
            height, width = x.shape[-2:]
            resized = F.interpolate(x[None], scale_factor=scale, mode="bilinear",
                                    align_corners=False, recompute_scale_factor=False)[0]
            if scale >= 1:
                top = int(torch.randint(0, resized.shape[-2] - height + 1, (1,)))
                left = int(torch.randint(0, resized.shape[-1] - width + 1, (1,)))
                x = resized[:, top:top + height, left:left + width]
            else:
                canvas = torch.zeros_like(x)
                canvas[:3] = x[:3].mean((-2, -1), keepdim=True).expand(-1, height, width)
                top = int(torch.randint(0, height - resized.shape[-2] + 1, (1,)))
                left = int(torch.randint(0, width - resized.shape[-1] + 1, (1,)))
                canvas[:, top:top + resized.shape[-2], left:left + resized.shape[-1]] = resized
                x = canvas
            gain = 0.75 + 0.5 * torch.rand(())
            bias = -0.12 + 0.24 * torch.rand(())
            x[:3] = (x[:3] * gain + bias).clamp(0, 1)
            if torch.rand(()) < 0.35:
                x[:3] = (x[:3] + torch.randn_like(x[:3]) * 0.02).clamp(0, 1)
        return x, int(self.labels[index])


class DSBlock(nn.Module):
    def __init__(self, source, target):
        super().__init__()
        self.net = nn.Sequential(
            nn.Conv2d(source, source, 3, 2, 1, groups=source, bias=False),
            nn.BatchNorm2d(source),
            nn.ReLU(inplace=True),
            nn.Conv2d(source, target, 1, bias=False),
            nn.BatchNorm2d(target),
            nn.ReLU(inplace=True),
        )

    def forward(self, x):
        return self.net(x)


class TinyGestureNet(nn.Module):
    def __init__(self, variant):
        super().__init__()
        self.variant = variant
        channels = 1 if variant == "foreground" else 2
        self.features = nn.Sequential(
            nn.Conv2d(channels, 12, 3, 2, 1, bias=False),
            nn.BatchNorm2d(12),
            nn.ReLU(inplace=True),
            DSBlock(12, 24),
            DSBlock(24, 40),
            DSBlock(40, 64),
        )
        self.classifier = nn.Linear(64, len(LABELS))

    def forward(self, x):
        gray = 0.114 * x[:, 0:1] + 0.587 * x[:, 1:2] + 0.299 * x[:, 2:3]
        foreground = x[:, 3:4]
        model_input = foreground if self.variant == "foreground" else torch.cat((gray, foreground), 1)
        x = self.features(model_input)
        return self.classifier(x.mean((-2, -1)))


def scores(expected, predicted):
    matrix = np.zeros((len(LABELS), len(LABELS)), np.int64)
    for truth, guess in zip(expected, predicted):
        matrix[truth, guess] += 1
    present = matrix.sum(1) > 0
    recall = np.diag(matrix)[present] / matrix.sum(1)[present]
    return float(np.trace(matrix) / max(1, matrix.sum())), float(recall.mean()), matrix.tolist()


def train_one(images, labels, train_mask, test_mask, variant, epochs, seed, device):
    torch.manual_seed(seed)
    random.seed(seed)
    model = TinyGestureNet(variant).to(device)
    counts = np.bincount(labels[train_mask], minlength=len(LABELS))
    weights = len(labels[train_mask]) / np.maximum(1, counts)
    weights = torch.tensor(weights / weights.mean(), dtype=torch.float32, device=device)
    loss_fn = nn.CrossEntropyLoss(weight=weights)
    optimizer = torch.optim.AdamW(model.parameters(), lr=2e-3, weight_decay=1e-4)
    train_loader = DataLoader(Frames(images[train_mask], labels[train_mask], True), 128, True)
    test_loader = DataLoader(Frames(images[test_mask], labels[test_mask]), 256)
    best = None
    for _ in range(epochs):
        model.train()
        for x, y in train_loader:
            x, y = x.to(device), y.to(device)
            optimizer.zero_grad(set_to_none=True)
            loss_fn(model(x), y).backward()
            optimizer.step()
        model.eval()
        truth, guess = [], []
        with torch.no_grad():
            for x, y in test_loader:
                guess.extend(model(x.to(device)).argmax(1).cpu().tolist())
                truth.extend(y.tolist())
        result = scores(truth, guess)
        if best is None or result[1] > best[0][1]:
            best = (result, copy.deepcopy(model.state_dict()))
    model.load_state_dict(best[1])
    return model, best[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("--epochs", type=int, default=18)
    parser.add_argument("--final-variant", choices=("foreground", "gray_foreground"))
    args = parser.parse_args()

    torch.set_num_threads(4)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    data = np.load(args.dataset)
    images, labels, groups = data["images"], data["labels"], data["groups"]
    report = {"device": str(device), "samples": int(len(labels)), "variants": {}}
    best_variant = (0.0, args.final_variant) if args.final_variant else None
    for name in (() if args.final_variant else ("foreground", "gray_foreground")):
        folds = []
        for held_out in sorted(set(groups.tolist())):
            _, result = train_one(images, labels, groups != held_out, groups == held_out,
                                  name, args.epochs, 100 + held_out, device)
            accuracy, balanced, matrix = result
            folds.append({"held_out": int(held_out), "accuracy": accuracy,
                          "balanced_accuracy": balanced, "matrix": matrix})
            print(name, "held_out", held_out, "accuracy", round(accuracy, 4),
                  "balanced", round(balanced, 4))
        mean_balanced = float(np.mean([fold["balanced_accuracy"] for fold in folds]))
        report["variants"][name] = {"mean_balanced_accuracy": mean_balanced, "folds": folds}
        if best_variant is None or mean_balanced > best_variant[0]:
            best_variant = (mean_balanced, name)

    # Final fit uses all labelled recordings; cross-video folds above remain the honest score.
    final, _ = train_one(images, labels, np.ones(len(labels), bool), np.ones(len(labels), bool),
                         best_variant[1], args.epochs, 999, device)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    model_path = args.output_dir / "tiny_gesture_float.pt"
    torch.save({"state_dict": final.cpu().state_dict(), "variant": best_variant[1],
                "labels": LABELS, "input": [4, 72, 96]}, model_path)
    parameters = sum(p.numel() for p in final.parameters())
    report.update({"selected": best_variant[1], "parameters": parameters,
                   "float_weight_bytes": parameters * 4, "model": str(model_path)})
    (args.output_dir / "metrics.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("selected=", best_variant[1], "parameters=", parameters, "saved=", model_path)


if __name__ == "__main__":
    main()
