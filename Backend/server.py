"""Local OnlineTower account service. Standard library only; loopback HTTP only."""
import argparse
from collections import deque
from contextlib import contextmanager
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import secrets
import sqlite3
import threading
import time
import uuid

ITERATIONS = 600_000
SESSION_SECONDS = 86400
MAX_BODY = 4096


class ApiError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


class Accounts:
    def __init__(self, database):
        self.database = str(database)
        Path(database).parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            db.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS users (
                    id TEXT PRIMARY KEY, username TEXT NOT NULL COLLATE NOCASE UNIQUE,
                    salt BLOB NOT NULL, password_hash BLOB NOT NULL,
                    iterations INTEGER NOT NULL, created_at INTEGER NOT NULL);
                CREATE TABLE IF NOT EXISTS sessions (
                    token_hash TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id),
                    expires_at INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS session_expiry ON sessions(expires_at);
                PRAGMA user_version=1;
            """)

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.database, timeout=10)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        try:
            with db:
                yield db
        finally:
            db.close()

    @staticmethod
    def credentials(body):
        username, password = body.get("username"), body.get("password")
        if not isinstance(username, str) or not re.fullmatch(r"[A-Za-z0-9_]{3,24}", username):
            raise ApiError(400, "Username must contain 3-24 letters, digits or underscores.")
        if not isinstance(password, str) or not 8 <= len(password) <= 128:
            raise ApiError(400, "Password must contain 8-128 characters.")
        return username, password

    @staticmethod
    def digest(password, salt, iterations=ITERATIONS):
        return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, iterations)

    def authenticate(self, body, register=False):
        username, password = self.credentials(body)
        with self.connect() as db:
            if register:
                salt = secrets.token_bytes(32)
                user_id = uuid.uuid4().hex
                digest = self.digest(password, salt)
                try:
                    db.execute("INSERT INTO users VALUES (?,?,?,?,?,?)",
                               (user_id, username, salt, digest, ITERATIONS, int(time.time())))
                except sqlite3.IntegrityError:
                    raise ApiError(409, "Username already exists.") from None
            else:
                user = db.execute("SELECT * FROM users WHERE username=?", (username,)).fetchone()
                # Perform the same expensive derivation for unknown accounts.
                salt = user["salt"] if user else bytes(32)
                digest = self.digest(password, salt, user["iterations"] if user else ITERATIONS)
                if not user or not hmac.compare_digest(digest, user["password_hash"]):
                    raise ApiError(401, "Invalid username or password.")
                user_id, username = user["id"], user["username"]
            token = secrets.token_urlsafe(32)
            now = int(time.time())
            db.execute("DELETE FROM sessions WHERE expires_at<=?", (now,))
            db.execute("INSERT INTO sessions VALUES (?,?,?)",
                       (self.token_hash(token), user_id, now + SESSION_SECONDS))
        return {"token": token, "user": {"id": user_id, "username": username}}

    @staticmethod
    def token_hash(token):
        return hashlib.sha256(token.encode("utf-8")).hexdigest()

    def user(self, token):
        if not token or len(token) > 256:
            raise ApiError(401, "Authentication required.")
        with self.connect() as db:
            user = db.execute("""SELECT u.id,u.username FROM users u JOIN sessions s
                ON s.user_id=u.id WHERE s.token_hash=? AND s.expires_at>?""",
                              (self.token_hash(token), int(time.time()))).fetchone()
        if not user:
            raise ApiError(401, "Session expired or invalid.")
        return {"id": user["id"], "username": user["username"]}

    def logout(self, token):
        self.user(token)
        with self.connect() as db:
            db.execute("DELETE FROM sessions WHERE token_hash=?", (self.token_hash(token),))


class AccountServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, database):
        self.accounts = Accounts(database)
        self.auth_attempts = deque()
        self.attempt_lock = threading.Lock()
        self.hash_slots = threading.BoundedSemaphore(4)
        super().__init__(address, Handler)

    def check_rate(self):
        with self.attempt_lock:
            now = time.monotonic()
            while self.auth_attempts and self.auth_attempts[0] < now - 60:
                self.auth_attempts.popleft()
            if len(self.auth_attempts) >= 30:
                raise ApiError(429, "Too many attempts. Try again in one minute.")
            self.auth_attempts.append(now)


class Handler(BaseHTTPRequestHandler):
    server_version = "OnlineTowerLocal/1"

    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, format, *args):
        pass  # Do not log passwords, bearer tokens or user input.

    def reply(self, status, body):
        data = json.dumps(body, ensure_ascii=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        if self.headers.get("Transfer-Encoding"):
            raise ApiError(400, "Chunked requests are not supported.")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            raise ApiError(400, "Invalid content length.") from None
        if length <= 0 or length > MAX_BODY:
            raise ApiError(413, "Request body must contain 1-4096 bytes.")
        if self.headers.get_content_type() != "application/json":
            raise ApiError(415, "Expected application/json.")
        try:
            body = json.loads(self.rfile.read(length))
        except (ValueError, UnicodeError):
            raise ApiError(400, "Invalid JSON.") from None
        if not isinstance(body, dict):
            raise ApiError(400, "Expected a JSON object.")
        return body

    def token(self):
        auth = self.headers.get("Authorization", "")
        return auth[7:] if auth.startswith("Bearer ") else ""

    def dispatch(self):
        try:
            if self.command == "GET" and self.path == "/health":
                result, status = {"ok": True}, 200
            elif self.command == "POST" and self.path in ("/v1/auth/register", "/v1/auth/login"):
                self.server.check_rate()
                body = self.body()
                if not self.server.hash_slots.acquire(blocking=False):
                    raise ApiError(429, "Service busy. Try again shortly.")
                try:
                    register = self.path.endswith("register")
                    result = self.server.accounts.authenticate(body, register)
                    status = 201 if register else 200
                finally:
                    self.server.hash_slots.release()
            elif self.command == "GET" and self.path == "/v1/auth/me":
                result, status = {"user": self.server.accounts.user(self.token())}, 200
            elif self.command == "POST" and self.path == "/v1/auth/logout":
                self.server.accounts.logout(self.token())
                result, status = {"ok": True}, 200
            else:
                raise ApiError(404, "Route not found.")
            self.reply(status, result)
        except ApiError as error:
            self.reply(error.status, {"error": error.message})
        except (TimeoutError, ConnectionError):
            self.close_connection = True
        except Exception:
            self.reply(500, {"error": "Internal service error."})

    do_GET = dispatch
    do_POST = dispatch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--db", type=Path, default=Path(__file__).parent / "data" / "tower.sqlite3")
    args = parser.parse_args()
    server = AccountServer(("127.0.0.1", args.port), args.db)
    print(f"OnlineTower accounts: http://127.0.0.1:{args.port} (local only)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
