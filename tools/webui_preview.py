#!/usr/bin/env python3
"""Preview the web UI on your computer, backed by the simulator's SD card folder.

    python3 tools/webui_preview.py [--scheme racer] [--port 8080]

Seed shots first with:  SIM_SEED=24 SIM_SNAPS=1 .pio/build/native/program
"""
import argparse
import csv
import io
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD = os.path.join(ROOT, "sim", "sdcard", "coffeescale")
INDEX = os.path.join(SD, "index.json")
state = {"scheme": "roast", "ref": 0}


def load_index():
    try:
        with open(INDEX) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"version": 1, "shots": []}


def save_index(doc):
    with open(INDEX, "w") as f:
        json.dump(doc, f)


class Handler(BaseHTTPRequestHandler):
    def send(self, code, body, ctype="application/json", extra=None):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def shot(self, sid):
        with open(os.path.join(SD, "shots", f"{sid}.json")) as f:
            return f.read()

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        if u.path == "/":
            with open(os.path.join(ROOT, "web", "index.html"), "rb") as f:
                return self.send(200, f.read(), "text/html")
        if u.path == "/api/info":
            n = len(load_index()["shots"])
            return self.send(200, json.dumps({"name": "Coffee Scale", "scheme": state["scheme"],
                                              "sd": os.path.isdir(SD), "count": n, "ref": state["ref"],
                                              "recipe": "Espresso", "deviceTime": 0}))
        if u.path == "/api/shots":
            return self.send(200, json.dumps(load_index()))
        if u.path == "/api/shot":
            try:
                body = self.shot(q["id"])
            except (OSError, KeyError):
                return self.send(404, '{"error":"not_found"}')
            extra = {"Content-Disposition": f'attachment; filename="shot-{q["id"]}.json"'} if "download" in q else None
            return self.send(200, body, extra=extra)
        if u.path == "/api/shot.csv":
            s = json.loads(self.shot(q["id"]))["samples"]
            out = io.StringIO()
            w = csv.writer(out)
            w.writerow(["time_s", "weight_g", "flow_gps"])
            w.writerows(zip(s["t"], s["w"], s["f"]))
            return self.send(200, out.getvalue(), "text/csv",
                             {"Content-Disposition": f'attachment; filename="shot-{q["id"]}.csv"'})
        if u.path == "/api/export.json":
            shots = [json.loads(self.shot(m["id"])) for m in load_index()["shots"]]
            return self.send(200, json.dumps({"shots": shots}), extra={"Content-Disposition": 'attachment; filename="coffee-shots.json"'})
        if u.path == "/api/export.csv":
            out = io.StringIO()
            w = csv.writer(out)
            w.writerow(["id", "time", "recipe", "dose_g", "yield_g", "duration_s", "rating", "notes"])
            for m in load_index()["shots"]:
                w.writerow([m["id"], m["time"], m["recipe"], m["dose"], m["yield"], m["duration"], m["rating"], m["notes"]])
            return self.send(200, out.getvalue(), "text/csv", {"Content-Disposition": 'attachment; filename="coffee-shots.csv"'})
        self.send(404, "not found", "text/plain")

    def do_POST(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        sid = int(q.get("id", 0))
        doc = load_index()
        if u.path == "/api/rate":
            for m in doc["shots"]:
                if m["id"] == sid:
                    m["rating"] = int(q.get("stars", 0))
            save_index(doc)
        elif u.path == "/api/delete":
            doc["shots"] = [m for m in doc["shots"] if m["id"] != sid]
            save_index(doc)
        elif u.path == "/api/reference":
            state["ref"] = sid
        else:
            return self.send(404, "not found", "text/plain")
        self.send(200, '{"ok":true}')

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--scheme", default="roast", choices=["roast", "racer"])
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--ref", type=int, default=0)
    a = ap.parse_args()
    state["scheme"], state["ref"] = a.scheme, a.ref
    print(f"Web UI preview on http://localhost:{a.port}")
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()
