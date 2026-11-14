"""Satellite downlink tracker for the iRadio UV-98 Plus open firmware.

Shows passes of satellites heard in the last 24 hours (AMSAT status) over your location
(config.json), with their downlinks from SatNOGS, and tunes the radio to a downlink with
Doppler correction at the moment you click. Run it and open http://localhost:8766.
"""
import json
import os
import threading
import time
import urllib.request
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import sys

import numpy as np
from skyfield.api import EarthSatellite, load, wgs84

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import uv98  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(HERE, "cache")
KEPS = os.path.join(HERE, "keps.txt")          # optional: your own TLEs, used when they are newest
with open(os.path.join(HERE, "config.json")) as _f:
    CONFIG = json.load(_f)
SITE_NAME = CONFIG["name"]
LAT, LON, ELEV = float(CONFIG["lat"]), float(CONFIG["lon"]), float(CONFIG.get("elev_m", 0))
TZ = CONFIG.get("timezone", "UTC")
HOST, PORT = CONFIG.get("host", "127.0.0.1"), int(CONFIG.get("port", 8766))
SERIAL_PORT = CONFIG.get("serial_port") or None
WINDOW_MIN = 60
C_KM_S = 299792.458

SOURCES = {
    "transmitters": ("https://db.satnogs.org/api/transmitters/?format=json", 24 * 3600),
    "tle": ("https://db.satnogs.org/api/tle/?format=json", 6 * 3600),
    "satellites": ("https://db.satnogs.org/api/satellites/?format=json", 24 * 3600),
    "amsat": ("https://status.amsat.org/api/v1/summary.php", 3600),
    "celestrak": ("https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=tle", 2 * 3600),
    "amsat_tle": ("https://www.amsat.org/tle/dailytle.txt", 6 * 3600),
}
TEXT_SOURCES = {"celestrak": 100, "amsat_tle": 10}
PREDICT_MIN = 24 * 60


def tle_epoch(l1):
    yy = int(l1[18:20])
    return (2000 + yy if yy < 57 else 1900 + yy) * 1000 + float(l1[20:32])

ts = load.timescale(builtin=True)
site = wgs84.latlon(LAT, LON, elevation_m=ELEV)
state = {"passes": [], "computed": 0, "error": None, "fetched": {}, "attempts": {},
         "fetch_errors": {}}
sats = {}
state_lock = threading.Lock()
radio = {"dev": None, "error": None}
radio_lock = threading.Lock()


def parse_tle_text(text):
    lines = [l.rstrip() for l in text.splitlines() if l.strip()]
    out = {}
    for i in range(len(lines) - 2):
        a, b = lines[i + 1], lines[i + 2]
        if a.startswith("1 ") and b.startswith("2 ") and len(a) == 69 and len(b) == 69:
            out[int(a[2:7])] = (lines[i].strip(), a, b)
    return out


def fetch(name):
    url, max_age = SOURCES[name]
    as_json = name not in TEXT_SOURCES
    path = os.path.join(CACHE, name + (".json" if as_json else ".tle"))
    stale = not os.path.exists(path) or time.time() - os.path.getmtime(path) > max_age
    attempts_path = os.path.join(CACHE, "attempts.json")
    if not state["attempts"] and os.path.exists(attempts_path):
        with open(attempts_path) as f:
            state["attempts"] = json.load(f)
    if stale and time.time() - state["attempts"].get(name, 0) > max_age:
        state["attempts"][name] = time.time()
        os.makedirs(CACHE, exist_ok=True)
        with open(attempts_path, "w") as f:
            json.dump(state["attempts"], f)
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "uv98-open-firmware-tracker"})
            data = urllib.request.urlopen(req, timeout=90).read()
            if as_json:
                json.loads(data)
            elif len(parse_tle_text(data.decode("ascii", "replace"))) < TEXT_SOURCES[name]:
                raise ValueError(data.decode("ascii", "replace")[:200].strip())
            os.makedirs(CACHE, exist_ok=True)
            with open(path + ".part", "wb") as f:
                f.write(data)
            os.replace(path + ".part", path)
        except Exception as e:
            state["fetch_errors"][name] = str(e)
    if not os.path.exists(path):
        raise RuntimeError(f"{name}: {state['fetch_errors'].get(name, 'not downloaded yet')}")
    state["fetch_errors"].pop(name, None)
    state["fetched"][name] = os.path.getmtime(path)
    with open(path) as f:
        return json.load(f) if as_json else parse_tle_text(f.read())


def norm(s):
    return "".join(ch for ch in s.upper() if ch.isalnum())


def rig_target(t):
    """Frequency and mode the iRadio would use for this downlink, or None if out of range."""
    lo, hi = t.get("downlink_low"), t.get("downlink_high")
    if not lo:
        return None
    f = int(round(lo if not hi or hi == lo else (lo + hi) / 2))
    if not uv98.can_tune(f):
        return None
    return {"freq_hz": f, "mode": uv98.pick_mode(t.get("mode"))}


POSITIVE = ("Heard", "Telemetry Only", "Crew Active")


def build_sats():
    tx = fetch("transmitters")
    tle = {r["sat_id"]: r for r in fetch("tle")}
    satinfo = fetch("satellites")
    info = {s["sat_id"]: s for s in satinfo}
    amsat = fetch("amsat").get("data", [])
    extra = []
    for src, label in (("celestrak", "CelesTrak"), ("amsat_tle", "AMSAT")):
        try:
            extra.append((label, fetch(src)))
        except Exception:
            pass
    if os.path.exists(KEPS):
        with open(KEPS) as f:
            extra.append(("your keps.txt", parse_tle_text(f.read())))
    heard = {}
    for row in amsat:
        if row.get("report") in POSITIVE:
            base, _, tag = (row.get("name") or "").partition("_")
            h = heard.setdefault(base, {"count": 0, "modes": set()})
            h["count"] += int(row.get("report_count") or 0)
            if tag:
                h["modes"].add(tag.strip("[]").replace("_", " "))
    alias_index = {}
    for s in satinfo:
        names = [s.get("name") or ""] + (s.get("names") or "").split(",")
        for n in names:
            if n.strip():
                alias_index.setdefault(norm(n), []).append(s["sat_id"])
    chosen, unmatched = {}, []
    for base, h in sorted(heard.items()):
        cands = [sid for sid in alias_index.get(norm(base), []) if sid in tle]
        if len(cands) != 1:
            unmatched.append(base)
            continue
        c = chosen.setdefault(cands[0], {"names": [], "count": 0, "modes": set()})
        c["names"].append(base)
        c["count"] += h["count"]
        c["modes"] |= h["modes"]
    state["unmatched"] = unmatched
    by_sat = {}
    for t in tx:
        if t["sat_id"] in chosen and t.get("status") == "active":
            by_sat.setdefault(t["sat_id"], []).append(t)
    out = {}
    for sid, c in chosen.items():
        r = tle[sid]
        meta = info.get(sid, {})
        aliases = [a.strip() for a in (meta.get("names") or "").split(",") if a.strip()]
        name = meta.get("name") or r["tle0"].lstrip("0 ").strip()
        display = " / ".join(c["names"])
        h = c["count"]
        txs = []
        for t in sorted(by_sat.get(sid, []), key=lambda t: t.get("downlink_low") or 0):
            txs.append({
                "description": t.get("description"), "type": t.get("type"),
                "mode": t.get("mode"), "baud": t.get("baud"), "service": t.get("service"),
                "downlink_low": t.get("downlink_low"), "downlink_high": t.get("downlink_high"),
                "uplink_low": t.get("uplink_low"), "uplink_high": t.get("uplink_high"),
                "uplink_mode": t.get("uplink_mode"), "invert": t.get("invert"),
                "rig": rig_target(t),
            })
        norad = int(r["tle1"][2:7])
        sets = [(r["tle1"], r["tle2"], f"SatNOGS ({r.get('tle_source')})")]
        for label, d in extra:
            if norad in d:
                sets.append((d[norad][1], d[norad][2], label))
        l1, l2, source = max(reversed(sets), key=lambda x: tle_epoch(x[0]))
        others = sorted({x[2] for x in sets} - {source})
        try:
            sat = EarthSatellite(l1, l2, name, ts)
        except Exception:
            continue
        out[sid] = {
            "sat": sat, "sat_id": sid, "norad": meta.get("norad_cat_id") or r["norad_cat_id"],
            "name": name, "display": display, "aliases": aliases, "heard": h,
            "heard_modes": sorted(c["modes"]),
            "tle_epoch": sat.epoch.utc_datetime().isoformat(), "tle_source": source,
            "tle_also": others,
            "tle_age_days": round(ts.now() - sat.epoch, 1), "transmitters": txs,
        }
    return out


def ms(t):
    return int(t.utc_datetime().timestamp() * 1000)


def look(sat, t):
    topo = (sat - site).at(t)
    alt, az, dist = topo.altaz()
    rr = topo.frame_latlon_and_rates(site)[5].km_per_s
    return alt.degrees, az.degrees, dist.km, rr


def compute_passes():
    now = ts.now()
    t0 = ts.tt_jd(now.tt - 30 / 1440)
    t1 = ts.tt_jd(now.tt + (PREDICT_MIN + 45) / 1440)
    limit = now.tt + PREDICT_MIN / 1440
    passes = []
    for sid, s in sats.items():
        try:
            times, events = s["sat"].find_events(site, t0, t1, altitude_degrees=0)
        except Exception:
            continue
        cur = None
        found = []
        for t, e in zip(times, events):
            if e == 0:
                cur = {"aos": t}
            else:
                cur = cur or {"aos": None}
                if e == 1 and ("tca" not in cur or look(s["sat"], t)[0] > cur["max_el"]):
                    cur["tca"], cur["max_el"] = t, look(s["sat"], t)[0]
                if e == 2:
                    cur["los"] = t
                    found.append(cur)
                    cur = None
        if cur:
            found.append(cur)
        if not found and look(s["sat"], now)[0] > 0:
            found.append({"aos": None})
        for p in found:
            aos, los = p.get("aos"), p.get("los")
            if aos is not None and aos.tt > limit:
                continue
            if los is not None and los.tt < now.tt:
                continue
            a = aos if aos is not None else t0
            b = los if los is not None else t1
            n = max(2, min(120, int((b.tt - a.tt) * 1440 * 2) + 1))
            alt, az, _ = (s["sat"] - site).at(ts.tt_jd(np.linspace(a.tt, b.tt, n))).altaz()
            track = [[round(float(z), 1), round(max(float(e), 0.0), 1)]
                     for z, e in zip(az.degrees, alt.degrees)]
            rec = {"sat_id": sid, "aos": ms(aos) if aos is not None else None,
                   "los": ms(los) if los is not None else None,
                   "tca": ms(p["tca"]) if "tca" in p else None,
                   "max_el": round(p["max_el"], 1) if "max_el" in p else None,
                   "track": track}
            if aos is not None:
                rec["aos_az"] = round(look(s["sat"], aos)[1])
            if los is not None:
                rec["los_az"] = round(look(s["sat"], los)[1])
            if "tca" in p:
                rec["tca_az"] = round(look(s["sat"], p["tca"])[1])
            passes.append(rec)
    return passes


def refresher():
    global sats
    last_build = 0
    while True:
        try:
            if time.time() - last_build > 3600 or not sats:
                built = build_sats()
                with state_lock:
                    sats = built
                last_build = time.time()
            p = compute_passes()
            with state_lock:
                state["passes"], state["computed"], state["error"] = p, time.time(), None
        except Exception as e:
            with state_lock:
                state["error"] = str(e)
        time.sleep(120)


def passes_payload():
    now = ts.now()
    now_ms = ms(now)
    out = []
    with state_lock:
        plist, computed, err = list(state["passes"]), state["computed"], state["error"]
        smap = sats
    for p in plist:
        if p["los"] is not None and p["los"] < now_ms:
            continue
        s = smap.get(p["sat_id"])
        if not s:
            continue
        el, az, dist, rr = look(s["sat"], now)
        rec = dict(p)
        rec.update({k: s[k] for k in ("sat_id", "norad", "name", "display", "aliases", "heard",
                                      "heard_modes", "tle_epoch", "tle_source", "tle_also",
                                      "tle_age_days", "transmitters")})
        rec["now"] = {"el": round(el, 1), "az": round(az, 1), "range_km": round(dist),
                      "range_rate": round(rr, 3)} if el > 0 else None
        out.append(rec)
    out.sort(key=lambda r: (r["now"] is None, r["aos"] or 0))
    return {"site": {"name": SITE_NAME, "lat": LAT, "lon": LON, "elev": ELEV, "tz": TZ},
            "now": now_ms, "computed": int(computed * 1000), "window_min": WINDOW_MIN,
            "predict_min": PREDICT_MIN,
            "error": err, "fetched": {k: int(v * 1000) for k, v in state["fetched"].items()},
            "heard_count": len(smap), "unmatched": state.get("unmatched", []),
            "fetch_errors": dict(state["fetch_errors"]),
            "passes": out}


def get_radio(connect=False):
    with radio_lock:
        dev = radio["dev"]
        if dev and dev.alive:
            return dev
        if dev:
            dev.close()
            radio["dev"] = None
        if not connect:
            raise uv98.RadioError(radio["error"] or "not connected")
        try:
            radio["dev"] = uv98.UV98(SERIAL_PORT)
            radio["error"] = None
            return radio["dev"]
        except Exception as e:
            radio["error"] = str(e)
            raise uv98.RadioError(str(e))


def doppler(sat_id, freq_hz):
    s = sats.get(sat_id)
    if not s:
        return freq_hz, 0
    el, _, _, rr = look(s["sat"], ts.now())
    if el <= 0:
        return freq_hz, 0
    f = freq_hz * (1 - rr / C_KM_S)
    return int(round(f)), int(round(f - freq_hz))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/api/passes":
            self.send_json(passes_payload())
        elif self.path == "/api/radio":
            try:
                s = get_radio().get_status()
                self.send_json({"connected": True, "status": s})
            except Exception as e:
                self.send_json({"connected": False, "error": str(e)})
        else:
            self.send_json({"error": "not found"}, 404)

    def do_POST(self):
        try:
            n = int(self.headers.get("Content-Length") or 0)
            req = json.loads(self.rfile.read(n) or b"{}")
        except Exception:
            return self.send_json({"ok": False, "error": "bad JSON"}, 400)
        try:
            dev = get_radio(connect=True)
            if self.path == "/api/connect":
                return self.send_json({"ok": True, "status": dev.get_status()})
            if self.path == "/api/tune":
                freq, shift = int(req["freq_hz"]), 0
                if req.get("doppler", True) and req.get("sat_id"):
                    freq, shift = doppler(req["sat_id"], freq)
                s = dev.tune(freq, req["mode"])
                self.send_json({"ok": True, "status": s, "requested_hz": int(req["freq_hz"]),
                                "tuned_hz": freq, "doppler_hz": shift})
            elif self.path == "/api/radio":
                if "step_hz" in req:
                    s = dev.step(int(req["step_hz"]))
                elif "volume_delta" in req:
                    s = dev.set_volume(dev.volume + int(req["volume_delta"]))
                else:
                    return self.send_json({"ok": False, "error": "nothing to do"}, 400)
                self.send_json({"ok": True, "status": s})
            else:
                self.send_json({"ok": False, "error": "not found"}, 404)
        except (uv98.RadioError, KeyError, ValueError) as e:
            self.send_json({"ok": False, "error": str(e)}, 409)
        except Exception as e:
            self.send_json({"ok": False, "error": f"{type(e).__name__}: {e}"}, 500)


def connect_at_start():
    try:
        get_radio(connect=True)
    except uv98.RadioError:
        pass


if __name__ == "__main__":
    threading.Thread(target=refresher, daemon=True).start()
    threading.Thread(target=connect_at_start, daemon=True).start()
    print(f"Satellite tracker on http://localhost:{PORT}  ({SITE_NAME})", flush=True)
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
