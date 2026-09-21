"""Live tail of Winamplify / Winamp vis logs."""
import os
import time
from pathlib import Path

LOGS = [
    Path(os.environ.get("APPDATA", "")) / "Winamp" / "Plugins" / "spotify" / "vis.log",
    Path(os.environ.get("APPDATA", "")) / "Winamp" / "winamp.log",
]


def color(line: str) -> str:
    low = line.lower()
    if "peak=0" in low or "vis off" in low or "crash" in low or "violation" in low:
        return "\033[91m" + line + "\033[0m"
    if "play " in low or "playfile" in low or "in: loaded" in low or "thread start" in low:
        return "\033[92m" + line + "\033[0m"
    if "peak=" in low:
        return "\033[93m" + line + "\033[0m"
    return line


def main() -> None:
    print("Winamplify vis log watcher — Ctrl+C to quit")
    for p in LOGS:
        print(" ", p, "OK" if p.exists() else "(not created yet)")
    print("-" * 60)
    pos = {p: (p.stat().st_size if p.exists() else 0) for p in LOGS}
    try:
        while True:
            for p in LOGS:
                if not p.exists():
                    continue
                size = p.stat().st_size
                start = pos.get(p, 0)
                if size < start:
                    start = 0
                    print(f"\n--- reset {p.name} ---\n")
                if size > start:
                    with p.open("r", encoding="utf-8", errors="replace") as f:
                        f.seek(start)
                        chunk = f.read()
                    pos[p] = start + len(chunk.encode("utf-8", errors="replace"))
                    # size is bytes; seek by previous pos in bytes
                    pos[p] = size
                    for line in chunk.splitlines():
                        if line.strip():
                            tag = "vis" if p.name == "vis.log" else "wa"
                            print(f"[{tag}] {color(line)}")
            time.sleep(0.25)
    except KeyboardInterrupt:
        print("\nstop")


if __name__ == "__main__":
    main()
