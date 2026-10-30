#pragma once

// Per-dino leash plus the shared revenge-gang roster.
// Home dinos path and follow only inside their FGFSafeCircles and home map.
// Revenge-gang members may enter any zone and any map, including the MOBA (UGFMobaGateSubsystem)
// and Beat Hop (UGFBeatHopGateSubsystem). Their target is the taming target key, copied into
// FGFRoamingHandoff when that player changes maps.

#include "GFRoamingTypes.h"

class GFGAME_API FGFRoamingPolicy
{
public:
	void SetHome(const FGFSafeCircles& InHome, FName InHomeMap);
	void SetSpecies(FName InSpecies) { Species = InSpecies; }
	void SetMemberId(FName InId) { MemberId = InId; }
	void SetRevengeGang(bool bGang) { bRevengeGang = bGang; }

	const FGFSafeCircles& GetHome() const { return Home; }
	FName GetHomeMap() const { return HomeMap; }
	FName GetSpecies() const { return Species; }
	FName GetMemberId() const { return MemberId; }
	bool IsRevengeGang() const { return bRevengeGang; }

	/** Home dinos: a zone named by one of their home circles. Gang: every zone. No matching circle, refused. */
	bool CanEnter(const FGFRoamZone& Zone) const;
	/** Home dinos: only their map. Gang: every map, including ones this module does not list. */
	bool CanEnter(const FGFRoamMap& Map) const;
	/** Gang: any point. Home dinos: only a point FGFSafeCircles treats as inside. */
	bool CanPathTo(const FVector& Goal) const;
	/** Both the zone and the map, so a matching circle on another map is still refused. */
	bool CanFollow(const FGFRoamZone& Zone, const FGFRoamMap& Map) const;
	/** Gang: Goal unchanged. Home dinos: Goal pulled back with ClosestSafePoint. */
	FVector ClampFollowGoal(const FVector& Goal) const;
	/**
	 * Home dinos do not step into another zone or map. Inside their own, a goal past the circles
	 * becomes the nearest point on a circle. The gang follows the raw goal.
	 */
	FGFFollowDecision DecideFollow(const FVector& RawGoal, const FGFRoamZone& Zone, const FGFRoamMap& Map) const;

private:
	FGFSafeCircles Home;
	FName HomeMap = NAME_None;
	FName Species = NAME_None;
	FName MemberId = NAME_None;
	bool bRevengeGang = false;
};

/** Shared roster and target. Not an actor, so a hand-off value can outlive the map. */
class GFGAME_API FGFRevengeGangBoard
{
public:
	void SetMap(FName MapId) { CurrentMap = MapId; }
	FName GetMap() const { return CurrentMap; }

	void SetTargetKey(const FString& InKey) { TargetKey = InKey; }
	const FString& GetTargetKey() const { return TargetKey; }
	bool HasTarget() const { return !TargetKey.IsEmpty(); }

	bool Contains(FName DinoId) const;
	const TArray<FGFRoamingMember>& GetMembers() const { return Members; }

	/**
	 * Adds a member. MaxPlayers <= 0 means no cap. The same id again returns true with Already
	 * and does not take a second seat.
	 */
	bool TryAdd(FName DinoId, FName Species, int32 MaxPlayers, FName& OutCode);

	/** bPending when a target key is set. Copies the roster; does not clear this board. */
	FGFRoamingHandoff ExportHandoff(FName ToMap) const;
	/** Applies Record onto this board and clears Record.bPending. False when nothing is pending. */
	bool ConsumeHandoff(FGFRoamingHandoff& Record);

private:
	FName CurrentMap = NAME_None;
	FString TargetKey;
	TArray<FGFRoamingMember> Members;
};
