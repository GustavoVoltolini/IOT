"""Receptor mínimo para testar o envio do ESP32 antes de ter o backend de verdade.

    python tools/backend_teste.py

Depois, em include/secrets.h:
    #define BACKEND_URL "http://<IP-do-seu-PC>:3000/api/feeder/events"

Imprime cada evento recebido e grava em tools/eventos.jsonl. Só biblioteca padrão.
"""

import json
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PORT = 3000
LOG = Path(__file__).resolve().parent / "eventos.jsonl"
REQUIRED = {"id", "device", "type"}
seen_ids = set()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def reply(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        if self.path != "/api/feeder/events":
            return self.reply(404, {"error": "not_found"})
        try:
            n = int(self.headers.get("Content-Length") or 0)
            event = json.loads(self.rfile.read(n))
        except (ValueError, json.JSONDecodeError):
            return self.reply(400, {"error": "invalid_json"})
        if not REQUIRED <= event.keys():
            return self.reply(422, {"error": "missing_fields", "required": sorted(REQUIRED)})

        if event["id"] in seen_ids:
            return self.reply(200, {"ok": True, "duplicate": True})  # reenvio do ESP32
        seen_ids.add(event["id"])

        event["received_at"] = datetime.now().astimezone().isoformat(timespec="seconds")
        with LOG.open("a", encoding="utf-8") as f:
            f.write(json.dumps(event, ensure_ascii=False) + "\n")
        print(json.dumps(event, ensure_ascii=False))
        self.reply(201, {"ok": True})


if __name__ == "__main__":
    print(f"Esperando eventos em http://0.0.0.0:{PORT}/api/feeder/events")
    ThreadingHTTPServer(("", PORT), Handler).serve_forever()
