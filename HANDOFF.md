# Jail permissions

Server-side jail for the session. `UGFJailSubsystem` (a replicated component on `AGFGameState`) checks `FGFJailRules` and is the only writer of the jailed set.

## Assumptions

- **Persistent player id** is `AGFPlayerState::PlayerKey` (platform net id, or `Name#PlayerId` when the net id is missing). The engine `GetPlayerId()` changes when a new player state is spawned on rejoin, so jail is not keyed by it.
- **Host** is `AGFPlayerState::bIsHost`, set in `AGFGameMode::PostLogin` for the local controller.
- **Faction** is `AGFPlayerState::FactionId`, claimed through `AGFGameState::ServerClaimFaction`. Ids stay opaque in the rules. Tests use Cinderwrights, Verdant, and Rime as those ids. The three-seat cap stays `AGFGameState::MaxPerFaction`.
- **Claim order** is the first time a `PlayerKey` successfully claims that faction this session (`AGFGameState::OnFactionChanged`). Leaving and claiming it again does not move that order. A logged-out player is not a remaining member.
- **Who leads, for jail.** `FGFJailRules::ChooseLeader` picks the earliest claim among online members of that faction, skipping jailed members unless every remaining member is jailed. Jailing the current leader hands leadership to the next free member immediately. When they are released, the earlier claim leads again. This is recomputed, not stored.
- **`AGFGameState::FactionLeaders` is unchanged.** It is still the first claimer for the whole session, including after they log out or switch faction (`IsFactionLeader`). Existing social jail (`UGFSocialComponent` / `FGFSocialRules`) still reads that. Jail permissions in this change do not.
- **No timer, session only.** The set lives on the component until `Release`. Logout does not clear it. A new player state with the same `PlayerKey` is jailed again in `NotePlayerJoined`. It is not written to the world save.
- **Mute and emotes are queries.** `IsMuted` is both voice and text. `CanEmote` is false when muted. Nothing here plays audio, sends chat, or teleports. `UGFSocialComponent` still owns its own jail list, chat gate, and cell.
- **Clients.** `Jail`, `Release`, and `AGFPlayerState::SetJailed` return without writing unless the owner has authority. `bJailed` and `JailedFlags` replicate server to client. There is no client RPC that assigns the flag.
- **Header location.** `Source/GFGame/Social/GFJailSubsystem.h` includes `Source/GFGame/Private/GFJailSubsystem.h`. That is the reflected component. Unreal Header Tool only generates code for `Public/` and `Private/`. The rules headers stay in `Social/` because they are not UObject types.
- **Not wired into chat.** Something that wants the mute has to call `IsMuted` / `CanEmote`. `AGFGameState::ServerHandleChat` is untouched.

## Classes used

| Need | Class | Field |
|---|---|---|
| Persistent id | `AGFPlayerState` | `PlayerKey` |
| Host | `AGFPlayerState` | `bIsHost` |
| Faction | `AGFPlayerState` | `FactionId` |
| First claimer (unchanged) | `AGFGameState` | `FactionLeaders`, `IsFactionLeader`, `ServerClaimFaction` |
| Rejoin faction | `AGFGameState` | `SessionFactions` via `NotifyModesPlayerReady` |
| Per-player replicated flag | `AGFPlayerState` | `bJailed`, `SetJailed` |
| Jailed set | `UGFJailSubsystem` (`Private/GFJailSubsystem.h`, included from `Social/GFJailSubsystem.h`) | `JailedFlags` |
