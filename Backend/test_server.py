import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from server import AccountServer, Accounts


class AccountApiTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.database = Path(self.directory.name) / "accounts.sqlite3"
        self.server = AccountServer(("127.0.0.1", 0), self.database)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.directory.cleanup()

    def request(self, path, body=None, token=None, method=None):
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = "Bearer " + token
        data = json.dumps(body).encode() if body is not None else None
        request = Request(self.base + path, data=data, headers=headers, method=method)
        try:
            response = urlopen(request, timeout=10)
        except HTTPError as error:
            response = error
        with response:
            return response.status, json.load(response)

    def register(self, username="Alice_1", password="test-password-123"):
        status, body = self.request("/v1/auth/register", {"username": username, "password": password})
        self.assertEqual(status, 201)
        return body

    def test_registration_login_identity_logout_and_persistence(self):
        first = self.register()
        second = self.register("Bob_2")
        self.assertNotEqual(first["user"]["id"], second["user"]["id"])
        self.assertNotEqual(first["token"], second["token"])
        status, me = self.request("/v1/auth/me", token=first["token"])
        self.assertEqual((status, me["user"]), (200, first["user"]))
        status, logged_in = self.request("/v1/auth/login", {"username": "ALICE_1", "password": "test-password-123"})
        self.assertEqual((status, logged_in["user"]), (200, first["user"]))
        reopened = Accounts(self.database)
        self.assertEqual(reopened.user(first["token"]), first["user"])
        with reopened.connect() as db:
            user = db.execute("SELECT * FROM users WHERE id=?", (first["user"]["id"],)).fetchone()
            self.assertNotEqual(user["password_hash"], b"test-password-123")
            tokens = [row[0] for row in db.execute("SELECT token_hash FROM sessions")]
            self.assertNotIn(first["token"], tokens)
        self.assertEqual(self.request("/v1/auth/logout", token=first["token"], method="POST")[0], 200)
        self.assertEqual(self.request("/v1/auth/me", token=first["token"])[0], 401)
        self.assertEqual(self.request("/v1/auth/me", token=second["token"])[0], 200)

    def test_duplicate_validation_expired_and_forged_tokens(self):
        account = self.register()
        self.assertEqual(self.request("/v1/auth/register", {"username": "alice_1", "password": "another-password"})[0], 409)
        for username, password in [("bad name", "password123"), ("Valid", "short"), (None, "password123")]:
            self.assertEqual(self.request("/v1/auth/register", {"username": username, "password": password})[0], 400)
        self.assertEqual(self.request("/v1/auth/login", {"username": "Alice_1", "password": "wrong-password"})[0], 401)
        self.assertEqual(self.request("/v1/auth/me", token="forged")[0], 401)
        self.assertEqual(self.request("/v1/auth/me")[0], 401)
        with self.server.accounts.connect() as db:
            db.execute("UPDATE sessions SET expires_at=?", (int(time.time())-1,))
        self.assertEqual(self.request("/v1/auth/me", token=account["token"])[0], 401)

    def test_body_limit_routes_and_rate_limit(self):
        self.assertEqual(self.request("/health")[0], 200)
        self.assertEqual(self.request("/missing")[0], 404)
        self.assertEqual(self.request("/v1/auth/register", {"username": "x" * 5000})[0], 413)
        for _ in range(29):
            self.assertEqual(self.request("/v1/auth/login", {})[0], 400)
        self.assertEqual(self.request("/v1/auth/login", {})[0], 429)


if __name__ == "__main__":
    unittest.main()
