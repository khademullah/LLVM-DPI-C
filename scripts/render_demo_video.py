#!/usr/bin/env python3
"""Render docs/portwalk-demo.mp4, a silent walkthrough of the tape."""

import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 720
FPS = 15
OUT = Path(__file__).resolve().parents[1] / "docs" / "portwalk-demo.mp4"

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONTB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

INK = "#18181b"
MUTED = "#3f3f46"
BG = "#f6f5f3"
HEAD_FILL, HEAD_EDGE = "#dbeafe", "#1d4ed8"
READ_FILL, READ_EDGE = "#ffedd5", "#c2410c"
SLOT_FILL, SLOT_EDGE = "#ffffff", "#18181b"


def font(size, bold=False):
    return ImageFont.truetype(FONTB if bold else FONT, size)


def wrap(draw, text, face, width):
    lines, cur = [], ""
    for word in text.split():
        trial = word if not cur else cur + " " + word
        if draw.textlength(trial, font=face) <= width:
            cur = trial
        else:
            if cur:
                lines.append(cur)
            cur = word
    if cur:
        lines.append(cur)
    return lines


class Frame:
    def __init__(self, kicker, title):
        self.im = Image.new("RGB", (W, H), BG)
        self.d = ImageDraw.Draw(self.im)
        self.d.text((64, 36), kicker, font=font(18), fill=MUTED)
        self.d.text((64, 68), title, font=font(36, True), fill=INK)

    def caption(self, text):
        self.d.line((64, 560, W - 64, 560), fill="#e4e4e7", width=2)
        face = font(26)
        y = 584
        for line in wrap(self.d, text, face, W - 128):
            self.d.text((64, y), line, font=face, fill=INK)
            y += 36

    def counter(self, label, value):
        x, y, w, h = 980, 150, 236, 92
        self.d.rounded_rectangle((x, y, x + w, y + h), radius=8, fill="#fafafa", outline="#e4e4e7", width=2)
        self.d.text((x + 16, y + 12), label, font=font(16), fill=MUTED)
        self.d.text((x + 16, y + 40), str(value), font=font(32, True), fill=INK)

    def slot(self, x, y, label, kind="plain"):
        fill, edge = {
            "head": (HEAD_FILL, HEAD_EDGE),
            "read": (READ_FILL, READ_EDGE),
            "plain": (SLOT_FILL, SLOT_EDGE),
        }[kind]
        self.d.rounded_rectangle((x, y, x + 68, y + 68), radius=6, fill=fill, outline=edge, width=3)
        face = font(22, True)
        text = str(label)
        box = self.d.textbbox((0, 0), text, font=face)
        tw, th = box[2] - box[0], box[3] - box[1]
        self.d.text((x + (68 - tw) / 2, y + (68 - th) / 2 - 2), text, font=face, fill=INK)

    def marker(self, cx, top):
        self.d.polygon([(cx, top + 14), (cx - 9, top), (cx + 9, top)], fill=HEAD_EDGE)

    def hop(self, x, y, label, wide=70):
        face = font(16)
        box = self.d.textbbox((0, 0), label, font=face)
        tw = box[2] - box[0]
        self.d.text((x + (wide - tw) / 2, y), label, font=face, fill=MUTED)
        self.d.line((x + 8, y + 28, x + wide - 8, y + 28), fill=INK, width=3)

    def save(self, proc):
        proc.stdin.write(self.im.tobytes())


def emit(proc, frame, seconds):
    n = max(1, int(round(seconds * FPS)))
    raw = frame.im.tobytes()
    for _ in range(n):
        proc.stdin.write(raw)


def scene_intro(proc, extra=0.0):
    f = Frame("Start here", "Memory is a tape")
    y = 280
    for i, label in enumerate(["0", "1", "2", "3", "4", "5", "6", "7"]):
        kind = "head" if i == 0 else "plain"
        f.slot(80 + i * 96, y, label, kind)
    f.marker(80 + 34, y - 28)
    f.caption("The head can read only the slot it sits on. Blue is the head. Moving it to another slot costs shifts.")
    emit(proc, f, 5.0 + extra)


def scene_one_step(proc, extra=0.0):
    for step in range(0, 4):
        f = Frame("One move", "Each slot of travel is one shift")
        y = 300
        for i in range(8):
            kind = "head" if i == step else ("read" if i < step else "plain")
            f.slot(80 + i * 96, y, str(i), kind)
        f.marker(80 + step * 96 + 34, y - 28)
        f.counter("Shifts", step)
        f.caption("The head started on 0, so the first read is free. Each later slot adds 1 shift.")
        emit(proc, f, 1.1 + (extra if step == 3 else 0.0))


def scene_row(proc, extra=0.0):
    f = Frame("Row walk", "Next number, next slot")
    y = 300
    labels = ["0", "1", "2", "3", "…", "63"]
    kinds = ["head", "read", "read", "read", "plain", "read"]
    x = 80
    for i, (label, kind) in enumerate(zip(labels, kinds)):
        f.slot(x, y, label, kind)
        if i == 0:
            f.marker(x + 34, y - 28)
        if i < len(labels) - 1 and label != "…":
            f.hop(x + 76, y + 16, "+1", 52)
            x += 68 + 60
        elif label == "…":
            x += 96
        else:
            x += 80
    f.counter("Shifts", 63)
    f.caption("64 reads along a row. The head steps +1 each time. Total 63 shifts, because it was already on the first slot.")
    emit(proc, f, 6.0 + extra)


def scene_column(proc, extra=0.0):
    f = Frame("Column walk", "Same 64 reads, much farther apart")
    y = 300
    labels = ["0", "64", "128", "…"]
    kinds = ["head", "read", "read", "plain"]
    x = 80
    for i, (label, kind) in enumerate(zip(labels, kinds)):
        f.slot(x, y, label, kind)
        if i == 0:
            f.marker(x + 34, y - 28)
        if i < 2:
            f.hop(x + 80, y + 16, "+64", 110)
            x += 68 + 122
        else:
            x += 100
    f.counter("Shifts", 4032)
    f.caption("Stored row by row, a column is 64 slots away. 63 hops of 64 is 4032 shifts. The layout changed the cost, not the number of reads.")
    emit(proc, f, 7.0 + extra)


def scene_ports(proc, extra=0.0):
    f = Frame("Two read heads", "A second head can already be there")
    y = 300
    f.slot(160, y, "0", "head")
    f.marker(194, y - 28)
    f.hop(250, y + 16, "32 apart", 160)
    f.slot(430, y, "32", "head")
    f.marker(464, y - 28)
    f.d.rounded_rectangle((760, 250, 1180, 340), radius=8, fill="#fafafa", outline="#e4e4e7", width=2)
    f.d.text((780, 266), "1 port", font=font(18), fill=MUTED)
    f.d.text((780, 292), "32 shifts", font=font(28, True), fill=INK)
    f.d.rounded_rectangle((760, 360, 1180, 450), radius=8, fill="#fafafa", outline="#e4e4e7", width=2)
    f.d.text((780, 376), "2 ports", font=font(18), fill=MUTED)
    f.d.text((780, 402), "0 shifts", font=font(28, True), fill=INK)
    f.caption("Asking for slot 32 costs a walk of 32 with one head. With a second head already on 32, the cost is 0.")
    emit(proc, f, 6.5 + extra)


def sequence(proc, title, pairs, total_caption, extra=0.0):
    """pairs: list of (slot, shift_added). First shift_added is 0."""
    shown = []
    total = 0
    for i, (slot, added) in enumerate(pairs):
        total += added
        shown.append((slot, added if i else None))
        f = Frame("Gather", title)
        y = 300
        x = 64
        for j, (lab, hop) in enumerate(shown):
            if hop is not None:
                f.hop(x, y + 16, f"+{hop}", 64)
                x += 72
            kind = "head" if j == len(shown) - 1 else "read"
            f.slot(x, y, lab, kind)
            if j == len(shown) - 1:
                f.marker(x + 34, y - 28)
            x += 76
        f.counter("Shifts", total)
        f.caption(total_caption)
        emit(proc, f, 1.15)
    emit(proc, f, 2.2 + extra)


def scene_dpdk(proc, extra=0.0):
    f = Frame("Same idea as DPDK", "A burst of descriptors")
    f.d.text((64, 180), "rte_eth_tx_burst  /  rte_eth_rx_burst", font=font(28, True), fill=INK)
    lines = [
        "One burst is a short hardware ring. Each descriptor names a buffer address.",
        "Slots 0, 10, 1, 11, 2, 12 are six of those addresses.",
        "Inside the burst they do not depend on each other, so the device may pull",
        "them in the short-hop order: 12 shifts instead of 48.",
        "rte_ring between cores is a FIFO. It keeps order. The reorder is inside the burst.",
    ]
    y = 250
    face = font(24)
    for line in lines:
        f.d.text((64, y), line, font=face, fill=INK)
        y += 40
    f.caption("Buffers from one mempool sit next to each other, like the row walk. Scattered buffers are the column walk.")
    emit(proc, f, 8.0 + extra)


def scene_write(proc, extra=0.0):
    f = Frame("A write is different", "The read of that slot has to wait")
    y = 220
    f.d.text((64, y), "Source order", font=font(20), fill=MUTED)
    labels = [("W 5", "read"), ("R 1", "plain"), ("R 5", "read"), ("R 2", "plain")]
    for i, (lab, kind) in enumerate(labels):
        f.slot(64 + i * 120, y + 40, lab, kind)
    f.d.text((64, 400), "Allowed order", font=font(20), fill=MUTED)
    labels = [("R 1", "plain"), ("R 2", "plain"), ("W 5", "head"), ("R 5", "read")]
    for i, (lab, kind) in enumerate(labels):
        f.slot(64 + i * 120, 440, lab, kind)
    f.caption("Reads of other slots can go first. The read of slot 5 stays behind the write, or it would see the old value. A NIC keeps a write descriptor ahead of a later read of the same buffer.")
    emit(proc, f, 8.0 + extra)


def scene_llvm(proc, extra=0.0):
    f = Frame("Where LLVM comes in", "The compiler reads the loop. It does not run it.")
    f.d.rounded_rectangle((64, 200, 600, 340), radius=8, fill="#ffffff", outline="#e4e4e7", width=2)
    f.d.text((84, 220), "row_sum", font=font(18), fill=MUTED)
    f.d.text((84, 258), "step 4 bytes  =  1 slot", font=font(28, True), fill=INK)
    f.d.text((84, 300), "63 shifts", font=font(22), fill=INK)
    f.d.rounded_rectangle((660, 200, 1216, 340), radius=8, fill="#ffffff", outline="#e4e4e7", width=2)
    f.d.text((680, 220), "col_sum", font=font(18), fill=MUTED)
    f.d.text((680, 258), "step 256 bytes  =  64 slots", font=font(26, True), fill=INK)
    f.d.text((680, 300), "4032 shifts", font=font(22), fill=INK)
    f.d.text((64, 390), "vals[idx[i]] has no fixed step.", font=font(26), fill=INK)
    f.d.text((64, 434), "That gather is the DPDK burst: schedule it once the addresses are known.", font=font(26), fill=INK)
    f.caption("Clang turns the C into a list of loads and stores. A pass measures how far each address jumps per loop trip.")
    emit(proc, f, 8.0 + extra)


def scene_end(proc, extra=0.0):
    f = Frame("See it run", "make demo")
    f.d.text((64, 220), "The video, the C model, and the LLVM pass use these same counts.", font=font(26), fill=INK)
    rows = [("Row", "63 shifts"), ("Column", "4032 shifts"), ("Gather, program order", "48 shifts"), ("Gather, scheduled", "12 shifts")]
    y = 290
    for name, value in rows:
        f.d.text((64, y), name, font=font(24), fill=MUTED)
        f.d.text((520, y), value, font=font(24, True), fill=INK)
        y += 44
    f.caption("docs/guide.md walks the same picture, including the DPDK ring and the write rule.")
    emit(proc, f, 5.0 + extra)


NARRATION = [
    ("intro", "Start with the tape. The head can read only the slot it is sitting on. The blue box is the head. Every move to another slot costs shifts."),
    ("step", "The head started on slot zero, so the first read is free. Each later slot adds one shift."),
    ("row", "A row walk reads the next slot every time. Sixty four reads, stepping by one. The total is sixty three shifts, because the head was already on the first slot."),
    ("column", "A column of those same sixty four numbers sits sixty four slots apart. Sixty three hops of sixty four is four thousand and thirty two shifts. The number of reads stayed the same. The layout changed the cost."),
    ("ports", "A second head can already be sitting on slot thirty two. With one head, that seek costs thirty two shifts. With two heads, it costs zero."),
    ("program", "These six reads do not depend on each other. In program order the head follows the source. Zero, ten, one, eleven, two, twelve. The hops add up to forty eight shifts."),
    ("scheduled", "The same six reads, rescheduled. The head walks zero, one, two, ten, eleven, twelve. The total drops to twelve shifts."),
    ("dpdk", "This is the same idea as a D P D K burst. R T E eth T X burst, and R T E eth R X burst, post a short hardware ring of buffer addresses. Inside the burst, the device may pull them in the short hop order. The software R T E ring between cores stays in order. Buffers from one mempool sit next to each other, like the row walk. Scattered buffers are the column walk."),
    ("write", "A write is different. If you write slot five, a later read of slot five has to stay behind that write, or it sees the old value. Reads of other slots can still go first. A network card keeps a write descriptor ahead of a later read of the same buffer."),
    ("llvm", "L L V M does not run the program. Clang turns the C into a list of loads and stores, and a pass reads how far each address jumps per loop trip. Four bytes is one slot, sixty three shifts. Two hundred and fifty six bytes is sixty four slots, four thousand and thirty two shifts. When the address is the value at an index loaded from memory, there is no fixed step. That gather is the D P D K burst. Schedule it once the addresses are known."),
    ("end", "The video, the C model, and the L L V M pass use these same counts. The guide next to this file walks the picture in more detail."),
]

# Visual time before any pause added so the voice can finish.
BASE_SECONDS = {
    "intro": 5.0,
    "step": 4.4,
    "row": 6.0,
    "column": 7.0,
    "ports": 6.5,
    "program": 6 * 1.15 + 2.2,
    "scheduled": 6 * 1.15 + 2.2,
    "dpdk": 8.0,
    "write": 8.0,
    "llvm": 8.0,
    "end": 5.0,
}


def edge_tts_bin():
    for candidate in ("/tmp/tts-venv/bin/edge-tts", "edge-tts"):
        if Path(candidate).exists() or candidate == "edge-tts":
            from shutil import which
            if candidate == "edge-tts":
                found = which("edge-tts")
                if found:
                    return found
            elif Path(candidate).exists():
                return candidate
    sys.exit("edge-tts is not installed; the narration voice cannot be generated")


def media_duration(path):
    out = subprocess.check_output(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(path)],
        text=True,
    )
    return float(out.strip())


def prepare_voice(root):
    voice_dir = root / "build" / "narration"
    voice_dir.mkdir(parents=True, exist_ok=True)
    tts = edge_tts_bin()
    files = []
    durations = []
    for i, (name, text) in enumerate(NARRATION):
        mp3 = voice_dir / f"{i:02d}-{name}.mp3"
        if not mp3.exists() or mp3.stat().st_size < 1000:
            subprocess.check_call([
                tts, "--voice", "en-US-GuyNeural", "--rate=-8%",
                "--text", text, "--write-media", str(mp3),
            ])
        files.append(mp3)
        durations.append(media_duration(mp3))
    return files, durations


def render_picture(silent, extras):
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS),
        "-i", "-",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "23",
        str(silent),
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    try:
        scene_intro(proc, extras[0])
        scene_one_step(proc, extras[1])
        scene_row(proc, extras[2])
        scene_column(proc, extras[3])
        scene_ports(proc, extras[4])
        sequence(
            proc,
            "Program order bounces",
            [(0, 0), (10, 10), (1, 9), (11, 10), (2, 9), (12, 10)],
            "The head follows the source: 0, 10, 1, 11, 2, 12. The hops add up to 48 shifts.",
            extras[5],
        )
        sequence(
            proc,
            "Scheduled order takes short hops",
            [(0, 0), (1, 1), (2, 1), (10, 8), (11, 1), (12, 1)],
            "Same six reads. The head walks 0, 1, 2, 10, 11, 12. Total 12 shifts.",
            extras[6],
        )
        scene_dpdk(proc, extras[7])
        scene_write(proc, extras[8])
        scene_llvm(proc, extras[9])
        scene_end(proc, extras[10])
    finally:
        proc.stdin.close()
        code = proc.wait()
    if code != 0:
        sys.exit(code)


def mux(silent, files, spans):
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", str(silent)]
    parts = []
    labels = []
    for i, (mp3, span) in enumerate(zip(files, spans)):
        cmd += ["-i", str(mp3)]
        parts.append(f"[{i + 1}:a]adelay=250|250,apad=whole_dur={span:.3f}[a{i}]")
        labels.append(f"[a{i}]")
    parts.append("".join(labels) + f"concat=n={len(files)}:v=0:a=1[a]")
    cmd += [
        "-filter_complex", ";".join(parts),
        "-map", "0:v", "-map", "[a]",
        "-c:v", "copy", "-c:a", "aac", "-b:a", "160k",
        "-movflags", "+faststart",
        str(OUT),
    ]
    subprocess.check_call(cmd)


def main():
    root = OUT.parent.parent
    files, audio_durations = prepare_voice(root)
    spans = []
    extras = []
    for (name, _), audio in zip(NARRATION, audio_durations):
        span = max(BASE_SECONDS[name], audio + 0.7)
        spans.append(span)
        extras.append(span - BASE_SECONDS[name])
    silent = root / "build" / "portwalk-silent.mp4"
    silent.parent.mkdir(parents=True, exist_ok=True)
    render_picture(silent, extras)
    mux(silent, files, spans)
    print(OUT)


if __name__ == "__main__":
    main()
