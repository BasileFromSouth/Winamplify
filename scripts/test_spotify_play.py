"""Play a known search hit via the same tokens the plugin uses. Never prints secrets."""
import json
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path.home() / "AppData/Roaming/Winamp/Plugins/spotify"
URI = "spotify:track:0COqiPhxzoWICwFCS4eZcp"


def ini(path: Path) -> dict:
    out = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def req(method, url, token=None, data=None, form=False):
    headers = {"Accept": "application/json"}
    body = None
    if data is not None:
        if form:
            body = urllib.parse.urlencode(data).encode()
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        else:
            body = json.dumps(data).encode()
            headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    r = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(r, timeout=20) as resp:
            raw = resp.read()
            return resp.status, raw.decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")


def main():
    cfg = ini(ROOT / "config.ini")
    tok = ini(ROOT / "tokens.ini")
    client = cfg.get("client_id", "")
    refresh = tok.get("refresh", "")
    if not client or not refresh:
        print("missing client_id or refresh token")
        return
    st, body = req(
        "POST",
        "https://accounts.spotify.com/api/token",
        data={"grant_type": "refresh_token", "refresh_token": refresh, "client_id": client},
        form=True,
    )
    print("refresh", st)
    if st != 200:
        print("refresh body", body[:300])
        return
    access = json.loads(body)["access_token"]
    st, body = req("GET", "https://api.spotify.com/v1/me/player/devices", token=access)
    print("devices", st)
    devices = json.loads(body).get("devices", []) if body else []
    pick = ""
    chrome = ""
    for d in devices:
        print(
            f"  - {d.get('name')} type={d.get('type')} active={d.get('is_active')} vol={d.get('volume_percent')}"
        )
        if "Chrome" in (d.get("name") or ""):
            chrome = d.get("id") or ""
        if d.get("is_active"):
            pick = d.get("id") or ""
        elif not pick and d.get("type") == "Computer":
            pick = d.get("id") or ""
    if chrome:
        print("trying Chrome Web Player…")
        pick = chrome
    if pick:
        st, body = req(
            "PUT",
            "https://api.spotify.com/v1/me/player",
            token=access,
            data={"device_ids": [pick], "play": False},
        )
        print("transfer", st, (body[:180] if body else "empty"))
        time.sleep(0.6)
        q = urllib.parse.urlencode({"device_id": pick})
        st, body = req(
            "PUT",
            "https://api.spotify.com/v1/me/player/play?" + q,
            token=access,
            data={"uris": [URI], "position_ms": 0},
        )
        print("play+device", st, (body[:200] if body else "empty"))
    else:
        print("no device to target")
    time.sleep(1.5)
    st, body = req("GET", "https://api.spotify.com/v1/me/player", token=access)
    print("player", st)
    if st == 200 and body:
        j = json.loads(body)
        item = j.get("item") or {}
        dev = j.get("device") or {}
        print(
            "  playing=",
            j.get("is_playing"),
            "title=",
            item.get("name"),
            "device=",
            dev.get("name"),
            "repeat=",
            j.get("repeat_state"),
        )
        actions = ((j.get("actions") or {}).get("disallows")) or {}
        print("  disallows=", actions)
        print("  currently_playing_type=", j.get("currently_playing_type"))
    elif st == 204:
        print("  no active player")
    else:
        print("  body", body[:300])
    if pick:
        q = urllib.parse.urlencode({"uri": URI, "device_id": pick})
        st, body = req("POST", "https://api.spotify.com/v1/me/player/queue?" + q, token=access)
        print("queue", st, (body[:200] if body else "empty"))
        st, body = req(
            "PUT",
            "https://api.spotify.com/v1/me/player/play?" + urllib.parse.urlencode({"device_id": pick}),
            token=access,
            data={},
        )
        print("resume", st, (body[:200] if body else "empty"))
        time.sleep(1.2)
        st, body = req("GET", "https://api.spotify.com/v1/me/player", token=access)
        if st == 200 and body:
            j = json.loads(body)
            item = j.get("item") or {}
            print("after queue+resume playing=", j.get("is_playing"), "title=", item.get("name"))
        else:
            print("after queue+resume", st)


if __name__ == "__main__":
    main()
