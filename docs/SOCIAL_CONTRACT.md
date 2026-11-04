# Social system implementation contract

Date: 2026-11-02. Status: implementation contract, not a statement that every route or test is complete. The current user request takes precedence over older project plans.

## Ownership and boundaries

- Backend implementer owns `Backend/**`: persistence, account search, friendship lifecycle, presence, invitations, trusted registry and ticket consumption.
- Unreal online implementer owns `OnlineTower 5.8/Source/OnlineTower/Online/**`: HTTP client, server credentials, presence publication, social state, invitation redemption and authoritative party integration.
- Coordinator owns frontend integration, tests, launch scripts and other documentation. This contract file is authored by the architecture reviewer only during this assignment.
- Keep one author per file. Existing party authority and maximum of six members remain server-side. Social friendship and gameplay party membership are separate systems.
- Blocking users is not requested and is outside this implementation.

## Wire format

All JSON field names use `snake_case`. Client routes require the existing account `Authorization: Bearer <token>`. Server routes require `X-Tower-Server-Id` and `X-Tower-Server-Key`, plus the affected player's `user_token` in the body. Credentials and tickets must not appear in logs, replicated properties or travel URLs.

Errors use the existing JSON `error` field and appropriate non-2xx status. Invalid identity, missing records, stale state, capacity conflicts, expiry and replay must fail without granting friendship or party membership. All identity and ownership checks happen on the backend or authoritative game server; UI-supplied names and IDs are requests, not authority.

## Client routes

| Method and route | Request | Success data / behavior |
|---|---|---|
| `GET /v1/social/search?q=...` | URL-encoded nickname query | `users`: bounded list of `{id, username}`; includes offline registered accounts; no email, secrets or token data. |
| `GET /v1/social/state` | None | `friends`, `incoming_requests`, `outgoing_requests`, `party_invites`, as defined below. |
| `POST /v1/social/friend-request` | `{target_id}` | Create a pending request to another account; reject self and invalid targets; prevent duplicate pending pairs. |
| `POST /v1/social/friend-respond` | `{request_id, accept}` | Recipient only; accept creates reciprocal friendship atomically; decline removes the pending request from active lists. |
| `POST /v1/social/friend-cancel` | `{request_id}` | Sender only; cancel a pending request. |
| `POST /v1/social/friend-remove` | `{target_id}` | Remove friendship for both accounts. |
| `POST /v1/social/presence` | `{}` | Refresh account online/menu liveness only. Cannot claim a game server, party or match. |
| `POST /v1/social/party-respond` | `{invite_id, accept}` | Recipient only. Decline closes invitation; accept returns `{join_ticket, address, server_id}` from trusted registry, not client input. |

Minimal snapshot rows:

- `friends`: `{id, username, online, status, server_id}`. `status` is `offline`, `menu`, `hub`, `queued` or `in_match`; absent server identity uses an empty string. A server association is exposed only while trusted presence is fresh.
- `incoming_requests`: `{id, user_id, username}` where the user is the sender.
- `outgoing_requests`: `{id, user_id, username}` where the user is the recipient.
- `party_invites`: `{id, user_id, username, server_id, expires_at}` where user_id is the inviter. `expires_at` is Unix time in seconds.

Backend storage persists friendships and friend-request lifecycle across restarts. Canonical unordered account pairs enforce unique reciprocal friendships. Acceptance must atomically transition a pending request and create its friendship; retries must never create duplicate rows. Social operations must not expose unrelated users' request records. Account search may return a bounded partial nickname match; invitation targets still resolve to stable account IDs.

## Trusted server routes

| Method and route | Request body | Behavior / result |
|---|---|---|
| `POST /v1/server/presence` | `{user_token, status, party_id, leader_id, party_size}` | Validate configured server credential and account token. Publish authoritative presence; status is `hub`, `queued` or `in_match`. `party_id` is a string or empty; `leader_id` is the leader's account ID, not Unreal PlayerId; `party_size` is the current member count, with solo represented as 1. |
| `POST /v1/server/party-invite` | `{user_token, target_id}` | Authenticated caller's server creates an invitation. Check friendship, fresh inviter presence, leader authority when grouped, capacity and eligible hub state. Return `{invite_id}`. |
| `POST /v1/server/consume-ticket` | `{user_token, join_ticket}` | Atomically consume once after validating account binding, target server binding, expiry and inviter's current presence/party. Return `{inviter_id, party_id}`. The game server still performs final roster checks. |

Server heartbeat interval: 5 seconds. Presence freshness TTL: 20 seconds. Publish promptly after authentication and party/queue/match state changes. Stale presence becomes offline unless a fresh client-only menu heartbeat exists; client heartbeats cannot overwrite a fresh trusted game-server association.

Party invitation lifetime: 60 seconds. Accepted join-ticket lifetime: 30 seconds. Tickets are opaque, unpredictable, single-use and bound to accepting account, destination server, inviter and intended party identity. The backend must reject replay and credentials for another destination. Avoid stale source-server heartbeat overwriting the destination during travel: use a short destination lease associated with successful ticket consumption until the new server's presence arrives.

## Registry and local configuration

The backend generates `Backend/data/servers.json` on first startup if absent. It is gitignored and contains local development credentials:

```json
{"servers":[{"id":"hub_a","address":"127.0.0.1:17777","key":"<random local secret>"},{"id":"hub_b","address":"127.0.0.1:17778","key":"<random local secret>"}]}
```

Backend `--servers-file` overrides the file for isolated tests. Game server launch supplies its configured identity and server-side registry file via `-TowerServerId` and `-TowerServersFile`. Never accept an arbitrary destination address from an invitation request. No registry-management UI or dynamic PIE server registration is required.

## Travel and final party admission

1. Inviter sends a normal server RPC. Its authoritative server calls the backend invitation route.
2. Recipient sees the invitation in social state and accepts through the client route.
3. Client retains the returned ticket in its game-instance subsystem across travel. It travels only to the registry-returned address when destination server differs from its current authenticated server identity.
4. Current server identity is supplied by an owner-only server response/RPC, not inferred from a client-set field. Same-server invitations skip travel and redeem on that server.
5. Destination first authenticates the account, then receives a `RedeemJoinTicket` request from its owning controller. It calls `consume-ticket` with its own credential and that player's account token.
6. Destination resolves `inviter_id` to a currently authenticated PlayerState. It verifies intended party, inviter leadership, maximum six members, and that neither side is searching or in a match. Only then does the server update party membership. An inviter without an existing party may create one during this authorized join.
7. A ticket is not a reserved seat. If final admission fails after consumption, display the reason and require a new invitation; do not silently join another party or grant a client-specified PartyId. Clear pending client ticket on final result or expiry.

## PIE and integration acceptance

One-authority PIE multi-window tests use the configured local `hub_a` identity. Same-server comparison avoids attempting travel to the registry's standalone port. Cross-server invitation tests require separate game-server processes on ports 17777 and 17778; dynamic registration of arbitrary PIE ports is outside scope.

Required checks: offline account search; pending request creation/cancel/decline/accept; reciprocal persistence after backend restart; remove friendship; TTL expiry; forged client presence rejected; wrong server credential rejected; unknown destination rejected; ticket wrong-account/wrong-server/replay/expiry rejected; same-server join without travel; cross-server join with identity preserved; party full or inviter departed rejected at admission. Confirm secrets/tickets are absent from logs and travel URLs. Report actual checks separately from this contract.
