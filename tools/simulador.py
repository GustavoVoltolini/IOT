"""Simula o ESP32 no PC para testar a interface sem hardware.

    python tools/simulador.py      ->  abra http://localhost:8080

Serve o mesmo src/web/index.html e imita a API do firmware: o pote "enche"
quando você alimenta, o gato come devagar, e as regras de recusa são as mesmas
(capacidade - margem). Só biblioteca padrão.
"""

import json
import random
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

INDEX = Path(__file__).resolve().parent.parent / "src" / "web" / "index.html"
PORT = 8080
MIN_FEED_G = 2.0
CATCHUP_MIN = 30

lock = threading.Lock()
state = {
    "weight": 12.0,
    "max_g": 150.0,
    "margin_g": 10.0,
    "portion_g": 30.0,
    "flow_gps": 6.0,
    "feeding": False,
    "target": None,
    "stalls": 0,
    "meals": [
        {"hour": 8, "minute": 0, "grams": 30, "enabled": True, "last_day": None},
        {"hour": 18, "minute": 30, "grams": 30, "enabled": True, "last_day": None},
    ],
    "history": [],
    "boot": time.time(),
    "current": None,
}


def today():
    return time.strftime("%Y-%j")


def record(source, result, requested, before, after):
    dispensed = None if after is None or before is None else round(after - before, 1)
    state["history"].insert(0, {
        "at": int(time.time()), "source": source, "result": result,
        "requested_g": requested, "dispensed_g": dispensed if result.startswith("ok") else (dispensed or 0),
        "before_g": round(before, 1), "after_g": None if after is None else round(after, 1),
        "estimated": False,
    })
    del state["history"][20:]


def request_feed(grams, source):
    """Mesma regra do feeder_core::decideFeed."""
    if state["feeding"]:
        return "ocupado"
    w = state["weight"]
    if not grams or grams <= 0:
        record(source, "quantidade_invalida", grams, w, w)
        return "quantidade_invalida"
    if w >= state["max_g"] - state["margin_g"]:
        record(source, "pote_cheio", grams, w, w)
        return "pote_cheio"
    target = min(w + grams, state["max_g"])
    if target - w < MIN_FEED_G:
        record(source, "quase_cheio", grams, w, w)
        return "quase_cheio"
    state.update(feeding=True, target=target, current={"source": source, "requested": grams, "before": w})
    return "ok"


def finish(result):
    c = state["current"]
    record(c["source"], result, c["requested"], c["before"], state["weight"])
    state.update(feeding=False, target=None, current=None)


def physics():
    """Ração caindo em pulsos e gato comendo de vez em quando."""
    while True:
        time.sleep(0.5)
        with lock:
            if state["feeding"]:
                state["weight"] += state["flow_gps"] * 0.5 * random.uniform(0.6, 1.2)
                if state["weight"] >= state["target"] - 2:
                    finish("ok")
            elif state["weight"] > 0 and random.random() < 0.08:
                state["weight"] = max(0.0, state["weight"] - random.uniform(0.5, 2.5))

            lt = time.localtime()
            minute = lt.tm_hour * 60 + lt.tm_min
            for m in state["meals"]:
                late = minute - (m["hour"] * 60 + m["minute"])
                if m["enabled"] and m["last_day"] != today() and 0 <= late <= CATCHUP_MIN and not state["feeding"]:
                    m["last_day"] = today()
                    request_feed(m["grams"], "agenda")


def next_meal():
    lt = time.localtime()
    now = lt.tm_hour * 60 + lt.tm_min
    best = None
    for m in state["meals"]:
        if not m["enabled"]:
            continue
        delta = m["hour"] * 60 + m["minute"] - now
        if delta < 0 or m["last_day"] == today():
            delta += 24 * 60
        if best is None or delta < best[0]:
            best = (delta, m)
    return None if best is None else {k: best[1][k] for k in ("hour", "minute", "grams")}


def status():
    return {
        "device": "alimentador-simulado",
        "uptime_s": int(time.time() - state["boot"]),
        "bowl": {
            "weight_g": round(state["weight"], 1), "scale_ok": True, "calibrated": True,
            "max_g": state["max_g"], "margin_g": state["margin_g"],
            "limit_g": state["max_g"] - state["margin_g"], "portion_g": state["portion_g"],
            "flow_gps": state["flow_gps"],
        },
        "feeding": {"active": state["feeding"], "target_g": state["target"]},
        "next_meal": next_meal(),
        "wifi": {"connected": True, "ap": False, "ssid": "CasaSimulada", "ip": "127.0.0.1", "rssi": -58},
        "clock": {"valid": True, "epoch": int(time.time())},
        "backend": {"enabled": False, "pending": 0, "sent": 0, "last_error": ""},
    }


def schedule():
    return [{"hour": m["hour"], "minute": m["minute"], "grams": m["grams"], "enabled": m["enabled"],
             "done_today": m["last_day"] == today()} for m in state["meals"]]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send(self, obj, code=200):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n) or b"{}")

    def do_GET(self):
        with lock:
            if self.path == "/":
                html = INDEX.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.end_headers()
                self.wfile.write(html)
            elif self.path == "/api/status":
                self.send(status())
            elif self.path == "/api/history":
                self.send(state["history"])
            elif self.path == "/api/schedule":
                self.send(schedule())
            else:
                self.send({"ok": False, "error": "nao_encontrado"}, 404)

    def do_PUT(self):
        with lock:
            if self.path != "/api/schedule":
                return self.send({"ok": False, "error": "nao_encontrado"}, 404)
            old = {(m["hour"], m["minute"]): m["last_day"] for m in state["meals"]}
            lt = time.localtime()
            now = lt.tm_hour * 60 + lt.tm_min
            meals = []
            for m in self.body()[:8]:
                key = (m["hour"], m["minute"])
                last = old.get(key, today() if m["hour"] * 60 + m["minute"] <= now else None)
                meals.append({**m, "last_day": last})
            state["meals"] = meals
            self.send(schedule())

    def do_POST(self):
        with lock:
            b = self.body()
            if self.path == "/api/feed":
                r = request_feed(b.get("grams", state["portion_g"]), "manual")
                self.send({"ok": r == "ok", "result": r})
            elif self.path == "/api/stop":
                if state["feeding"]:
                    finish("cancelado")
                self.send({"ok": True, "result": "ok"})
            elif self.path == "/api/settings":
                state.update(max_g=b["max_g"], margin_g=b["margin_g"], portion_g=b["portion_g"])
                self.send({"ok": True, "result": "ok"})
            elif self.path == "/api/tare":
                state["weight"] = 0.0
                self.send({"ok": True, "result": "ok"})
            elif self.path == "/api/calibrate":
                self.send({"ok": True, "factor": 412.3456, "offset": 84213})
            elif self.path in ("/api/time", "/api/jog", "/api/wifi"):
                self.send({"ok": True, "result": "ok"})
            else:
                self.send({"ok": False, "error": "nao_encontrado"}, 404)


if __name__ == "__main__":
    threading.Thread(target=physics, daemon=True).start()
    print(f"Simulador em http://localhost:{PORT}  (Ctrl+C para sair)")
    ThreadingHTTPServer(("", PORT), Handler).serve_forever()
