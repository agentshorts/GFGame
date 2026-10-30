# HANDOFF feature/faction-territory

Territory logic stays in `Source/GFGame/Territory/`. `Config/DefaultGame.ini` gained only the `FactionLayout` anchors. `GFGame.Build.cs` was not edited.

## What the safe query uses

`IsSafeFor` reads `FGFSocialPlayer::Faction` and matches it to the faction that owns the point. `bHost` is copied from `AGFPlayerState` onto that struct and does not add land. A player with no faction is not safe on faction land, in the neutral disc, or in the wild.

`CopySafeRegions` does not bake a disc down to one member. `Members` stays the effective head count, `BaseRadius` is the one-member radius, and `RadiusPerExtraMember` is the curve step, so `FGFSafeRegion::Radius()` equals base + per member × N.

## Build.cs (not applied)

`Territory/Tests/GFTerritoryTests.cpp` includes `GFTerritoryMap.h` and `GFSocialTypes.h`. Quoted includes from that folder do not see `Territory/`, and this module does not depend on GFSocial yet. In `Source/GFGame/GFGame.Build.cs`:

```csharp
PrivateDependencyModuleNames.AddRange(new string[] { "GFSocial" });
PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Territory"));
```

`GFSocial` does not depend on `GFGame`, so the private dependency does not cycle.

## Faction-aware safe-zone query (not applied)

Do not change `UGFSocialComponent::IsPlayerSafe`, and do not push discs into `UGFGiantEventComponent::SetSafeRegions`. Add a defaulted method on `IGFSafeZoneQuery` in `Source/GFCore/Public/GFSafeZone.h` instead. `FGFSafeCircles` and the giant's half-plane test keep today's union. `FGFTerritoryMap` already implements the same signature and should be marked `override` once the base method exists:

```cpp
/** Own-faction safety. Default: any safe land counts for a factioned player. */
virtual bool IsSafeFor(FName FactionId, const FVector& Location) const
{
	return !FactionId.IsNone() && IsInsideSafe(Location);
}
```

Callers that need own-land safety, including social, should call `IsSafeFor(Player.Faction, Location)` on the current `IGFSafeZoneQuery`. Territory's override returns true only when `GetZoneAt` is that faction. The giant's herding can keep using `IsInsideSafe` (the union, neutral excluded).

## Cap

Growth clamps with `AGFGameState::MaxPerFaction` only (0 = no clamp, which is the class default on staging). `AGameSession::MaxPlayers` is not read. The live session is null on clients, so that read was 0 there and would not match the server. It is not replicated onto the territory map.

## Overlap rule

Closest horizontal center wins. Distances within 1 cm are a tie: more effective members, then the case-insensitive smaller faction id. Overlap stays owned. It does not become neutral.
