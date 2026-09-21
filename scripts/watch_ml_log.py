"""Live tail of Winamplify Media Library play log."""
import os
import time
from pathlib import Path

LOG = Path(os.environ.get("APPDATA", "")) / "Winamp" / "Plugins" / "spotify" / "ml.log"


def main() -> None:
    print("ml.log watcher —", LOG, "exists" if LOG.exists() else "missing")
    pos = LOG.stat().st_size if LOG.exists() else 0
    if LOG.exists():
        with LOG.open("r", encoding="utf-8", errors="replace") as f:
            for line in f.read().splitlines()[-20:]:
                if line.strip():
                    print("[ml]", line)
    print("-" * 60)
    while True:
        if not LOG.exists():
            time.sleep(0.25)
            continue
        size = LOG.stat().st_size
        if size < pos:
            pos = 0
            print("--- reset ---")
        if size > pos:
            with LOG.open("r", encoding="utf-8", errors="replace") as f:
                f.seek(pos)
                chunk = f.read()
            pos = size
            for line in chunk.splitlines():
                if line.strip():
                    print("[ml]", line, flush=True)
        time.sleep(0.25)


if __name__ == "__main__":
    main()
