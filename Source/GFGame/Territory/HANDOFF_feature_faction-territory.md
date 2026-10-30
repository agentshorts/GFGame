# HANDOFF feature/faction-territory

Territory code stays in `Source/GFGame/Territory/`. Nothing outside that folder was edited. The pieces below are the follow-ups that need another module or a config file.

## Already reused

- `AGFPlayerState::FactionId` and `bIsHost`. Host does not grant land.
- `AGFGameState::FactionCount` / `MaxPerFaction` for head count and the seat cap.
- `AGameSession::MaxPlayers` for the session player cap. The curve clamps to the tighter of those two. No player total is written in this folder.
- `FGFSafeRegion::Radius` for the disc size, and `FGFSafeCircles` for inside / closest-point / edge. Neutral is not part of that union.
- `AGFDino` plus `UGFTamingComponent`: tamed dinos use the owner's faction (`GetOwnerKey`); any other tame state is wild and not safe.
- `UGFSafeZoneSubsystem`: this subsystem registers as an `IGFSafeZoneProvider` only while some faction owns land. An empty world leaves the giant's default circle in place. Once someone owns land, territory is the newest provider (the union of owned discs; neutral is not included).

`UGFMobaGateSubsystem` is only the arena gate. It does not own land. Landlord plots (`FGFLandState`) are rent plots, not faction discs.

## Social safe check

`UGFSocialComponent::IsPlayerSafe` still asks `UGFSafeZoneSubsystem::IsInsideSafe`, which is the union of every faction's disc. Own-land safety is `UGFTerritorySubsystem::IsSafeFor`. Suggested replacement in `Source/GFSocial/Private/GFSocialComponent.cpp`:

```cpp
bool UGFSocialComponent::IsPlayerSafe(const AGFPlayerState* PS) const
{
	const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}
	if (UGFTerritorySubsystem* Territory = UGFTerritorySubsystem::Get(this))
	{
		return Territory->IsSafeFor(PS, Pawn->GetActorLocation());
	}
	const UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(this);
	return Zones && Zones->IsInsideSafe(Pawn->GetActorLocation());
}
```

That file would include `GFTerritorySubsystem.h`. `GFSocial.Build.cs` does not depend on `GFGame` today (Core, CoreUObject, Engine, NetCore, GFCore, GFNet). Add `GFGame` to its public dependencies. `GFGame` does not depend on `GFSocial`, so that edge does not cycle. Do not change `GFJail*`.

## Giant herding

`UGFGiantEventComponent` keeps its own circles until `SetSafeRegions` / `SetSafeZone`. Publishing through the safe-zone subsystem does not change herding. `GFGame` does not depend on `GFGiant`, so this folder cannot call `SetSafeRegions` yet.

Preferred: add `GFGiant` to `GFGame.Build.cs` `PublicDependencyModuleNames`, then from `UGFTerritorySubsystem::Tick` after `RefreshFromWorld`:

```cpp
#include "GFGiantEventComponent.h"
#include "UObject/UObjectIterator.h"

for (TObjectIterator<UGFGiantEventComponent> It; It; ++It)
{
	if (It->GetWorld() == GetWorld())
	{
		const TArray<FGFSafeRegion> Regions = CopySafeRegions();
		if (Regions.Num() > 0)
		{
			It->SetSafeRegions(Regions);
		}
	}
}
```

`CopySafeRegions` already bakes each radius into an `FGFSafeRegion` (one member, base = the territory radius) so the giant's extra-member formula does not shrink the disc.

## Config

Curve and neutral zone are `Config = Game` on `UGFTerritorySubsystem`. Optional `Config/DefaultGame.ini` (not added here):

```ini
[/Script/GFGame.GFTerritorySubsystem]
Curve=(BaseRadius=1200.000000,RadiusPerMember=400.000000,MaxRadius=3200.000000)
NeutralZone=(bEnabled=False,Center=(X=0.000000,Y=0.000000,Z=0.000000),Radius=800.000000)
```

No `GFGame.Build.cs` edit is required for these sources. UnrealBuildTool compiles every `.cpp` under the `GFGame` module, including `Territory/` and `Territory/Tests/`.

## Overlap rule

Closest horizontal center wins. Within 1 cm, more effective members wins, then the case-insensitive smaller faction id. Overlap stays owned by that one faction. It does not become neutral.
