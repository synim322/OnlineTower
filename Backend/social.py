"""Persistent friendships and short-lived, server-authoritative social presence."""
from collections import deque
import hmac
import json
from pathlib import Path
import re
import secrets
import threading
import time
import uuid

PRESENCE_TTL = 20
INVITE_TTL = 60
TICKET_TTL = 30


def migrate(accounts):
    with accounts.connect() as db:
        version = db.execute("PRAGMA user_version").fetchone()[0]
        if version > 2:
            raise RuntimeError("Database schema is newer than this server.")
        db.executescript("""
            CREATE TABLE IF NOT EXISTS friendships (
                low_id TEXT NOT NULL REFERENCES users(id),
                high_id TEXT NOT NULL REFERENCES users(id),
                created_at INTEGER NOT NULL, PRIMARY KEY(low_id,high_id), CHECK(low_id<high_id));
            CREATE TABLE IF NOT EXISTS friend_requests (
                id TEXT PRIMARY KEY, sender_id TEXT NOT NULL REFERENCES users(id),
                recipient_id TEXT NOT NULL REFERENCES users(id),
                low_id TEXT NOT NULL, high_id TEXT NOT NULL,
                status TEXT NOT NULL CHECK(status IN ('pending','accepted','declined','cancelled')),
                created_at INTEGER NOT NULL, CHECK(sender_id<>recipient_id));
            CREATE UNIQUE INDEX IF NOT EXISTS one_pending_friend_pair
                ON friend_requests(low_id,high_id) WHERE status='pending';
            CREATE INDEX IF NOT EXISTS requests_recipient ON friend_requests(recipient_id,status);
            PRAGMA user_version=2;
        """)


def load_registry(path):
    path = Path(path)
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        data = {"servers": [{"id": "hub_a", "address": "127.0.0.1:17777", "key": secrets.token_urlsafe(32)},
                            {"id": "hub_b", "address": "127.0.0.1:17778", "key": secrets.token_urlsafe(32)}]}
        # Exclusive creation avoids overwriting a concurrently created credential file.
        try:
            with path.open("x", encoding="utf-8") as stream:
                json.dump(data, stream, indent=2)
        except FileExistsError:
            pass
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    result = {}
    for row in data.get("servers", []):
        if (not isinstance(row, dict) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", row.get("id", ""))
                or not re.fullmatch(r"[A-Za-z0-9.-]+:[0-9]{1,5}", row.get("address", ""))
                or not 1 <= int(row["address"].rsplit(":", 1)[1]) <= 65535
                or not isinstance(row.get("key"), str) or len(row["key"]) < 24 or row["id"] in result):
            raise ValueError("Invalid trusted server registry.")
        result[row["id"]] = row
    if not result:
        raise ValueError("Trusted server registry is empty.")
    return result


class Social:
    def __init__(self, accounts, registry, error_type, clock=time.time):
        self.accounts, self.registry, self.error, self.clock = accounts, registry, error_type, clock
        self.lock = threading.RLock()
        self.client_presence, self.server_presence = {}, {}
        self.invites, self.tickets, self.leases, self.rates = {}, {}, {}, {}
        migrate(accounts)

    def require(self, condition, status, message):
        if not condition:
            raise self.error(status, message)

    def identifier(self, body, key):
        value = body.get(key)
        self.require(isinstance(value, str) and 1 <= len(value) <= 128, 400, "Invalid " + key + ".")
        return value

    def authenticated(self, token):
        self.require(isinstance(token, str), 401, "Authentication required.")
        return self.accounts.user(token)

    def check_rate(self, user_id):
        now = self.clock()
        for key in list(self.rates):
            if not self.rates[key] or self.rates[key][-1] <= now - 60:
                del self.rates[key]
        attempts = self.rates.setdefault(user_id, deque())
        while attempts and attempts[0] <= now - 60:
            attempts.popleft()
        self.require(len(attempts) < 180, 429, "Too many social requests. Try again shortly.")
        attempts.append(now)

    def fresh(self, value):
        if not value or value["expires_at"] <= self.clock():
            return False
        with self.accounts.connect() as db:
            return db.execute("SELECT 1 FROM sessions WHERE token_hash=? AND expires_at>?",
                              (value["token_hash"], int(time.time()))).fetchone() is not None

    def clean(self):
        now = self.clock()
        for values in (self.client_presence, self.server_presence, self.invites, self.tickets, self.leases):
            for key in list(values):
                if values[key]["expires_at"] <= now:
                    del values[key]

    def public_user(self, row):
        result = {"id": row["id"], "username": row["username"], "online": False, "status": "offline", "server_id": ""}
        live = self.server_presence.get(row["id"])
        if self.fresh(live):
            result.update(online=True, status=live["status"], server_id=live["server_id"])
        elif self.fresh(self.client_presence.get(row["id"])):
            result.update(online=True, status="menu")
        return result

    @staticmethod
    def pair(a, b):
        return tuple(sorted((a, b)))

    def are_friends(self, a, b):
        with self.accounts.connect() as db:
            return db.execute("SELECT 1 FROM friendships WHERE low_id=? AND high_id=?", self.pair(a, b)).fetchone() is not None

    def search(self, user, query):
        query = query.strip()
        self.require(2 <= len(query) <= 24 and re.fullmatch(r"[A-Za-z0-9_]+", query), 400, "Search requires 2-24 letters, digits or underscores.")
        # Underscore is a literal nickname character, not SQL LIKE wildcard.
        pattern = query.replace("_", "\\_") + "%"
        with self.accounts.connect() as db:
            rows = db.execute("SELECT id,username FROM users WHERE username LIKE ? ESCAPE '\\' AND id<>? ORDER BY username LIMIT 20",
                              (pattern, user["id"])).fetchall()
        return {"users": [self.public_user(row) for row in rows]}

    def state(self, user):
        uid = user["id"]
        with self.accounts.connect() as db:
            friends = db.execute("""SELECT u.id,u.username FROM friendships f JOIN users u
                ON u.id=CASE WHEN f.low_id=? THEN f.high_id ELSE f.low_id END
                WHERE f.low_id=? OR f.high_id=? ORDER BY u.username""", (uid, uid, uid)).fetchall()
            incoming = db.execute("""SELECT r.id,u.id user_id,u.username FROM friend_requests r
                JOIN users u ON u.id=r.sender_id WHERE r.recipient_id=? AND r.status='pending' ORDER BY r.created_at""", (uid,)).fetchall()
            outgoing = db.execute("""SELECT r.id,u.id user_id,u.username FROM friend_requests r
                JOIN users u ON u.id=r.recipient_id WHERE r.sender_id=? AND r.status='pending' ORDER BY r.created_at""", (uid,)).fetchall()
            invitations = []
            for key, invite in self.invites.items():
                if invite["target_id"] != uid or not self.valid_inviter(invite):
                    continue
                peer = db.execute("SELECT username FROM users WHERE id=?", (invite["inviter_id"],)).fetchone()
                invitations.append({"id": key, "user_id": invite["inviter_id"], "username": peer["username"],
                                    "server_id": invite["server_id"], "expires_at": int(invite["expires_at"])})
        return {"friends": [self.public_user(row) for row in friends], "incoming_requests": [dict(row) for row in incoming],
                "outgoing_requests": [dict(row) for row in outgoing], "party_invites": invitations}

    def friend_request(self, user, body):
        uid, target = user["id"], self.identifier(body, "target_id")
        self.require(uid != target, 400, "Cannot add yourself.")
        low, high = self.pair(uid, target)
        with self.accounts.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            self.require(db.execute("SELECT 1 FROM users WHERE id=?", (target,)).fetchone(), 404, "Account not found.")
            if db.execute("SELECT 1 FROM friendships WHERE low_id=? AND high_id=?", (low, high)).fetchone():
                return {"ok": True, "already_friends": True}
            pending = db.execute("SELECT id,sender_id FROM friend_requests WHERE low_id=? AND high_id=? AND status='pending'", (low, high)).fetchone()
            if pending:
                self.require(pending["sender_id"] == uid, 409, "This player already sent you a request. Accept it instead.")
                return {"ok": True, "request_id": pending["id"]}
            count = db.execute("SELECT COUNT(*) FROM friend_requests WHERE (sender_id=? OR recipient_id=?) AND status='pending'", (uid, uid)).fetchone()[0]
            target_count = db.execute("SELECT COUNT(*) FROM friend_requests WHERE recipient_id=? AND status='pending'", (target,)).fetchone()[0]
            self.require(count < 100 and target_count < 100, 409, "Too many pending requests.")
            request = uuid.uuid4().hex
            db.execute("INSERT INTO friend_requests VALUES (?,?,?,?,?,?,?)", (request, uid, target, low, high, "pending", int(self.clock())))
        return {"ok": True, "request_id": request}

    def friend_respond(self, user, body, cancel=False):
        request = self.identifier(body, "request_id")
        if not cancel:
            self.require(type(body.get("accept")) is bool, 400, "accept must be boolean.")
        with self.accounts.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            row = db.execute("SELECT * FROM friend_requests WHERE id=?", (request,)).fetchone()
            self.require(row and row["sender_id" if cancel else "recipient_id"] == user["id"], 404, "Request not found.")
            outcome = "cancelled" if cancel else "accepted" if body["accept"] else "declined"
            if row["status"] != "pending":
                self.require(row["status"] == outcome, 409, "Request already resolved.")
                return {"ok": True}
            if outcome == "accepted":
                for uid in (row["sender_id"], row["recipient_id"]):
                    count = db.execute("SELECT COUNT(*) FROM friendships WHERE low_id=? OR high_id=?", (uid, uid)).fetchone()[0]
                    self.require(count < 200, 409, "Friend limit reached.")
                db.execute("INSERT OR IGNORE INTO friendships VALUES (?,?,?)", (row["low_id"], row["high_id"], int(self.clock())))
            db.execute("UPDATE friend_requests SET status=? WHERE id=?", (outcome, request))
        return {"ok": True}

    def friend_remove(self, user, body):
        target = self.identifier(body, "target_id")
        with self.accounts.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            db.execute("DELETE FROM friendships WHERE low_id=? AND high_id=?", self.pair(user["id"], target))
            db.execute("UPDATE friend_requests SET status='cancelled' WHERE low_id=? AND high_id=? AND status='pending'", self.pair(user["id"], target))
        for values in (self.invites, self.tickets):
            for key in list(values):
                row = values[key]
                if self.pair(row["inviter_id"], row["target_id"]) == self.pair(user["id"], target):
                    del values[key]
        return {"ok": True}

    def client(self, route, token, body=None, query=""):
        user = self.authenticated(token)
        with self.lock:
            self.clean()
            self.check_rate(user["id"])
            if route == "search": return self.search(user, query)
            if route == "state": return self.state(user)
            if route == "presence":
                self.require(not body, 400, "Clients may not publish server presence.")
                self.client_presence[user["id"]] = {"token_hash": self.accounts.token_hash(token), "expires_at": self.clock()+PRESENCE_TTL}
                return {"ok": True}
            if route == "friend-request": return self.friend_request(user, body)
            if route == "friend-respond": return self.friend_respond(user, body)
            if route == "friend-cancel": return self.friend_respond(user, body, True)
            if route == "friend-remove": return self.friend_remove(user, body)
            if route == "party-respond": return self.party_respond(user, body)
            raise self.error(404, "Route not found.")

    def valid_inviter(self, invite):
        live = self.server_presence.get(invite["inviter_id"])
        return (self.fresh(live) and live["server_id"] == invite["server_id"] and live["status"] == "hub"
                and live["party_id"] == invite["party_id"] and live["leader_id"] == invite["inviter_id"]
                and live["party_size"] < 6 and self.are_friends(invite["inviter_id"], invite["target_id"]))

    def party_respond(self, user, body):
        key = self.identifier(body, "invite_id")
        self.require(type(body.get("accept")) is bool, 400, "accept must be boolean.")
        invite = self.invites.get(key)
        self.require(invite and invite["target_id"] == user["id"], 404, "Invitation not found or expired.")
        if not body["accept"]:
            del self.invites[key]
            return {"ok": True}
        self.require(self.valid_inviter(invite), 409, "Inviter is unavailable or their party changed.")
        own = self.server_presence.get(user["id"])
        self.require(not self.fresh(own) or (own["status"] == "hub" and not own["party_id"]), 409, "Leave your party and finish matchmaking before joining.")
        del self.invites[key]
        ticket = secrets.token_urlsafe(32)
        self.tickets[self.accounts.token_hash(ticket)] = dict(invite, expires_at=self.clock()+TICKET_TTL)
        self.leases[user["id"]] = {"server_id": invite["server_id"], "expires_at": self.clock()+TICKET_TTL}
        return {"join_ticket": ticket, "address": self.registry[invite["server_id"]]["address"], "server_id": invite["server_id"]}

    def server(self, route, server_id, key, body):
        config = self.registry.get(server_id)
        self.require(config and isinstance(key, str) and hmac.compare_digest(config["key"], key), 403, "Invalid game server credential.")
        token = self.identifier(body, "user_token")
        user = self.authenticated(token)
        uid = user["id"]
        with self.lock:
            self.clean()
            self.check_rate(uid)
            if route == "presence":
                status = body.get("status")
                if status == "in_match": status = "match"
                self.require(status in ("hub", "queued", "match"), 400, "Invalid presence status.")
                party, leader, size = body.get("party_id"), body.get("leader_id"), body.get("party_size")
                self.require(isinstance(party, str) and len(party)<=128 and isinstance(leader, str) and 1<=len(leader)<=128
                             and type(size) is int and 1<=size<=6, 400, "Invalid party presence.")
                self.require(bool(party) or (leader == uid and size == 1), 400, "Invalid solo presence.")
                lease = self.leases.get(uid)
                self.require(not lease or lease["server_id"] == server_id, 409, "Account is transferring to another server.")
                self.server_presence[uid] = {"token_hash": self.accounts.token_hash(token), "server_id": server_id, "status": status,
                                             "party_id": party, "leader_id": leader, "party_size": size, "expires_at": self.clock()+PRESENCE_TTL}
                # Keep the short destination lease until it expires; stale source callbacks cannot reclaim presence.
                return {"ok": True}
            if route == "party-invite":
                target = self.identifier(body, "target_id")
                self.require(target != uid and self.are_friends(uid, target), 403, "Only friends can receive a party invitation.")
                invite = {"inviter_id": uid, "target_id": target, "server_id": server_id, "party_id": "", "expires_at": self.clock()+INVITE_TTL}
                live = self.server_presence.get(uid)
                if live: invite["party_id"] = live["party_id"]
                self.require(self.valid_inviter(invite), 409, "Inviter must be a party leader in an available hub.")
                self.require(len([v for v in self.invites.values() if v["target_id"]==target]) < 20, 409, "Too many invitations.")
                for old_key, old in list(self.invites.items()):
                    if old["inviter_id"] == uid and old["target_id"] == target: del self.invites[old_key]
                invite_id = uuid.uuid4().hex
                self.invites[invite_id] = invite
                return {"invite_id": invite_id}
            if route == "consume-ticket":
                ticket_key = self.accounts.token_hash(self.identifier(body, "join_ticket"))
                ticket = self.tickets.get(ticket_key)
                self.require(ticket and ticket["target_id"] == uid and ticket["server_id"] == server_id, 403, "Invalid, expired or used join ticket.")
                self.require(self.valid_inviter(ticket), 409, "Inviter is unavailable or their party changed.")
                del self.tickets[ticket_key]
                self.leases[uid] = {"server_id": server_id, "expires_at": self.clock()+TICKET_TTL}
                return {"inviter_id": ticket["inviter_id"], "party_id": ticket["party_id"]}
            raise self.error(404, "Route not found.")
