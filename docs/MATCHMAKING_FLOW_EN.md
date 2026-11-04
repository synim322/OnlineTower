# Account, party and matchmaking flow

Implementation dated 2 November 2026. [Русский](MATCHMAKING_FLOW_RU.md). Project: `OnlineTower 5.8/OnlineTower.uproject`, UE 5.8.4.

## Player flow

In a normal game window: **Play → pick a local character → Online / account → register or sign in → hub**. Start the account backend and game server first. The networked loadout is still server-issued; the local profile stays separate and is not yet a persistent account character.

Esc opens the pause menu (including **Invite friend / party**) and blocks only that player's input. The world, other players and queue timer continue, including in standalone. **Solo**: no party required — one player enters a portal and waits in queue. **Invite friend / party**: search by nickname (including offline accounts), add as a friend, then use “Invite to party” or “Invite to party by nickname”. Matching ignores letter case. Cross-hub friend invites go through the backend: after acceptance the friend travels to the inviter's hub and joins the party. If both players already share a hub, nickname invite also allows a same-server party invite without friendship. Acceptance requires hub/menu state (not queue or match). In PIE multiplayer the window is already in the listen-server session: a separate “connect to hub” step is unnecessary and is not a failure.

A party contains at most **six people including its leader**. Only the leader can invite. All party members must enter the same portal to start searching; a solo player enters alone. After queueing, a search menu opens with the trial, search duration timer and waiting/capacity counts. Cancel from that menu or the account panel. When the roster is full, every player gets an Accept button with a **10-second** timer; anyone who does not press it leaves the search, while accepters stay queued. A party larger than the trial capacity cannot start search.

| Portal | Required roster | Player-to-player damage |
|---|---:|---|
| Dungeon | 5 | Disabled |
| Duel | 2 | Enabled between the two members of the same match, regardless of social party |
| Final | 12 | Not added; queue and entry only |

An oversized party is rejected without splitting. Matchmaking combines whole parties and solo tickets, for example 2+1+1+1. It does not add people to a social party. Cancellation by any member cancels the entire ticket; leave the portal before entering again. Party changes, logout/authentication loss and disconnect invalidate affected tickets.

## Testing through Unreal Editor

For `Invalid backend response`, ensure `Start-LocalBackend.cmd` is running: `http://127.0.0.1:18080/health` must return JSON `{"ok": true}`. A local HTTP proxy may return a non-JSON error while the backend is down. `DefaultEngine.ini` now sets `[HTTP] HttpNoProxy=127.0.0.1,localhost`; restart the editor to apply it. Multi-window PIE must use `L_Hub`, not `L_MainMenu`.

1. Start `OnlineTower 5.8/Start-LocalBackend.cmd`.
2. Open `/Game/Maps/Hub/L_Hub` in the editor.
3. Select **New Editor Window**, **Play As Listen Server**, **Number of Players = 5**, **Run Under One Process**. Use 2 for a duel or 7 for party-limit checks. Sign into a different account in every window. Each unauthenticated window automatically displays the login panel. Multi-window PIE on one listen server uses `hub_a`; cross-hub travel is covered by `Start-Social-Hubs.cmd` / `Test-SocialMultiplayer.ps1`.
4. For the dungeon, invite one friend by nickname (or via friends) and leave the other windows solo. Move everyone into the dungeon portal: the match must wait for five. Check the waiting HUD, cancellation/re-entry and disabled friendly fire after entry.
5. For a duel, move two players into its portal and verify mutual damage even if they share a social party. Death still uses training respawn; this change does not implement a winner or rewards.

`Scripts/Configure-EditorMultiplayer.ps1 -Players 5` writes PIE settings while the **editor is closed**, preserving other user settings. The project default is five windows.

**The two launch routes differ:** multi-window PIE starts directly on `L_Hub` with a login screen; normal clients start on `L_MainMenu` and connect to a separate server. PIE on `L_MainMenu` does not move the whole listen-server world after one player signs in. To test the actual menu route, run from the repository root:

```powershell
& './OnlineTower 5.8/Scripts/Start-HubMultiplayer.ps1' -Players 5
```

This starts a local game server and five game windows at the main menu. Start the account backend separately. Sign in in each window. The default server address is `127.0.0.1:17777`; clients can override it with `-TowerServer=host:port`. The script prints its server PID and records its processes. Close your game windows and stop that server after manual testing.

## Architecture and limits

`Trials/TowerMatchmaking.*` implements the authoritative WorldSubsystem and atomic party tickets. PlayerState replicates `MatchId`, plus owner-only `QueuedTrial`, `QueueStartedAt` and `QueuePopulation`. `TowerPartyComponent` validates nickname invitations, the six-player cap and cancellation. `TowerFrontend`, `TowerAccountSubsystem` and `TowerHub` display UI and send requests. `TowerCharacter` validates damage against trial and MatchId; clients do not decide opponents.

One local server world, `L_Hub`, contains spatially separated trial areas. Matched players teleport to the appropriate area while unrelated players stay in the hub. Each trial type supports one active roster at a time; the next roster waits until every previous participant has left. Return portals affect individuals, not their entire social party. Death/respawn clears participation.

This is real local server matchmaking, not Internet matchmaking or allocation across separate server processes. Environments replicate without duplicating global lighting. The account database does not store queues or rosters. Persistent characters, saved outcomes/rewards, full PvE objectives and match victory rules remain separate work.

The final trial gains only capacity/queue/entry. Trio formation, combat rules, outcomes, rewards and progression admission are not implemented here. A capacity of 12 does not establish a successful 12-window run.

## Automated validation

With backend port 18080 and game-server port 17779 free:

```powershell
& './OnlineTower 5.8/Scripts/Test-Matchmaking.ps1' -TrialIndex 0
& './OnlineTower 5.8/Scripts/Test-Matchmaking.ps1' -TrialIndex 1
& './OnlineTower 5.8/Scripts/Test-PartyLimits.ps1'
```

Run sequentially. Scripts create isolated databases and real hidden Unreal processes using `-nullrhi`, and stop only their own processes. Dungeon coverage uses five participants plus an uninvolved hub client; duel uses two plus an uninvolved client. Assertions cover full-roster waiting, replicated timer, MatchId, retained loadouts and damage policy. Party-limit coverage uses seven clients for the six-player cap, oversized-party rejection and cancellation. These are API/runtime checks, not physical mouse-click automation.

`Test-PartyLimits.ps1` uses seven clients and port 17780. The old `Test-PartyMultiplayer.ps1` now forwards to the current five-player dungeon test.

## Verified on 2 November 2026

- Full `OnlineTowerEditor Win64 Development` build on UE 5.8.4: **PASS**. Existing C4305 color-literal warnings remain.
- Menu → registration → hub → dungeon matchmaking: **PASS**, five participants plus a sixth uninvolved client. Covered case-insensitive nickname invitation, waiting for the second party member at the portal, a 2+1+1+1 roster, replicated timer, retained loadouts, blocked friendly fire and spectator isolation. Logs: `OnlineTower 5.8/Saved/Logs/Matchmaking-1102115435-0/`.
- Menu → duel: **PASS**, two participants plus a third uninvolved client. Covered waiting for the second player, shared MatchId, permitted opponent damage and blocked outsider damage. Logs: `Saved/Logs/Matchmaking-1102115203-1/` within the working project.
- Limits/cancellation: **PASS**, seven clients; party-six cap, seventh rejected, party-six rejected by dungeon-five and party-three rejected by duel-two; one member cancels the three-player ticket, with no automatic re-entry over four seconds. Logs: `Saved/Logs/PartyLimits-1102115332/`.
- PowerShell syntax parsed successfully; `git diff --check` passed. Local PIE settings applied for five windows.
- Captured and inspected the account/invitation panel offscreen: `Saved/Screenshots/Menu-Online.png`, using `Scripts/Capture-Menus.ps1 -View Online`. This verifies the signed-out panel appearance, not physical clicks or an active queue screenshot.

These runs used separate headless Unreal processes, not the editor's Play button. Physical login/search clicks, five graphical PIE windows and the 12-player final were not runtime-verified here. Experimental Python Toolsets still emit initialization errors in `-game`; the listed gameplay checks passed. Shipping/packaged builds and Internet hosting were not tested.
