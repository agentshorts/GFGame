# HANDOFF feature/gf-protection

Faction protection with visitor and guest rules (option C). Branched from staging `e0005ee`. Local commits only.

## Rules

- Members are always protected on their own faction's land. This can't be turned off.
- The host is always protected, on any land. The host is also exempt from the visitor build and jail rule.
- Visitors (players on another faction's land) start **unprotected**.
- The faction **leader** can always toggle protection for an outsider on their land, per player (`Protect` / `Unprotect`). The server checks that the target is standing on the toggler's faction land (`NotOnYourLand`).
- The leader grants or revokes that ability per member (`GrantToggle` / `RevokeToggle`). The grant covers both `Protect` / `Unprotect` and `Guest` / `Unguest`. Only the leader can grant or revoke it (a granted member can't grant others), and the server checks it on every toggle and guest change.
- The leader, or a member the leader granted, can mark a visitor as a **guest** (`Guest` / `Unguest`). Like the toggles, the target must be standing on the setter's faction land (`NotOnYourLand`), for both Guest and Unguest. A guest gets full rights (build, break, jail power) plus protection. A guest can't be unprotected, and `Protect` on a guest is refused too (`TargetIsGuest`): a guest is already protected, and Unguest restores the protection state the visitor had before, so a Protect on top would be lost anyway.
- Visitors without guest status can't build, break or use jail power on another faction's land (`VisitorNoRights`). They can still paint. Plot permissions still apply on top for everyone.
- When a visitor loses protection (Unprotect, or Unguest with no protection left), the server records the **Dragger** (who did it) and a UTC timestamp:
  - on the target's player state as `AGFPlayerState::LastDrag` (replicated, `FGFDragRecord`);
  - in the social component's replicated `Access` book on the visitor's entry.
  The live record is cleared when protection comes back on.
- **Powers** (grow, shrink, grab) need the target on safe land and, for a visitor, unprotected. Protected targets are immune (`TargetProtected`): members at home, the host, guests and protected visitors. Powers on the caster's own faction stay `SameFaction`. On unowned safe land (safe circles) nobody is faction-protected, so powers work as before.
- **Holds**: a grab, grow or shrink only ends when someone leaves safe land, never when protection changes, so re-protecting a held visitor does not release them. The one place that decides this is `UGFSocialComponent::PowerStillHolds`; set `bReleaseHoldOnProtect=True` in `DefaultGame.ini` to also end holds when the target becomes protected.
- **Jailed players are never protected**, even on their own land (`IsPlayerSafe` and `IsProtectedNow` are false while jailed).
- **Danger checks** use protection: `IsPlayerSafe` ("safe land AND protected") drives emote noise. Power gating and holds use land only (`IsOnSafeLand`: the current zone's `IsInsideSafe`, or any owned land).
- All-chat line when protection is revoked: `bBroadcastProtectionRevoked` (**on** by default, placeholder text) and `ProtectionRevokedChat` in `Config/DefaultGame.ini` under `[/Script/GFSocial.GFSocialComponent]`. Tokens are `{Dragger}`, `{Target}` and `{Faction}`. An empty string posts nothing. No text is hard-coded.

"Leader" is the social layer's leader (`Describe().bLeader`, which follows leader handover), not only the first claimer. Toggle, guest and grant state is session-only and is not saved.

Faction ids and land come from the existing territory config (`FactionLayout`). Nothing in code names a faction.

## Where it lives

- **GFCore** `GFSafeZone.h`:
  - `FGFSafeSubject { Key, GroupId, bHost }`.
  - `IGFSafeZoneQuery::IsSafeFor(const FGFSafeSubject&, Location)` means "in safe land AND protected". It defaults to `IsInsideSafe`, so every existing provider (safe circles, the giant) behaves as before.
  - `IGFSafeZoneQuery::OwnerAt(Location)` returns the owning group, defaulting to `NAME_None`.
  - `UGFSafeZoneSubsystem::IsSafeFor` and `OwnerAt`. Both find the source that owns the land at the location: the override alone if one is set, otherwise every provider, newest first. `IsSafeFor` asks that owner, so the giant's newer circle can't hide protection. On unowned land `IsSafeFor` falls back to the current zone, as before.
  - `GFLineTokens.h`: `FGFLineTokens` fills `{Target}`, `{Dragger}` and `{Faction}` in any line template. It stays game-agnostic.
- **GFNet**:
  - `FGFDragRecord` and `AGFPlayerState::LastDrag`.
  - `IGFModeHandler::EditBlockReason` (default none), asked by `AGFGameState::EditBlockReason` from `ValidateEdit` after the reach check and before the plot check.
  - `AGFGameState::ServerPostSystemChat`: a server line in chat with no sender.
- **GFSocial**:
  - Rules in `FGFProtectionRules` (pure, unit tested).
  - Replicated state in `FGFFactionAccess` (`UGFSocialComponent::Access`).
  - Verbs and server entry points `ServerSetProtected`, `ServerSetGuest` and `ServerSetToggleGrant`.
  - `IsPlayerSafe` now calls `UGFSafeZoneSubsystem::IsSafeFor`, so social powers, grabs and the like follow protection.
  - Jail power checks `bVisitorNoRights`.
  - Joining or leaving a faction drops that player's grants and their visitor entry on the new faction's land.
- **GFGame Territory**:
  - `FGFTerritoryMap` overrides `IsSafeFor(FGFSafeSubject)` and `OwnerAt`.
  - It reads the social `Access` live through `AccessSource`, which the subsystem binds to the world's social component.
  - The older `IsSafeFor(FGFSocialPlayer)` and `IsSafeFor(FName)` overloads stay "own land only" (dinos use them).

GFSocial does not depend on GFGame:
- Safety and land ownership go through the GFCore interface.
- The build gate goes through the GFNet mode handler.
- The territory map pulls `Access` through a callback, and GFGame already depends privately on GFSocial.

### IsSafeFor signature (decision)

The territory HANDOFF proposal had `IsSafeFor(FName Group, Location)`. Protection is per player, so the GFCore call takes `FGFSafeSubject` (player key, group, host flag) instead. The group-only overloads remain on the territory map.

## GFGiant hook (flagged)

This is the minimal hook so Big Grumble lines can use `{Dragger}` next to `{Target}`:

- `FGFGiantPlayerView` gains `Dragger` and `DraggedAtUtcTicks`.
- `GFGiantEventComponent::GatherPlayers` copies them from `LastDrag`.
- `FGFGiantSim` passes the target's dragger to its line picker.
- `{Dragger}` is filled by `FGFLineTokens`. With no dragger, `, {Dragger}` is dropped with its comma and any other `{Dragger}` reads "someone".

There are no other changes to GFGiant. GFRhythm, GFFashion and the art importer are untouched.

## Tests

- `GFGame.Protection.Rules`: every rule as a pure function.
- `GFGame.Protection.QueryDefaults`: the GFCore defaults and the line tokens.
- `GFGame.Territory.Protection`: owner lookup, members, visitors, host, guest, per player, and the live `AccessSource`.
- `GFGame.Social.Protection`: the server component in a bare world with pawns. Covers permissions, the target-on-your-land check (toggles and guests), a granted member doing Guest / Unguest, a revoked member refused (`NotAllowed`), a granted member unable to grant others, Unchanged, Dragger and timestamp, the config chat line (on by default), grants, guests (no Protect on top), powers on visitors versus protected players, the build gate, jailed players not protected, and faction-change cleanup.
- `GFGame.Social.ProtectionProviders`: a land provider plus the real giant event (default circle) registered in both orders; `IsSafeFor` answers from the land owner.
- `GFGame.Protection.Net`: listen server plus 2 clients. Covers replication, the build, break and jail gate, paint still allowed, grants, host, guest, and holds: a grabbed and grown visitor stays held through protect and unprotect, and both end when they leave safe land.
- `GFGame.Giant.DraggerToken`: `{Dragger}` in Big Grumble lines.

## Config

- `Config/DefaultGame.ini`: added the `[/Script/GFSocial.GFSocialComponent]` block. The broadcast is on by default with a placeholder line ("lol, {Dragger} just left {Target} out in the open."); `bReleaseHoldOnProtect=False`.
- `Config/DefaultEngine.ini`: checked, no change needed. No new modules, plugins or redirects.

## Risks and follow-ups

- **Big Grumble (follow-up in GFGiant, notes only, nothing built here)**: he CAN move into safe and protected areas; he just can't attack protected players there. Only attacks are blocked, never movement. The jail interior is the one physical place he can't enter (he may run up to a jailed target but not get inside). Jailed players are unprotected; no protection flag is involved in the jail rule. Today his targeting still uses `IsInsideSafe`; switching the attack check to `IsSafeFor` is part of this follow-up.
- **Unowned land with several providers**: `IsSafeFor` and `IsInsideSafe` still answer from the newest provider there (the giant's circle during an event). Owned land is fixed.
- **Safe land for powers** now also counts owned faction land when the giant's circle is the current zone (`IsInsideSafe` or `OwnerAt`).
- **Guest changes need the target on your land**: `Guest` / `Unguest` return `NotOnYourLand` unless the target stands on the setter's faction land, the same as the protection toggles. This is a default choice (the land in question is always the setter's own faction land), logged as a design-decision issue. A side effect: a guest who has walked off your land can't be unguested until they come back. To change it, drop the `LandOwner` check at the end of `FGFProtectionRules::CanSetGuest`.
- **The word "guest"**: GFNet already says "guest" for a non-host player. Here it means a faction guest. Code uses `bGuest` on `FGFVisitorAccess` only.
- `{Faction}` in the chat line is the config id string, not a display name.
- The edit gate checks the land at the block. The jail check uses the land the jailer stands on.
