# Local OnlineTower account backend

Python 3.11+, standard library, SQLite. It binds only `127.0.0.1`; this is a local prototype, not a publicly hosted authentication service.

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe' Backend/server.py
```

The database is created in `Backend/data/tower.sqlite3` and excluded from Git. Accounts survive restarts. Stop with Ctrl+C. Optional arguments: `--port 18080 --db <path>`.

API on `http://127.0.0.1:18080`:

| Method/path | Request | Response |
|---|---|---|
| GET `/health` | none | `{ "ok": true }` |
| POST `/v1/auth/register` | `{ "username": "Player_1", "password": "your-password" }` | 201 `{ "token": "...", "user": { "id": "...", "username": "Player_1" } }` |
| POST `/v1/auth/login` | same | 200, same schema |
| GET `/v1/auth/me` | `Authorization: Bearer ...` | 200 `{ "user": { "id": "...", "username": "..." } }` |
| POST `/v1/auth/logout` | same bearer header | 200 `{ "ok": true }` |

Errors use `{ "error": "message" }`. Usernames: 3–24 ASCII letters/digits/underscores, case insensitive uniqueness; passwords: 8–128 characters. Tokens expire in 24 hours. Passwords use salted PBKDF2-HMAC-SHA256 (600,000 iterations). Only token hashes are stored. A global loopback budget allows 30 registration/login attempts per minute, with at most four password derivations concurrently. Request bodies are capped at 4096 bytes. Credentials are not logged.

Party membership, invitations, portal readiness and combat remain authoritative on the Unreal game server. They are not account records or persistent character progression. Production hosting, HTTPS, email verification, password recovery and account moderation are outside this local prototype.

Tests:

```powershell
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe' -m unittest discover -s Backend -v
```

Tests start a temporary HTTP server and database; they do not alter real accounts.
