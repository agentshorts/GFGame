#include "GFDinoSpellHit.h"
#include "GFBlockIntegritySubsystem.h"
#include "GFDino.h"
#include "GFMagicComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	void HitDinos(UGFMagicComponent* Caster, const FVector& Origin, const FVector& Dir, float Range, float HalfAngleDeg, float Damage)
	{
		if (!Caster || Damage <= 0.f || Range <= 0.f)
		{
			return;
		}
		UWorld* World = Caster->GetWorld();
		if (!World || !Caster->GetOwner() || !Caster->GetOwner()->HasAuthority())
		{
			return;
		}
		APawn* Attacker = nullptr;
		AController* Controller = nullptr;
		if (APlayerState* PS = Cast<APlayerState>(Caster->GetOwner()))
		{
			Attacker = PS->GetPawn();
			Controller = PS->GetPlayerController();
		}
		if (!Attacker)
		{
			Attacker = Cast<APawn>(Caster->GetOwner());
			Controller = Attacker ? Attacker->GetController() : nullptr;
		}
		AActor* Causer = Attacker ? static_cast<AActor*>(Attacker) : Caster->GetOwner();
		const FVector Aim = Dir.GetSafeNormal();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GFDinoSpell), false, Attacker);
		for (TActorIterator<AGFDino> It(World); It; ++It)
		{
			AGFDino* Dino = *It;
			if (!Dino || Dino == Attacker || !Dino->CanBeDamaged() || Dino->GetLife() != EGFDinoLife::Alive)
			{
				continue;
			}
			const FVector To = Dino->GetActorLocation() - Origin;
			const float Dist = To.Size();
			if (Dist > Range + 80.f)
			{
				continue;
			}
			if (Dist > 80.f && HalfAngleDeg < 179.f)
			{
				const float Dot = FVector::DotProduct(Aim, To.GetSafeNormal());
				const float Ang = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.f, 1.f)));
				const float Slack = FMath::RadiansToDegrees(FMath::Atan2(48.f, FMath::Max(Dist, 1.f)));
				if (Ang > HalfAngleDeg + Slack)
				{
					continue;
				}
			}
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, Origin, Dino->GetActorLocation(), ECC_Visibility, Params))
			{
				const AActor* Blocker = Hit.GetActor();
				if (Blocker && Blocker != Dino && Hit.Distance + 30.f < Dist)
				{
					continue;
				}
			}
			UGameplayStatics::ApplyDamage(Dino, Damage, Controller, Causer, nullptr);
		}
	}
}

void GFBindDinoSpellHits(UGFBlockIntegritySubsystem* Magic)
{
	if (!Magic)
	{
		return;
	}
	Magic->SetDinoHit(&HitDinos);
}
