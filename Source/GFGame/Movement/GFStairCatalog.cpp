#include "GFStairCatalog.h"

#include "Engine/World.h"
#include "EngineUtils.h"

namespace GFStairCatalog
{
	TArray<FGFStairFlightSpec> HubFlights()
	{
		TArray<FGFStairFlightSpec> Out;
		// West of the Rime stand (0, 0). Outside the 2000 cm gate ring, the forest box
		// (X -3100..-1500, Y -1600..1600), the lava bridge, and the Kindle column at (-400, 400).
		auto Add = [&Out](const TCHAR* Id, int32 Steps, float Riser, float Tread, float Y)
		{
			FGFStairFlightSpec Spec;
			Spec.Id = Id;
			Spec.Steps = Steps;
			Spec.RiserCm = Riser;
			Spec.TreadCm = Tread;
			Spec.WidthCm = 180.f;
			Spec.Origin = FVector(-1200.f, Y, 0.f);
			Spec.YawDeg = 0.f;
			Out.Add(Spec);
		};
		Add(TEXT("Hub.Short"), 4, 20.f, 32.f, -2400.f);
		Add(TEXT("Hub.Shallow"), 6, 15.f, 36.f, -3100.f);
		Add(TEXT("Hub.Steep"), 5, 25.f, 32.f, -3800.f);
		return Out;
	}

	AGFStairFlight* SpawnFlight(UWorld* World, const FGFStairFlightSpec& Spec)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		const FTransform Xform(FRotator(0.f, Spec.YawDeg, 0.f), Spec.Origin);
		AGFStairFlight* Flight = World->SpawnActor<AGFStairFlight>(AGFStairFlight::StaticClass(), Xform, Params);
		if (!Flight)
		{
			return nullptr;
		}
		Flight->Spec = Spec;
		Flight->FinishSpawning(Xform);
		return Flight;
	}

	void SpawnHubFlights(UWorld* World)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			return;
		}
		for (TActorIterator<AGFStairFlight> It(World); It; ++It)
		{
			return;
		}
		for (const FGFStairFlightSpec& Spec : HubFlights())
		{
			SpawnFlight(World, Spec);
		}
	}
}
