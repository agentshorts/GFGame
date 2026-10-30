# HANDOFF feature/unbinding-taming

The Unbinding is pure rules in this folder (mode id `Unbinding`). A completion only adds `BigGrumbleTame` to the named recipient. A later `AttemptTame` spends one of those once `NotifySpawned` has seen the display name `Big Grumble`. Nothing here calls jail release, kick, ban, faction claim, or logout.

`GFGame.Build.cs`, `DefaultEngine.ini`, `DefaultGame.ini`, `Plugins/VoxelFinish/**`, and `Source/GFGame/Social/GFJail*` were not changed. New `.cpp` files live under the GFGame module root, so the build should pick them up without an include-path edit. If a compile cannot find `GFUnbinding.h`, add this module's `Endgame` directory to `PrivateIncludePaths` in `Source/GFGame/GFGame.Build.cs`. Do not add a GFSocial dependency.

Outside changes worth doing later:

1. Done on staging: `Data/CSV/DT_Items.csv` has a `BigGrumbleTame` row (Material, tier 4 Rare, not wagerable, not rentable). The default headcount is now the game's max players (GameSession MaxPlayers, 10), not `MaxPerFaction` x 3.

2. Spawn hook — when `UGFGiantEventComponent` sets `AGFGiantDino::DisplayName` from `DT_GiantDino` and when that event ends, a GFGame-side watcher should call `FGFUnbinding::NotifySpawned` / `NotifyGone`. That watcher needs `GFGiant` on `GFGame.Build.cs`. Do not make GFGiant depend on GFGame. Do not send the material through `UGFTamingComponent::ServerOffer` or `FGFSocialRules::CanRelease`.

3. After a successful `TryComplete` or `AttemptTame` on the server, call `PushMaterial` on the recipient's `AGFPlayerState` so the existing owner-only inventory matches the bag. `ReadSession` / `FromPlayerState` already copy `PlayerKey`, `bIsHost`, `FactionId`, `bLoggedOut`, and `ColorIndex`.
