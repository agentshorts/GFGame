#pragma once

// World side of faction territory. Counts members from AGFGameState::FactionCount, caps growth with
// AGFGameState::MaxPerFaction, and answers IsSafeFor(Player, Location) through FGFSocialPlayer::Faction.
// Registers once with UGFSafeZoneSubsystem from Initialize. The giant keeps its own circles.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GFPlayerState.h"
#include "GFTerritoryMap.h"
#include "GFTerritorySubsystem.generated.h"

class AGFDino;

UCLASS(Config = Game)
class GFGAME_API UGFTerritorySubsystem : public UTickableWorldSubsystem, public IGFSafeZoneProvider
{
	GENERATED_BODY()

public:
	static UGFTerritorySubsystem* Get(const UObject* WorldContext);

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FGFTerritoryCurve Curve;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FGFNeutralZone NeutralZone;

	/** Anchors from DefaultGame.ini. Member counts are filled from the game state. Empty means no land. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	TArray<FGFTerritoryFaction> FactionLayout;

	UFUNCTION(BlueprintCallable, Category = "GF|Territory")
	FGFTerritoryZoneHit GetZoneAt(FVector Location);

	/** Own faction land only. The query other systems should call. */
	UFUNCTION(BlueprintCallable, Category = "GF|Territory")
	bool IsSafeFor(const AGFPlayerState* Player, FVector Location);

	/** Tamed: the owner's faction. Wild (anything but EGFTameState::Tamed): never safe. */
	bool IsSafeForDino(const AGFDino* Dino, const FVector& Location);

	/** Owned discs. See FGFTerritoryMap::CopySafeRegions. */
	TArray<FGFSafeRegion> CopySafeRegions();

	const FGFTerritoryMap& GetMap() const { return Map; }

	virtual const IGFSafeZoneQuery* GetSafeZoneQuery() const override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void RefreshFromWorld();
	void Publish();
	FGFSocialPlayer SubjectFor(const AGFPlayerState* Player) const;
	FGFSocialPlayer SubjectFor(const AGFDino* Dino) const;

	FGFTerritoryMap Map;
	bool bPublished = false;
};
