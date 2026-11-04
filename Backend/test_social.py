import json
from pathlib import Path
import tempfile
import threading
import unittest

from server import Accounts, ApiError
from social import Social, PRESENCE_TTL, TICKET_TTL, load_registry


class SocialTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.database = Path(self.directory.name) / "accounts.sqlite3"
        self.accounts = Accounts(self.database)
        self.users = [self.accounts.authenticate({"username": name, "password": "test-password-123"}, True)
                      for name in ("Alice_1", "Bob_2", "Carol_3")]
        self.now = 1000
        self.registry = {name: {"id": name, "address": "127.0.0.1:"+str(17777+i), "key": name+"_secret_"*5}
                         for i, name in enumerate(("hub_a", "hub_b"))}
        self.social = Social(self.accounts, self.registry, ApiError, lambda: self.now)

    def tearDown(self):
        self.directory.cleanup()

    def uid(self, index): return self.users[index]["user"]["id"]
    def token(self, index): return self.users[index]["token"]
    def client(self, route, who=0, body=None, query=""):
        return self.social.client(route, self.token(who), body, query)

    def server(self, route, who=0, hub="hub_a", **body):
        return self.social.server(route, hub, self.registry[hub]["key"], dict(body, user_token=self.token(who)))

    def presence(self, who=0, hub="hub_a", **extra):
        body = {"status": "hub", "party_id": "", "leader_id": self.uid(who), "party_size": 1}
        body.update(extra)
        return self.server("presence", who, hub, **body)

    def befriend(self, a=0, b=1):
        request = self.client("friend-request", a, {"target_id": self.uid(b)})["request_id"]
        self.client("friend-respond", b, {"request_id": request, "accept": True})
        return request

    def assert_api(self, status, action):
        with self.assertRaises(ApiError) as raised: action()
        self.assertEqual(raised.exception.status, status)

    def invitation(self):
        self.befriend()
        self.presence(0)
        self.presence(1, "hub_b")
        return self.server("party-invite", target_id=self.uid(1))["invite_id"]

    def test_offline_search_friend_lifecycle_and_restart(self):
        result = self.client("search", query="bO")
        self.assertEqual(result["users"][0]["id"], self.uid(1))
        self.assertFalse(result["users"][0]["online"])
        self.assertNotIn("password_hash", result["users"][0])
        self.assert_api(400, lambda: self.client("friend-request", body={"target_id": self.uid(0)}))
        self.assert_api(404, lambda: self.client("friend-request", body={"target_id": "missing"}))
        request = self.client("friend-request", body={"target_id": self.uid(1)})["request_id"]
        self.assertEqual(self.client("friend-request", body={"target_id": self.uid(1)})["request_id"], request)
        self.assert_api(409, lambda: self.client("friend-request", 1, {"target_id": self.uid(0)}))
        self.assert_api(404, lambda: self.client("friend-respond", 2, {"request_id": request, "accept": True}))
        self.client("friend-cancel", body={"request_id": request})
        self.assertEqual(self.client("state", 1)["incoming_requests"], [])
        request = self.client("friend-request", body={"target_id": self.uid(1)})["request_id"]
        self.client("friend-respond", 1, {"request_id": request, "accept": False})
        self.assertEqual(self.client("state")["friends"], [])
        request = self.befriend()
        self.client("friend-respond", 1, {"request_id": request, "accept": True})
        reopened = Social(Accounts(self.database), self.registry, ApiError, lambda: self.now)
        self.assertEqual(reopened.client("state", self.token(1))["friends"][0]["id"], self.uid(0))
        with self.accounts.connect() as db:
            self.assertEqual(db.execute("PRAGMA user_version").fetchone()[0], 2)
            self.assertEqual(db.execute("SELECT COUNT(*) FROM friendships").fetchone()[0], 1)
        self.client("friend-remove", body={"target_id": self.uid(1)})
        self.assertEqual(self.client("state", 1)["friends"], [])

    def test_presence_authentication_ttl_and_menu_cannot_override_server(self):
        self.befriend()
        self.client("presence", 1, {})
        self.assertEqual(self.client("state")["friends"][0]["status"], "menu")
        self.assert_api(400, lambda: self.client("presence", 1, {"server_id": "hub_a"}))
        self.assert_api(403, lambda: self.social.server("presence", "hub_a", "bad", {"user_token": self.token(1)}))
        self.presence(1, "hub_b", status="queued")
        self.client("presence", 1, {})
        self.assertEqual(self.client("state")["friends"][0]["status"], "queued")
        self.now += PRESENCE_TTL + 1
        self.assertFalse(self.client("state")["friends"][0]["online"])
        self.presence(1)
        self.accounts.logout(self.token(1))
        self.assertFalse(self.client("state")["friends"][0]["online"])
        self.assert_api(401, lambda: self.client("state", 1))

    def test_ticket_bindings_replay_and_transfer_lease(self):
        invite = self.invitation()
        self.assert_api(404, lambda: self.client("party-respond", 2, {"invite_id": invite, "accept": True}))
        result = self.client("party-respond", 1, {"invite_id": invite, "accept": True})
        self.assertEqual(result["address"], self.registry["hub_a"]["address"])
        ticket = result["join_ticket"]
        self.assert_api(403, lambda: self.server("consume-ticket", 2, join_ticket=ticket))
        self.assert_api(403, lambda: self.server("consume-ticket", 1, "hub_b", join_ticket=ticket))
        self.assert_api(409, lambda: self.presence(1, "hub_b"))
        admitted = self.server("consume-ticket", 1, join_ticket=ticket)
        self.assertEqual(admitted, {"inviter_id": self.uid(0), "party_id": ""})
        self.assert_api(403, lambda: self.server("consume-ticket", 1, join_ticket=ticket))
        self.presence(1, "hub_a")
        self.assert_api(409, lambda: self.presence(1, "hub_b"))

    def test_stale_changed_removed_inviter_and_ticket_expiry(self):
        invite = self.invitation()
        self.presence(0, party_id="new-party", leader_id=self.uid(0), party_size=2)
        self.assert_api(409, lambda: self.client("party-respond", 1, {"invite_id": invite, "accept": True}))
        self.presence(0)
        invite = self.server("party-invite", target_id=self.uid(1))["invite_id"]
        self.now += PRESENCE_TTL + 1
        self.assert_api(409, lambda: self.client("party-respond", 1, {"invite_id": invite, "accept": True}))
        self.presence(0)
        invite = self.server("party-invite", target_id=self.uid(1))["invite_id"]
        ticket = self.client("party-respond", 1, {"invite_id": invite, "accept": True})["join_ticket"]
        self.now += TICKET_TTL + 1
        self.presence(0)
        self.assert_api(403, lambda: self.server("consume-ticket", 1, join_ticket=ticket))
        invite = self.server("party-invite", target_id=self.uid(1))["invite_id"]
        ticket = self.client("party-respond", 1, {"invite_id": invite, "accept": True})["join_ticket"]
        self.client("friend-remove", body={"target_id": self.uid(1)})
        self.assert_api(403, lambda: self.server("consume-ticket", 1, join_ticket=ticket))
        self.assertEqual(self.client("state", 1)["party_invites"], [])

    def test_reverse_request_race_has_one_pending_pair(self):
        barrier = threading.Barrier(2)
        results = []
        def request(a, b):
            barrier.wait()
            try: results.append(self.client("friend-request", a, {"target_id": self.uid(b)}))
            except ApiError as error: results.append(error.status)
        threads = [threading.Thread(target=request, args=pair) for pair in ((0, 1), (1, 0))]
        for thread in threads: thread.start()
        for thread in threads: thread.join()
        self.assertEqual(len(results), 2)
        self.assertIn(409, results)
        with self.accounts.connect() as db:
            self.assertEqual(db.execute("SELECT COUNT(*) FROM friend_requests WHERE status='pending'").fetchone()[0], 1)

    def test_registry_generation_and_newer_schema_rejection(self):
        file = Path(self.directory.name) / "servers.json"
        first = load_registry(file)
        self.assertEqual(first, load_registry(file))
        self.assertEqual(set(first), {"hub_a", "hub_b"})
        self.assertNotEqual(first["hub_a"]["key"], first["hub_b"]["key"])
        file.write_text(json.dumps({"servers": [{"id": "bad", "address": "http://evil/path", "key": "x"*32}]}))
        with self.assertRaises(ValueError): load_registry(file)
        with self.accounts.connect() as db: db.execute("PRAGMA user_version=3")
        with self.assertRaises(RuntimeError): Social(Accounts(self.database), self.registry, ApiError)


if __name__ == "__main__": unittest.main()
