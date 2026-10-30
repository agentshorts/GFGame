#include "GFRoamingPolicyComponent.h"
#include "GFDino.h"
#include "GFTamingComponent.h"
#include "GFPlayerState.h"

UGFRoamingPolicyComponent::UGFRoamingPolicyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UGFRoamingPolicyComponent* UGFRoamingPolicyComponent::AttachToDino(AGFDino* Dino, const FGFDinoHomeZone& Home, FName HomeMap, FName InMemberId)
{
	if (!Dino)
	{
		return nullptr;
	}
	UGFRoamingPolicyComponent* Comp = Dino->FindComponentByClass<UGFRoamingPolicyComponent>();
	if (!Comp)
	{
		Comp = NewObject<UGFRoamingPolicyComponent>(Dino, TEXT("Roaming"));
		Comp->RegisterComponent();
	}
	if (!InMemberId.IsNone())
	{
		Comp->SetMemberId(InMemberId);
	}
	Comp->BindDino(Dino);
	Comp->SetHome(Home, HomeMap);
	return Comp;
}

void UGFRoamingPolicyComponent::BindDino(const AGFDino* Dino)
{
	if (!Dino || !Dino->Taming)
	{
		return;
	}
	Policy.SetSpecies(Dino->Taming->Species);
	if (Dino->Taming->CompanionId.IsValid())
	{
		Policy.SetMemberId(FName(*Dino->Taming->CompanionId.ToString(EGuidFormats::Digits)));
	}
	else if (Policy.GetMemberId().IsNone() && !Dino->Taming->Species.IsNone())
	{
		Policy.SetMemberId(Dino->Taming->Species);
	}
	SyncView();
}

void UGFRoamingPolicyComponent::SetHome(const FGFDinoHomeZone& Home, FName HomeMap)
{
	Policy.SetHome(Home, HomeMap);
	SyncView();
}

void UGFRoamingPolicyComponent::SetSpecies(FName InSpecies)
{
	Policy.SetSpecies(InSpecies);
	SyncView();
}

void UGFRoamingPolicyComponent::SetMemberId(FName InId)
{
	Policy.SetMemberId(InId);
	SyncView();
}

void UGFRoamingPolicyComponent::SetRevengeGang(bool bGang)
{
	Policy.SetRevengeGang(bGang);
	SyncView();
}

bool UGFRoamingPolicyComponent::CanEnterZone(FName ZoneId) const
{
	return CanEnter(FGFRoamZone(ZoneId));
}

bool UGFRoamingPolicyComponent::CanEnterMap(FName MapId) const
{
	return CanEnter(FGFRoamMap(MapId));
}

bool UGFRoamingPolicyComponent::JoinGang(const TSharedPtr<FGFRevengeGangBoard>& InBoard, int32 SeatsPerFaction, FName& OutCode)
{
	if (!InBoard || Policy.GetMemberId().IsNone())
	{
		OutCode = GFRoamingCodes::NoDino();
		return false;
	}
	if (!InBoard->TryAdd(Policy.GetMemberId(), Policy.GetSpecies(), SeatsPerFaction, OutCode))
	{
		return false;
	}
	if (InBoard->GetMap().IsNone())
	{
		InBoard->SetMap(Policy.GetHomeMap());
	}
	Board = InBoard;
	Policy.SetRevengeGang(true);
	SyncView();
	return true;
}

bool UGFRoamingPolicyComponent::AdoptBoard(const TSharedPtr<FGFRevengeGangBoard>& InBoard)
{
	Board = InBoard;
	const bool bMember = Board.IsValid() && Board->Contains(Policy.GetMemberId());
	Policy.SetRevengeGang(bMember);
	SyncView();
	return bMember;
}

bool UGFRoamingPolicyComponent::SetGangTargetFromPlayer(const AGFPlayerState* PS)
{
	if (!Policy.IsRevengeGang() || !Board || !PS || PS->PlayerKey.IsEmpty())
	{
		return false;
	}
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		return false;
	}
	Board->SetTarget(FGFGangTarget::FromPlayer(*PS));
	SyncView();
	return true;
}

FGFGangTarget UGFRoamingPolicyComponent::GetGangTarget() const
{
	return Board ? Board->GetTarget() : FGFGangTarget();
}

void UGFRoamingPolicyComponent::SyncView()
{
	HomeZoneId = Policy.GetHome().ZoneId;
	HomeMapId = Policy.GetHomeMap();
	Species = Policy.GetSpecies();
	MemberId = Policy.GetMemberId();
	bRevengeGang = Policy.IsRevengeGang();
	GangTargetKey = Board && Board->GetTarget().IsSet() ? Board->GetTarget().PlayerKey : FString();
}
