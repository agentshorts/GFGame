#include "GFBeatHopMapGates.h"
#include "GFBeatHopCourse.h"
#include "GFBeatHopMaps.h"
#include "GFReturnToHub.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace GFBeatHopMapGatesPrivate
{
	static const FName BeatHopId(TEXT("BeatHop"));

	static AGFReturnGate* FindTagged(UWorld* World, FName Tag)
	{
		for (TActorIterator<AGFReturnGate> It(World); It; ++It)
		{
			if (It->GetModeId() == BeatHopId && It->ActorHasTag(Tag))
			{
				return *It;
			}
		}
		return nullptr;
	}

	static AGFReturnGate* Canonical(UWorld* World)
	{
		for (TActorIterator<AGFReturnGate> It(World); It; ++It)
		{
			if (It->GetModeId() != BeatHopId)
			{
				continue;
			}
			if (It->ActorHasTag(GFBeatHopMapGates::StartTag()) || It->ActorHasTag(GFBeatHopMapGates::FinishTag()))
			{
				continue;
			}
			return *It;
		}
		return nullptr;
	}

	static AGFReturnGate* Place(UWorld* World, AGFReturnGate* Source, FName Tag, const FVector& At)
	{
		if (AGFReturnGate* Existing = FindTagged(World, Tag))
		{
			Existing->Configure(Source->GetModeId(), Source->GetRimeCenter(), Source->IsMapTravel());
			Existing->SetActorLocation(At);
			Existing->ApplyVisuals();
			return Existing;
		}
		const FTransform Xform(FRotator::ZeroRotator, At);
		AGFReturnGate* Gate = World->SpawnActorDeferred<AGFReturnGate>(
			AGFReturnGate::StaticClass(), Xform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Gate)
		{
			return nullptr;
		}
		Gate->Configure(Source->GetModeId(), Source->GetRimeCenter(), Source->IsMapTravel());
		Gate->Tags.AddUnique(Tag);
		Gate->FinishSpawning(Xform);
		Gate->ApplyVisuals();
		return Gate;
	}

	static FVector Beside(const AGFBeatHopCourse& Course, const USceneComponent& Comp, const FVector& LocalOffset)
	{
		const FVector Local = Course.GetActorTransform().InverseTransformPosition(Comp.GetComponentLocation());
		return Course.GetActorTransform().TransformPosition(Local + LocalOffset);
	}
}

FName GFBeatHopMapGates::StartTag()
{
	static const FName Tag(TEXT("BeatHop.Return.Start"));
	return Tag;
}

FName GFBeatHopMapGates::FinishTag()
{
	static const FName Tag(TEXT("BeatHop.Return.Finish"));
	return Tag;
}

void GFBeatHopMapGates::Refresh(AGFBeatHopCourse* Course)
{
	if (!Course || !Course->HasAuthority())
	{
		return;
	}
	UWorld* World = Course->GetWorld();
	if (!World)
	{
		return;
	}
	AGFReturnGate* Home = GFBeatHopMapGatesPrivate::Canonical(World);
	if (!Home)
	{
		return;
	}
	if (const UStaticMeshComponent* Start = Course->GetStartGate())
	{
		const FVector At = GFBeatHopMapGatesPrivate::Beside(*Course, *Start, FVector(0.0, 350.0, 0.0));
		GFBeatHopMapGatesPrivate::Place(World, Home, StartTag(), At);
	}
	if (const UStaticMeshComponent* Finish = Course->GetFinishGate())
	{
		const FVector At = GFBeatHopMapGatesPrivate::Beside(*Course, *Finish, FVector(220.0, 350.0, 0.0));
		GFBeatHopMapGatesPrivate::Place(World, Home, FinishTag(), At);
	}
}
