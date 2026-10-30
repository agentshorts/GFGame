#pragma once

// World side of faction territory. Counts members from AGFGameState::FactionCount, caps growth with
// FGFTerritoryLimits (AGameSession::MaxPlayers and AGFGameState::MaxPerFaction), and answers
// IsSafeFor(Player, Location). Registers as an IGFSafeZoneProvider only while some faction owns land,
// so an empty world still uses the giant's default circle. The giant's own herding still uses its
// component until it is pointed at CopySafeRegions (see the handoff).

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
	UGFTerritorySubsystem();

	static UGFTerritorySubsystem* Get(const UObject* WorldContext);

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FGFTerritoryCurve Curve;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	FGFNeutralZone NeutralZone;

	/** Empty means the three default factions. Centers are tunable; member counts are filled from the game state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GF|Territory")
	TArray<FGFTerritoryFaction> FactionLayout;

	UFUNCTION(BlueprintCallable, Category = "GF|Territory")
	FGFTerritoryZoneHit GetZoneAt(FVector Location);

	/** Own faction land only. The query other systems should call. */
	UFUNCTION(BlueprintCallable, Category = "GF|Territory")
	bool IsSafeFor(const AGFPlayerState* Player, FVector Location);

	/** Tamed: the owner's faction. Wild (anything but EGFTameState::Tamed): never safe. */
	bool IsSafeForDino(const AGFDino* Dino, const FVector& Location);

	/** Discs for UGFGiantEventComponent::SetSafeRegions. Radius is already baked into each FGFSafeRegion. */
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
	static FGFTerritorySubject SubjectFor(const AGFPlayerState* Player);
	FGFTerritorySubject SubjectFor(const AGFDino* Dino) const;

	FGFTerritoryMap Map;
};
