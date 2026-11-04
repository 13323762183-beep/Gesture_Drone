"""AX7020 手势结果串口监视器 —— 高吞吐读取、实时统计、落 CSV。

对应板端两种输出：
  UART0（经 ESP32 -> USB）:   G:0 / G:1 / G:2 / G:3   （只在变化时发）+ 1 Hz HB
  UART1（调试串口）:          TINY_GESTURE G:2 logits=..,..,.. us=..  （逐帧）
                              TINY_CALIBRATING remaining=..

用法：
    python uart_gesture_monitor.py --list
    python uart_gesture_monitor.py COM7
    python uart_gesture_monitor.py COM7 --seconds 60 --csv run1.csv
    python uart_gesture_monitor.py COM7 --expect V,OPEN,FIST
    python uart_gesture_monitor.py --replay 调试记录.txt          # 无硬件复盘

设计要点：
  * 一次读整块 in_waiting 字节再切行，不做逐字符 readline，串口再快也不丢行；
  * CSV 每秒落盘一次，长时间录音不怕中途断电；
  * 心跳和重复手势只更新状态，不刷屏。
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import time
from collections import Counter
from datetime import datetime
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

NAMES = ("NONE", "V", "OPEN", "FIST")
NAME_TO_ID = {name: i for i, name in enumerate(NAMES)}

RE_GESTURE = re.compile(r"^G:([0-3])$")
RE_FRAME = re.compile(
    r"TINY_GESTURE\s+raw=(-?\d+)\s+stable=(-?\d+)\s+action=([A-Z]+)"
    r"\s+xy=(-?\d+),(-?\d+)\s+edge=(\d+)\s+logits=(-?\d+),(-?\d+),(-?\d+)"
)
RE_CALIB = re.compile(r"TINY_CALIBRATING")
RE_CRC = re.compile(r"\*[0-9A-Fa-f]{4}$")

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # 允许 --replay 在没有 pyserial 的机器上跑
    serial = None
    list_ports = None


def list_serial_ports() -> int:
    if list_ports is None:
        print("没有 pyserial：python -m pip install pyserial")
        return 1
    ports = list(list_ports.comports())
    if not ports:
        print("没有检测到串口设备")
        return 1
    for info in ports:
        print(f"{info.device:10s} {info.description}")
    return 0


class Stats:
    def __init__(self) -> None:
        self.counts = Counter()
        self.frames = Counter()
        self.heartbeats = 0
        self.calibrating = 0
        self.started = time.perf_counter()
        self.last_gesture_time: float | None = None
        self.last_gesture: int | None = None
        self.sequence: list[int] = []          # 连续去重后的手势序列
        self.unknown_lines = 0

    def note_gesture(self, value: int, t: float) -> bool:
        """返回值表示是否是一次“新”手势（用于打印）。"""
        self.counts[value] += 1
        changed = value != self.last_gesture
        self.last_gesture = value
        self.last_gesture_time = t
        if changed:
            self.sequence.append(value)
        return changed

    def summary(self, expected: list[int] | None) -> str:
        total = time.perf_counter() - self.started
        lines = ["", "=== 本次统计 ===",
                 f"时长 {total:.1f}s，心跳 {self.heartbeats} 个，未识别行 {self.unknown_lines} 条"]
        for i, name in enumerate(NAMES):
            lines.append(f"  G:{i} {name:<5} 事件 {self.counts[i]:4d} 次，逐帧 {self.frames[i]:6d} 帧")
        if self.sequence:
            shown = ", ".join(NAMES[v] for v in self.sequence[:40])
            more = "" if len(self.sequence) <= 40 else f" ...（共 {len(self.sequence)} 段）"
            lines.append(f"手势变化序列：{shown}{more}")
        if expected:
            # 板子上电会先发一次 G:0，比对时把期望序列之前的 NONE 丢掉
            seq = list(self.sequence)
            while seq and seq[0] == 0 and expected[0] != 0:
                seq.pop(0)
            got = seq[:len(expected)]
            hit = sum(1 for a, b in zip(expected, got) if a == b)
            lines.append(f"期望序列：{', '.join(NAMES[v] for v in expected)}")
            lines.append(f"按顺序命中：{hit}/{len(expected)}"
                         + ("  全部命中" if hit == len(expected) else "  未全部命中"))
        return "\n".join(lines)


def run(source, args, stats: Stats) -> None:
    csv_path = Path(args.csv) if args.csv else Path(
        f"gesture_log_{datetime.now():%Y%m%d_%H%M%S}.csv")
    csv_file = open(csv_path, "w", encoding="utf-8", newline="")
    writer = csv.writer(csv_file)
    writer.writerow(["t", "kind", "value", "raw"])
    csv_file.flush()

    port = None
    replay_lines = None
    if args.replay:
        replay_lines = Path(args.replay).read_text(encoding="utf-8", errors="replace").splitlines()
        print(f"回放 {args.replay}（{len(replay_lines)} 行）")
    else:
        if serial is None:
            print("没有 pyserial：python -m pip install pyserial")
            csv_file.close()
            return
        try:
            port = serial.Serial(source, args.baud, timeout=0.02)
        except Exception as exc:  # 端口被占用 / 名字写错 / 线没插
            print(f"打不开 {source}：{exc}")
            print("可用的串口：")
            for info in list_ports.comports():
                print(f"  {info.device:10s} {info.description}")
            csv_file.close()
            return
        print(f"已连接 {source} @ {args.baud}，Ctrl+C 结束。CSV -> {csv_path}")

    buffer = b""
    last_status = time.perf_counter()
    last_flush = last_status
    deadline = stats.started + args.seconds if args.seconds else None

    def handle(raw_line: str, t: float) -> None:
        line = raw_line.strip()
        if not line:
            return
        core = RE_CRC.sub("", line)

        match = RE_GESTURE.match(core)
        if match:
            value = int(match.group(1))
            new = stats.note_gesture(value, t)
            writer.writerow([f"{t:.3f}", "G", value, line])
            if new and not args.quiet:
                print(f"{t:8.2f}s  G:{value} {NAMES[value]}")
            return

        match = RE_FRAME.search(core)
        if match:
            raw = int(match.group(1))
            value = int(match.group(2))
            if value >= 0:
                stats.frames[value] += 1
                if not args.quiet:
                    action = match.group(3)
                    xy = ",".join(match.group(4, 5))
                    logits = ",".join(match.group(7, 8, 9))
                    print(
                        f"{t:8.2f}s  raw={NAMES[raw]:<5} stable={NAMES[value]:<5} "
                        f"action={action:<11} xy={xy:<5} logits={logits}"
                    )
            writer.writerow([f"{t:.3f}", "FRAME", value, line])
            return

        if core == "HB":
            stats.heartbeats += 1
            writer.writerow([f"{t:.3f}", "HB", "", ""])
            return

        if RE_CALIB.search(core):
            stats.calibrating += 1
            writer.writerow([f"{t:.3f}", "CAL", core, line])
            if not args.quiet:
                print(f"{t:8.2f}s  {core}")
            return

        if core.startswith(("GC", "GP", "CMD")) or "HELLO" in core:
            writer.writerow([f"{t:.3f}", "INFO", core, line])
            return

        stats.unknown_lines += 1
        writer.writerow([f"{t:.3f}", "RAW", core, line])
        if not args.quiet:
            print(f"{t:8.2f}s  [raw] {line}")

    try:
        if replay_lines is not None:
            for index, line in enumerate(replay_lines):
                if args.seconds and index / 200.0 > args.seconds:
                    break
                handle(line, time.perf_counter() - stats.started)
                if index % 200 == 0:
                    time.sleep(0.02)
        else:
            while True:
                chunk = port.read(port.in_waiting or 1)
                now = time.perf_counter()
                if chunk:
                    buffer += chunk
                    while b"\n" in buffer:
                        raw, buffer = buffer.split(b"\n", 1)
                        handle(raw.decode("utf-8", "replace"), now - stats.started)
                if now - last_flush >= 1.0:
                    csv_file.flush()
                    last_flush = now
                if not args.quiet and now - last_status >= 5.0:
                    last_status = now
                    elapsed = now - stats.started
                    recent = " ".join(f"{NAMES[i]}={stats.counts[i]}" for i in range(4))
                    if stats.last_gesture_time is None:
                        print(f"--- {elapsed:6.1f}s  {recent}  HB={stats.heartbeats}  还没有手势")
                    else:
                        age = now - stats.last_gesture_time
                        print(f"--- {elapsed:6.1f}s  {recent}  HB={stats.heartbeats}  "
                              f"最后手势 {age:.1f}s 前")
                if deadline and now >= deadline:
                    break
    except KeyboardInterrupt:
        pass
    finally:
        if port is not None:
            port.close()
        csv_file.flush()
        csv_file.close()

    print(stats.summary(args.expect))
    print(f"\nCSV 已保存：{csv_path}")


def parse_expect(text: str | None) -> list[int] | None:
    if not text:
        return None
    out = []
    for part in text.replace(" ", "").split(","):
        if not part:
            continue
        if part.isdigit() and 0 <= int(part) <= 3:
            out.append(int(part))
        elif part.upper() in NAME_TO_ID:
            out.append(NAME_TO_ID[part.upper()])
        else:
            raise SystemExit(f"看不懂的期望值：{part}（用 0-3 或 NONE/V/OPEN/FIST）")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="AX7020 手势串口监视器")
    ap.add_argument("port", nargs="?", help="串口名，例如 COM7")
    ap.add_argument("--list", action="store_true", help="列出串口")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=0, help="录多少秒，0 表示到 Ctrl+C")
    ap.add_argument("--csv", default=None, help="CSV 输出路径")
    ap.add_argument("--expect", default=None, help="期望手势序列，如 V,OPEN,FIST")
    ap.add_argument("--quiet", action="store_true", help="不打印逐帧细节")
    ap.add_argument("--replay", default=None, help="从文本文件回放（不需要硬件）")
    args = ap.parse_args()
    args.expect = parse_expect(args.expect)

    if args.list:
        return list_serial_ports()
    if not args.port and not args.replay:
        list_serial_ports()
        print("\n用法：python uart_gesture_monitor.py COM7")
        return 1

    run(args.port, args, Stats())
    return 0


if __name__ == "__main__":
    sys.exit(main())
