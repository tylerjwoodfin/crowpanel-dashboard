#!/usr/bin/env python3
"""Serve incomplete titles from the Reminders list named Grocery.

Read-only. The CrowPanel fetches GET /grocery on the LAN. Nothing here
completes, edits, or deletes a reminder.

A LaunchAgent cannot read the Reminders store until macOS allows it, so this
tries the local store first and falls back to the EventKit helper.
"""

import json
import os
import sqlite3
import subprocess
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

PORT = 8765
LIST_NAME = "Grocery"
STORES = Path.home() / "Library/Group Containers/group.com.apple.reminders/Container_v1/Stores"
HELPER = (
    Path(__file__).resolve().parent
    / "GroceryList.app"
    / "Contents"
    / "MacOS"
    / "GroceryList"
)

QUERY = """
SELECT r.ZTITLE, r.ZIDENTIFIER, r.ZCREATIONDATE
FROM ZREMCDREMINDER r
JOIN ZREMCDBASELIST l ON l.Z_PK = r.ZLIST
WHERE l.ZNAME = ?
  AND r.ZCOMPLETED = 0
  AND IFNULL(r.ZMARKEDFORDELETION, 0) = 0
  AND r.ZPARENTREMINDER IS NULL
  AND r.ZTITLE IS NOT NULL
"""


def _order_index(con):
    row = con.execute(
        "SELECT ZREMINDERIDSMERGEABLEORDERING_V2_JSON FROM ZREMCDBASELIST WHERE ZNAME = ?",
        (LIST_NAME,),
    ).fetchone()
    if not row or not row[0]:
        return {}
    try:
        order = json.loads(row[0])
    except (json.JSONDecodeError, TypeError):
        return {}
    if not isinstance(order, list):
        return {}
    return {str(item).lower(): i for i, item in enumerate(order) if isinstance(item, str)}


def _sort_key(blob, created, index):
    created = created or 0
    if not isinstance(blob, (bytes, bytearray)):
        return (10**9, created)
    try:
        key = str(uuid.UUID(bytes=bytes(blob))).lower()
    except ValueError:
        return (10**9, created)
    return (index.get(key, 10**9), created)


def _titles_in(path):
    uri = path.resolve().as_uri() + "?mode=ro"
    con = sqlite3.connect(uri, uri=True, timeout=3)
    try:
        con.execute("PRAGMA query_only = ON")
        names = [row[0] for row in con.execute("SELECT ZNAME FROM ZREMCDBASELIST")]
        if LIST_NAME not in names:
            return False, []
        index = _order_index(con)
        rows = list(con.execute(QUERY, (LIST_NAME,)))
    finally:
        con.close()

    rows.sort(key=lambda row: _sort_key(row[1], row[2], index))
    titles = []
    seen = set()
    for title, _blob, _created in rows:
        text = " ".join(str(title).split())
        if not text or text in seen:
            continue
        seen.add(text)
        titles.append(text)
    return True, titles


def titles_from_store():
    """Incomplete titles in the list's manual order. Raises if the store is blocked."""
    try:
        os.listdir(STORES)
    except FileNotFoundError as err:
        raise FileNotFoundError(f"Reminders store not found: {STORES}") from err
    except PermissionError as err:
        raise PermissionError("Reminders store is not readable from this service") from err

    found = False
    titles = []
    errors = []
    for path in sorted(STORES.glob("Data-*.sqlite")):
        try:
            has_list, found_titles = _titles_in(path)
        except sqlite3.Error as err:
            errors.append(err)
            continue
        if not has_list:
            continue
        found = True
        titles.extend(found_titles)
    if not found and errors:
        raise errors[0]

    deduped = []
    seen = set()
    for title in titles:
        if title in seen:
            continue
        seen.add(title)
        deduped.append(title)
    return deduped


def titles_from_helper():
    if not HELPER.is_file():
        raise FileNotFoundError(f"helper not built: {HELPER}")
    try:
        proc = subprocess.run([str(HELPER)], capture_output=True, timeout=6, check=False)
    except subprocess.TimeoutExpired as err:
        raise RuntimeError(
            "Allow CrowPanel Grocery in System Settings > Privacy & Security > Reminders"
        ) from err
    if proc.returncode != 0:
        detail = proc.stderr.decode(errors="replace").strip() or f"exit {proc.returncode}"
        raise RuntimeError(detail)
    payload = json.loads(proc.stdout)
    items = payload.get("items")
    if not isinstance(items, list):
        raise RuntimeError("helper returned no items list")
    titles = []
    seen = set()
    for item in items:
        if not isinstance(item, str):
            continue
        text = " ".join(item.split())
        if not text or text in seen:
            continue
        seen.add(text)
        titles.append(text)
    return titles


def incomplete_grocery_titles():
    """Incomplete Grocery titles. Store order when the store is readable."""
    try:
        return titles_from_store()
    except PermissionError:
        return titles_from_helper()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if urlparse(self.path).path != "/grocery":
            self.send_error(404)
            return
        try:
            items = incomplete_grocery_titles()
        except Exception as err:
            body = json.dumps({"error": str(err)}).encode()
            self._send(500, body)
            return
        self._send(200, json.dumps({"items": items}).encode())

    def _send(self, code, body):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    import sys

    if "--check" in sys.argv:
        items = incomplete_grocery_titles()
        if any(not item.strip() for item in items):
            raise SystemExit("blank grocery title")
        print(f"{len(items)} incomplete")
        return
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"grocery list on 0.0.0.0:{PORT}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
