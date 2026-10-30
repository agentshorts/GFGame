#include "GFDinoTypes.h"

// ---------------- Diet row ----------------

float FDinoDietRow::AcceptedGain(FName Food) const
{
	if (Food.IsNone())
	{
		return -1.f;
	}
	if (Food == GFLabItems::LabTreat && !Accepted.IsEmpty())
	{
		return FMath::RoundToFloat(MeanAcceptedGain() * GFLabItems::LabTreatMultiplier); // every species eats it
	}
	for (const FGFDietEntry& E : Accepted)
	{
		if (E.Food == Food)
		{
			return E.BondGain;
		}
	}
	return -1.f;
}

float FDinoDietRow::MeanAcceptedGain() const
{
	if (Accepted.IsEmpty())
	{
		return 0.f;
	}
	float Sum = 0.f;
	for (const FGFDietEntry& E : Accepted)
	{
		Sum += E.BondGain;
	}
	return Sum / Accepted.Num();
}

// ---------------- Journal ----------------

FGFDinoPreferenceRecord& FGFDiscoveredDinoPreferences::FindOrAdd(FName Species)
{
	for (FGFDinoPreferenceRecord& R : Records)
	{
		if (R.Species == Species)
		{
			return R;
		}
	}
	FGFDinoPreferenceRecord& New = Records.AddDefaulted_GetRef();
	New.Species = Species;
	return New;
}

const FGFDinoPreferenceRecord* FGFDiscoveredDinoPreferences::Find(FName Species) const
{
	return Records.FindByPredicate([Species](const FGFDinoPreferenceRecord& R) { return R.Species == Species; });
}

int32 FGFDiscoveredDinoPreferences::CountFacts() const
{
	int32 N = 0;
	for (const FGFDinoPreferenceRecord& R : Records)
	{
		N += (R.bSighted ? 1 : 0) + R.KnownAccepted.Num() + R.KnownFavorites.Num() + R.KnownRejected.Num() + R.Interested.Num() + R.Clues.Num();
	}
	return N;
}

// ---------------- Diet helpers ----------------

EGFFeedResult FGFDinoDiet::Classify(const FDinoDietRow& Row, FName Food)
{
	if (Row.IsFavourite(Food))
	{
		return EGFFeedResult::Favorite;
	}
	return Row.AcceptedGain(Food) >= 0.f ? EGFFeedResult::Accepted : EGFFeedResult::Rejected;
}

float FGFDinoDiet::BondGain(const FDinoDietRow& Row, FName Food)
{
	switch (Classify(Row, Food))
	{
	case EGFFeedResult::Favorite: return Row.FavouriteGain();
	case EGFFeedResult::Accepted: return Row.AcceptedGain(Food);
	default: return 0.f;
	}
}

int32 FGFDinoDiet::PickAggroTarget(const FDinoDietRow& Row, const TArray<FGFTamerView>& Players, float RadiusScale)
{
	int32 Best = INDEX_NONE;
	float BestDist = TNumericLimits<float>::Max();
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		const FGFTamerView& P = Players[i];
		const float Radius = Row.AggroRadius * RadiusScale * (P.bCrouching ? FGFTameMachine::CrouchScale : 1.f);
		if (!Row.Accepts(P.Held) && P.Distance <= Radius && P.Distance < BestDist)
		{
			Best = i;
			BestDist = P.Distance;
		}
	}
	return Best;
}

bool FGFDinoDiet::RecordDiscovery(FGFDiscoveredDinoPreferences& Book, FName Species, FName Food, EGFFeedResult Result)
{
	if (Food.IsNone())
	{
		return false;
	}
	FGFDinoPreferenceRecord& Rec = Book.FindOrAdd(Species);
	const bool bSightedNew = !Rec.bSighted;
	Rec.bSighted = true;
	bool bNew = false;
	switch (Result)
	{
	case EGFFeedResult::Favorite:
		bNew = !Rec.KnownFavorites.Contains(Food);
		Rec.KnownFavorites.AddUnique(Food);
		Rec.KnownAccepted.Remove(Food); // a star replaces a check
		break;
	case EGFFeedResult::Accepted:
		if (!Rec.KnownFavorites.Contains(Food))
		{
			bNew = !Rec.KnownAccepted.Contains(Food);
			Rec.KnownAccepted.AddUnique(Food);
		}
		break;
	default:
		bNew = !Rec.KnownRejected.Contains(Food);
		Rec.KnownRejected.AddUnique(Food);
		break;
	}
	if (Result != EGFFeedResult::Rejected)
	{
		Rec.Interested.Remove(Food);
	}
	return bNew || bSightedNew;
}

bool FGFDinoDiet::RecordSighting(FGFDiscoveredDinoPreferences& Book, FName Species)
{
	FGFDinoPreferenceRecord& Rec = Book.FindOrAdd(Species);
	const bool bNew = !Rec.bSighted;
	Rec.bSighted = true;
	return bNew;
}

bool FGFDinoDiet::RecordInterest(FGFDiscoveredDinoPreferences& Book, FName Species, FName Food)
{
	if (Food.IsNone())
	{
		return false;
	}
	FGFDinoPreferenceRecord& Rec = Book.FindOrAdd(Species);
	if (Rec.KnownAccepted.Contains(Food) || Rec.KnownFavorites.Contains(Food) || Rec.Interested.Contains(Food))
	{
		return false;
	}
	Rec.Interested.Add(Food);
	return true;
}

bool FGFDinoDiet::RecordClue(FGFDiscoveredDinoPreferences& Book, FName Species, FName ClueId)
{
	FGFDinoPreferenceRecord& Rec = Book.FindOrAdd(Species);
	if (Rec.Clues.Contains(ClueId))
	{
		return false;
	}
	Rec.Clues.Add(ClueId);
	return true;
}

int32 FGFDinoDiet::MergeInto(FGFDiscoveredDinoPreferences& To, const FGFDinoPreferenceRecord& From)
{
	const int32 Before = To.CountFacts();
	if (From.bSighted)
	{
		RecordSighting(To, From.Species);
	}
	for (FName F : From.KnownFavorites) { RecordDiscovery(To, From.Species, F, EGFFeedResult::Favorite); }
	for (FName F : From.KnownAccepted) { RecordDiscovery(To, From.Species, F, EGFFeedResult::Accepted); }
	for (FName F : From.KnownRejected) { RecordDiscovery(To, From.Species, F, EGFFeedResult::Rejected); }
	for (FName F : From.Interested) { RecordInterest(To, From.Species, F); }
	for (FName C : From.Clues) { RecordClue(To, From.Species, C); }
	return To.CountFacts() - Before;
}

bool FGFDinoDiet::UpdateCompletion(FGFDiscoveredDinoPreferences& Book, const FDinoDietRow& Row)
{
	FGFDinoPreferenceRecord& Rec = Book.FindOrAdd(Row.Species);
	if (Rec.bComplete)
	{
		return false;
	}
	for (const FGFDietEntry& E : Row.Accepted)
	{
		if (!Rec.KnownAccepted.Contains(E.Food))
		{
			return false;
		}
	}
	for (FName F : Row.Favourites)
	{
		if (!Rec.KnownFavorites.Contains(F))
		{
			return false;
		}
	}
	Rec.bComplete = true;
	return true;
}

// ---------------- Taming state machine ----------------

void FGFTameMachine::SetState(EGFTameState NewState)
{
	State = NewState;
	StateTime = 0.f;
	CalmTime = 0.f;
}

void FGFTameMachine::EndVisit()
{
	OffersThisVisit = 0;
	Aggravation = 0;
	TargetKey.Reset();
}

void FGFTameMachine::ForceHostile(const FString& Key)
{
	if (State != EGFTameState::Tamed && State != EGFTameState::Fled && State != EGFTameState::Hostile)
	{
		SetState(EGFTameState::Hostile);
		TargetKey = Key;
	}
}

bool FGFTameMachine::ApplyCalm(float Now, float Seconds)
{
	if (State == EGFTameState::Tamed || State == EGFTameState::Fled)
	{
		return false;
	}
	CalmUntil = Now + Seconds;
	Aggravation = 0;
	TargetKey.Reset();
	if (State == EGFTameState::Threat || State == EGFTameState::Hostile)
	{
		SetState(EGFTameState::Alert);
	}
	return true;
}

void FGFTameMachine::Flee()
{
	if (State == EGFTameState::Tamed)
	{
		return;
	}
	Bond = 0.f;
	StarterKey.Reset();
	Contributions.Reset();
	EndVisit();
	SetState(EGFTameState::Fled);
}

bool FGFTameMachine::Tick(const FDinoDietRow& Row, float Now, float Dt, const TArray<FGFTamerView>& Players)
{
	const EGFTameState Before = State;
	StateTime += Dt;
	if (State == EGFTameState::Tamed)
	{
		return false;
	}
	if (State == EGFTameState::Fled)
	{
		if (StateTime >= FledRecoverSeconds)
		{
			SetState(EGFTameState::Wild);
		}
		return State != Before;
	}

	bool bAnyPresent = false, bAnyInWarn = false, bThreatInWarn = false, bThreatInAggro = false, bThreatPresent = false;
	for (const FGFTamerView& P : Players)
	{
		const float S = P.bCrouching ? CrouchScale : 1.f;
		const bool bOk = Row.Accepts(P.Held);
		bAnyPresent |= P.Distance <= Row.CalmRadius() * S;
		bAnyInWarn |= P.Distance <= Row.WarnRadius() * S;
		bThreatPresent |= !bOk && P.Distance <= Row.CalmRadius() * S;
		bThreatInWarn |= !bOk && P.Distance <= Row.WarnRadius() * S;
		bThreatInAggro |= !bOk && P.Distance <= Row.AggroRadius * S;
	}
	if (IsCalmed(Now))
	{
		bThreatPresent = bThreatInWarn = bThreatInAggro = false; // Calming Mist: nobody looks like a threat
	}

	// Bond fades while nobody is around; at zero the tame attempt is forgotten.
	if (!bAnyPresent && Bond > 0.f)
	{
		Bond = FMath::Max(0.f, Bond - Row.BondDecayPerSec * Dt);
		if (Bond <= 0.f)
		{
			StarterKey.Reset();
			Contributions.Reset();
		}
	}

	auto PickKey = [&](float Scale) -> FString
	{
		const int32 I = FGFDinoDiet::PickAggroTarget(Row, Players, Scale);
		return I == INDEX_NONE ? FString() : Players[I].Key;
	};
	auto CalmTowardsWild = [&]()
	{
		CalmTime += Dt;
		if (CalmTime >= CalmAloneSeconds)
		{
			EndVisit();
			SetState(EGFTameState::Wild);
		}
	};

	switch (State)
	{
	case EGFTameState::Wild:
		if (bAnyInWarn)
		{
			SetState(EGFTameState::Alert);
		}
		break;

	case EGFTameState::Alert:
		if (bThreatInWarn)
		{
			SetState(EGFTameState::Threat);
			TargetKey = PickKey(1.25f);
		}
		else if (bAnyInWarn)
		{
			SetState(OffersThisVisit > 0 ? EGFTameState::Feeding : EGFTameState::Curious);
		}
		else
		{
			CalmTowardsWild();
		}
		break;

	case EGFTameState::Curious:
	case EGFTameState::Feeding:
		if (bThreatInAggro)
		{
			SetState(EGFTameState::Threat);
			TargetKey = PickKey(1.f);
		}
		else if (!bAnyPresent)
		{
			CalmTowardsWild();
		}
		else
		{
			CalmTime = 0.f;
		}
		break;

	case EGFTameState::Threat:
		if (!bThreatInWarn)
		{
			TargetKey.Reset();
			SetState(bAnyInWarn ? (OffersThisVisit > 0 ? EGFTameState::Feeding : EGFTameState::Curious) : EGFTameState::Alert);
		}
		else if (StateTime >= Row.ThreatSeconds && bThreatInAggro)
		{
			SetState(EGFTameState::Hostile);
			TargetKey = PickKey(1.f);
		}
		break;

	case EGFTameState::Hostile:
		if (StateTime >= FleeAfterHostileSeconds)
		{
			Flee();
			break;
		}
		if (bThreatPresent)
		{
			const FString Key = PickKey(1.875f); // anyone threatening inside CalmRadius
			if (!Key.IsEmpty())
			{
				TargetKey = Key;
			}
			CalmTime = 0.f;
		}
		else
		{
			CalmTime += Dt;
			if (CalmTime >= (bAnyInWarn ? CalmWithFoodSeconds : CalmAloneSeconds))
			{
				TargetKey.Reset();
				if (bAnyPresent)
				{
					SetState(EGFTameState::Alert);
				}
				else
				{
					EndVisit();
					SetState(EGFTameState::Wild);
				}
			}
		}
		break;

	default:
		break;
	}
	return State != Before;
}

EGFOfferResult FGFTameMachine::Offer(const FDinoDietRow& Row, float Now, const FString& Key, FName Food)
{
	if (State == EGFTameState::Tamed || State == EGFTameState::Fled || Key.IsEmpty())
	{
		return EGFOfferResult::NotTameable;
	}
	const EGFFeedResult Kind = FGFDinoDiet::Classify(Row, Food);
	if (State == EGFTameState::Hostile && Kind != EGFFeedResult::Favorite)
	{
		return EGFOfferResult::NotNow; // only a favourite calms an angry dino
	}
	if (Now - LastOfferTime < Row.OfferCooldown)
	{
		return EGFOfferResult::Cooldown;
	}
	if (OffersThisVisit >= Row.Fullness)
	{
		return EGFOfferResult::Full;
	}
	LastOfferTime = Now;

	if (Kind == EGFFeedResult::Rejected)
	{
		++Aggravation;
		if (Aggravation >= 2 && !IsCalmed(Now))
		{
			SetState(EGFTameState::Threat);
			TargetKey = Key;
		}
		return EGFOfferResult::Refused;
	}

	float Gain = FGFDinoDiet::BondGain(Row, Food);
	if (StarterKey.IsEmpty())
	{
		StarterKey = Key;
	}
	if (Key != StarterKey)
	{
		Gain *= HelperShare;
	}
	Bond = FMath::Min(MaxBond, Bond + Gain);
	Contributions.FindOrAdd(Key) += Gain;
	++OffersThisVisit;
	Aggravation = 0;
	TargetKey.Reset();
	if (Bond >= MaxBond)
	{
		OwnerKey = StarterKey;
		SetState(EGFTameState::Tamed);
	}
	else
	{
		SetState(EGFTameState::Feeding);
	}
	return Kind == EGFFeedResult::Favorite ? EGFOfferResult::Favourite : EGFOfferResult::Accepted;
}
