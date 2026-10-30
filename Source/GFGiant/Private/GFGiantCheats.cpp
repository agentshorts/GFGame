#include "GFGiantCheats.h"
#include "GFGiantDirector.h"
#include "GFGiantEventComponent.h"
#include "GFGiantTypes.h"
#include "GFLog.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	bool IsClientWorld(const UWorld* World)
	{
		return World && World->GetNetMode() == NM_Client;
	}

	FString StateName(EGFGiantState State)
	{
		if (const UEnum* Enum = StaticEnum<EGFGiantState>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(State));
		}
		return FString::FromInt(static_cast<int32>(State));
	}

	double RealSecondsLeft(const UGFGiantEventComponent& Event)
	{
		const FGFGiantSave& Save = Event.GetSave();
		if (Save.LastEventUtcTicks == 0)
		{
			return Event.Tuning.MinRealSecondsBetween;
		}
		const double Elapsed = double(Event.NowUtcTicks() - Save.LastEventUtcTicks) / FGFGiantRules::TicksPerSecond;
		return FMath::Max(0.0, Event.Tuning.MinRealSecondsBetween - Elapsed);
	}

	FString NextAuto(double RealLeft, double PlayLeft, int32 Players, bool bExposed)
	{
		if (PlayLeft > 0.0 && Players <= 0)
		{
			return TEXT("needs a player");
		}
		const double Seconds = Players > 0 ? FMath::Max(RealLeft, PlayLeft / double(Players)) : RealLeft;
		if (Seconds <= 0.0 && !bExposed)
		{
			return TEXT("needs someone outside safety");
		}
		return FString::Printf(TEXT("%.0fs"), Seconds);
	}

	FString PlaytestText(const UGFGiantEventComponent& Event)
	{
		if (Event.PlaytestFirstEventSeconds <= 0.f)
		{
			return TEXT("off");
		}
		if (Event.EventsStarted > 0)
		{
			return TEXT("done");
		}
		if (Event.PlaytestSecondsSinceFirstPlayer < 0.0)
		{
			return FString::Printf(TEXT("armed %.0fs, waiting for a player"), Event.PlaytestFirstEventSeconds);
		}
		const double Left = FMath::Max(0.0, double(Event.PlaytestFirstEventSeconds) - Event.PlaytestSecondsSinceFirstPlayer);
		return FString::Printf(TEXT("%.0fs left"), Left);
	}
}

FGFGiantPlaytestClock FGFGiantPlaytest::Advance(const FGFGiantPlaytestClock& In, double Dt)
{
	FGFGiantPlaytestClock Out = In;
	if (In.PlaytestFirstEventSeconds <= 0.f || In.bSessionEventStarted)
	{
		return Out;
	}
	const double Step = FMath::Max(0.0, Dt);
	if (In.SecondsSinceFirstPlayer < 0.0)
	{
		Out.SecondsSinceFirstPlayer = In.bPlayerIn ? Step : -1.0;
		return Out;
	}
	Out.SecondsSinceFirstPlayer = In.SecondsSinceFirstPlayer + Step;
	return Out;
}

bool FGFGiantPlaytest::IsFirstEventDue(const FGFGiantPlaytestClock& In)
{
	return In.PlaytestFirstEventSeconds > 0.f
		&& !In.bSessionEventStarted
		&& In.bPlayerIn
		&& In.SecondsSinceFirstPlayer >= double(In.PlaytestFirstEventSeconds);
}

bool FGFGiantPlaytest::HoldsOffNormalClock(const FGFGiantPlaytestClock& In)
{
	return In.PlaytestFirstEventSeconds > 0.f && !In.bSessionEventStarted;
}

FString GFGiantCheats::Start(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.Start failed (no world)."));
		return TEXT("no world");
	}
	if (IsClientWorld(World))
	{
		return TEXT("client");
	}
	UGFGiantEventComponent* Event = AGFGiantDirector::GetEvent(World, true);
	if (!Event)
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.Start failed (no director)."));
		return TEXT("no director");
	}
	if (Event->bEventActive)
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.Start failed (an event is already running)."));
		return TEXT("already running");
	}
	if (Event->GatherPlayers().Num() == 0)
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.Start failed (nobody is in)."));
		return TEXT("nobody in");
	}
	if (!Event->ServerStartEvent())
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.Start failed."));
		return TEXT("failed");
	}
	return FString();
}

FString GFGiantCheats::End(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.End failed (no world)."));
		return TEXT("no world");
	}
	if (IsClientWorld(World))
	{
		return TEXT("client");
	}
	UGFGiantEventComponent* Event = AGFGiantDirector::GetEvent(World, false);
	if (!Event)
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.End failed (no director)."));
		return TEXT("no director");
	}
	if (!Event->ServerEndEvent())
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.End failed (no event is running)."));
		return TEXT("no event");
	}
	return FString();
}

FString GFGiantCheats::Status(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogGF, Warning, TEXT("Giant: gf.Giant.Status failed (no world)."));
		return TEXT("no world");
	}
	if (IsClientWorld(World))
	{
		return TEXT("client");
	}
	UGFGiantEventComponent* Event = AGFGiantDirector::GetEvent(World, false);
	if (!Event)
	{
		UE_LOG(LogGF, Log, TEXT("Giant: status: no director."));
		return TEXT("no director");
	}
	const FGFGiantSave& Save = Event->GetSave();
	const int32 Players = Event->GatherPlayers().Num();
	const double RealLeft = RealSecondsLeft(*Event);
	const double PlayLeft = FMath::Max(0.0, Event->Tuning.MinPlaySecondsSinceLast - Save.PlaySecondsSinceLast);
	const bool bExposed = FGFGiantRules::AnyoneOutside(Event->GetLandZone(), Event->GatherPlayers(), Event->GetNoEntry());
	const FString State = Event->bEventActive ? StateName(Event->GetSim().State) : FString(TEXT("Idle"));
	const FString Target = Event->bEventActive && !Event->GetSim().TargetKey.IsEmpty() ? Event->GetSim().TargetKey : FString(TEXT("-"));
	const FString Line = FString::Printf(
		TEXT("Giant: status active=%d state=%s target=%s players=%d realLeft=%.0f playLeft=%.0f next=%s playtest=%s"),
		Event->bEventActive ? 1 : 0,
		*State,
		*Target,
		Players,
		RealLeft,
		PlayLeft,
		*NextAuto(RealLeft, PlayLeft, Players, bExposed),
		*PlaytestText(*Event));
	UE_LOG(LogGF, Log, TEXT("%s"), *Line);
	return Line;
}

static void GiantStartCmd(const TArray<FString>&, UWorld* World)
{
	GFGiantCheats::Start(World);
}

static void GiantEndCmd(const TArray<FString>&, UWorld* World)
{
	GFGiantCheats::End(World);
}

static void GiantStatusCmd(const TArray<FString>&, UWorld* World)
{
	GFGiantCheats::Status(World);
}

static FAutoConsoleCommandWithWorldAndArgs GGiantStart(
	TEXT("gf.Giant.Start"),
	TEXT("Start Big Grumble now. Server or standalone; ignored on clients. This is the playtest path (PlaytestFirstEventSeconds stays 0)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiantStartCmd));

static FAutoConsoleCommandWithWorldAndArgs GGiantEnd(
	TEXT("gf.Giant.End"),
	TEXT("End the current Big Grumble event (destroys the body). Server or standalone; ignored on clients."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiantEndCmd));

static FAutoConsoleCommandWithWorldAndArgs GGiantStatus(
	TEXT("gf.Giant.Status"),
	TEXT("Log Big Grumble's state, target, time to the next auto trigger, and how many players are counted. Server or standalone; ignored on clients."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiantStatusCmd));
