# HANDOFF feature/faction-territory

Territory logic stays in `Source/GFGame/Territory/`. `Config/DefaultGame.ini` gained the `FactionLayout` anchors and the growth `Curve`. The staging merge added the `GFGame.Build.cs` lines below.

## What the safe query uses

`IsSafeFor` reads `FGFSocialPlayer::Faction` and matches it to the faction that owns the point. `bHost` is copied from `AGFPlayerState` onto that struct and does not add land. A player with no faction is not safe on faction land, in the neutral disc, or in the wild.

`CopySafeRegions` does not bake a disc down to one member. `Members` stays the effective head count, `BaseRadius` is the one-member radius, and `RadiusPerExtraMember` is the curve step, so `FGFSafeRegion::Radius()` equals base + per member × N.

## Build.cs (applied on staging)

`Territory/Tests/GFTerritoryTests.cpp` includes `GFTerritoryMap.h` and `GFSocialTypes.h`. Quoted includes from that folder do not see `Territory/`, and this module does not depend on GFSocial yet. In `Source/GFGame/GFGame.Build.cs`:

```csharp
PrivateDependencyModuleNames.AddRange(new string[] { "GFSocial" });
PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Territory"));
```

`GFSocial` does not depend on `GFGame`, so the private dependency does not cycle.

## Safe for everyone

Intended: all faction land is safe for everyone. TODO: visitors on another faction's land get reduced privileges (to be defined).

## Cap

The default curve is 1200 + 400 per member with `MaxRadius` 2800, so land grows up to 4 members (a full faction). All three values are `Curve` in `DefaultGame.ini`.

Growth clamps with `AGFGameState::MaxPerFaction` only (default 4; 0 = no clamp). `AGameSession::MaxPlayers` is not read. The live session is null on clients, so that read was 0 there and would not match the server. It is not replicated onto the territory map.

## Overlap rule

Closest horizontal center wins. Distances within 1 cm are a tie: more effective members, then the case-insensitive smaller faction id. Overlap stays owned. It does not become neutral.
