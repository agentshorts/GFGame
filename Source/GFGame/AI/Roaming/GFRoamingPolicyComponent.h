#pragma once

// Actor component a dino wears. Queries go to FGFRoamingPolicy; the shared target lives on a
// FGFRevengeGangBoard (usually the one UGFRoamingSubsystem keeps across the map load).
// UHT includes this header by filename. AI/Roaming must be on GFGame's private include path
// (see HANDOFF_feature_revenge-gang-roaming.md). Do not add that path from GFCompanion.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GFRoamingPolicy.h"
#include "GFRoamingPolicyComponent.generated.h"

class AGFDino;
class UGFTamingComponent;

UCLASS(ClassGroup = (GF), meta = (BlueprintSpawnableComponent))
class GFGAME_API UGFRoamingPolicyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGFRoamingPolicyComponent();

	/**
	 * Adds the component if the dino does not already have one, then copies species off AGFDino.
	 * A valid companion id becomes the member id. Otherwise MemberId is kept when the caller set one.
	 * Species is never used as an id: two wild dinos of the same species would not be unique.
	 */
	static UGFRoamingPolicyComponent* AttachToDino(AGFDino* Dino, const FGFSafeCircles& Home, FName HomeMap, FName MemberId = NAME_None);

	void BindDino(const AGFDino* Dino);
	void SetHome(const FGFSafeCircles& Home, FName HomeMap);
	void SetSpecies(FName InSpecies);
	void SetMemberId(FName InId);
	void SetRevengeGang(bool bGang);

	UFUNCTION(BlueprintPure, Category = "GF|Roaming")
	bool CanEnterZone(FName ZoneId) const;

	UFUNCTION(BlueprintPure, Category = "GF|Roaming")
	bool CanEnterMap(FName MapId) const;

	bool CanEnter(const FGFRoamZone& Zone) const { return Policy.CanEnter(Zone); }
	bool CanEnter(const FGFRoamMap& Map) const { return Policy.CanEnter(Map); }
	bool CanPathTo(const FVector& Goal) const { return Policy.CanPathTo(Goal); }
	bool CanFollow(const FGFRoamZone& Zone, const FGFRoamMap& Map) const { return Policy.CanFollow(Zone, Map); }
	FVector ClampFollowGoal(const FVector& Goal) const { return Policy.ClampFollowGoal(Goal); }
	FGFFollowDecision DecideFollow(const FVector& RawGoal, const FGFRoamZone& Zone, const FGFRoamMap& Map) const
	{
		return Policy.DecideFollow(RawGoal, Zone, Map);
	}

	bool IsRevengeGang() const { return Policy.IsRevengeGang(); }
	FName GetSpecies() const { return Policy.GetSpecies(); }
	FName GetMemberId() const { return Policy.GetMemberId(); }
	const FGFRoamingPolicy& GetPolicy() const { return Policy; }

	/**
	 * Puts this dino on the board and marks it revenge-gang. MaxPlayers <= 0 means no cap.
	 * Pass GFRoamingLimits::MaxPlayers (AGameSession::MaxPlayers).
	 */
	bool JoinGang(const TSharedPtr<FGFRevengeGangBoard>& InBoard, int32 MaxPlayers, FName& OutCode);

	/** Members pick up map access. Anyone else staying on this board remains home-leashed. */
	bool AdoptBoard(const TSharedPtr<FGFRevengeGangBoard>& InBoard);

	/** Server (or an unregistered component): copies Taming->GetTargetKey() onto the board. */
	bool SetGangTargetFromTaming(const UGFTamingComponent* Taming);

	FString GetGangTargetKey() const;
	TSharedPtr<FGFRevengeGangBoard> GetBoard() const { return Board; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Roaming")
	FName HomeZoneId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Roaming")
	FName HomeMapId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Roaming")
	FName Species;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Roaming")
	FName MemberId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Roaming")
	bool bRevengeGang = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GF|Roaming")
	FString GangTargetKey;

private:
	void SyncView();

	FGFRoamingPolicy Policy;
	TSharedPtr<FGFRevengeGangBoard> Board;
};
