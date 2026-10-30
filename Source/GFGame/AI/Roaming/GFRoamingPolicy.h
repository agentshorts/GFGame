#pragma once

// Per-dino leash plus the shared revenge-gang roster.
// Home dinos path and follow only inside their home zone and home map.
// Revenge-gang members may enter any zone and any map, including the MOBA (UGFMobaGateSubsystem)
// and Beat Hop (UGFBeatHopGateSubsystem). Their target lives on the board and is copied into
// FGFRoamingHandoff when the map changes.

#include "GFRoamingTypes.h"

class GFGAME_API FGFRoamingPolicy
{
public:
	void SetHome(const FGFDinoHomeZone& InHome, FName InHomeMap);
	void SetSpecies(FName InSpecies) { Species = InSpecies; }
	void SetMemberId(FName InId) { MemberId = InId; }
	void SetRevengeGang(bool bGang) { bRevengeGang = bGang; }

	const FGFDinoHomeZone& GetHome() const { return Home; }
	FName GetHomeMap() const { return HomeMap; }
	FName GetSpecies() const { return Species; }
	FName GetMemberId() const { return MemberId; }
	bool IsRevengeGang() const { return bRevengeGang; }

	/** Home dinos: only their zone. Gang: every zone. A dino with no home zone is refused. */
	bool CanEnter(const FGFRoamZone& Zone) const;
	/** Home dinos: only their map. Gang: every map, including ones this module does not list. */
	bool CanEnter(const FGFRoamMap& Map) const;
	/** Gang: any point. Home dinos: only a point inside the home circle. */
	bool CanPathTo(const FVector& Goal) const;
	/** Both the zone and the map, so a matching circle on another map is still refused. */
	bool CanFollow(const FGFRoamZone& Zone, const FGFRoamMap& Map) const;
	/** Gang: Goal unchanged. Home dinos: Goal pulled back onto the home circle. */
	FVector ClampFollowGoal(const FVector& Goal) const;
	/**
	 * Home dinos do not step into another zone or map. Inside their own, a goal past the circle
	 * becomes the point on the circle. The gang follows the raw goal.
	 */
	FGFFollowDecision DecideFollow(const FVector& RawGoal, const FGFRoamZone& Zone, const FGFRoamMap& Map) const;

private:
	FGFDinoHomeZone Home;
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

	void SetTarget(const FGFGangTarget& In) { Target = In; }
	const FGFGangTarget& GetTarget() const { return Target; }

	bool Contains(FName DinoId) const;
	const TArray<FGFRoamingMember>& GetMembers() const { return Members; }

	/**
	 * Adds a member until the roster reaches GFRoamingLimits::MaxSessionPlayers(SeatsPerFaction).
	 * The same id again returns true with Already and does not take a second seat.
	 */
	bool TryAdd(FName DinoId, FName Species, int32 SeatsPerFaction, FName& OutCode);

	/** bPending when a target is set. Copies the roster; does not clear this board. */
	FGFRoamingHandoff ExportHandoff(FName ToMap) const;
	/** Applies Record onto this board and clears Record.bPending. False when nothing is pending. */
	bool ConsumeHandoff(FGFRoamingHandoff& Record);

private:
	FName CurrentMap = NAME_None;
	FGFGangTarget Target;
	TArray<FGFRoamingMember> Members;
};
