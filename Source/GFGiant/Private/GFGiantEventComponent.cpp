#include "GFGiantEventComponent.h"
#include "GFGiantDino.h"
#include "GFGiantBellyCam.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFDataTables.h"
#include "GFAttention.h"
#include "GFLog.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

const FName UGFGiantEventComponent::ModeName(TEXT("Giant"));

UGFGiantEventComponent::UGFGiantEventComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

bool UGFGiantEventComponent::IsServer() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

void UGFGiantEventComponent::NetUpdate()
{
	if (GetOwner())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void UGFGiantEventComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGFGiantEventComponent, SafeRegions);
	DOREPLIFETIME(UGFGiantEventComponent, bEventActive);
	DOREPLIFETIME(UGFGiantEventComponent, Giant);
	DOREPLIFETIME(UGFGiantEventComponent, Chat);
}

void UGFGiantEventComponent::BeginPlay()
{
	Super::BeginPlay();
	Lines = FGFGiantLines::Load();
	UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(this);
	if (Zones)
	{
		Zones->RegisterProvider(this); // server and clients (SafeRegions replicate)
	}
	// Shape: its own circles plus every other provider's land (faction territory), read live on each query.
	LandShape = MakeShared<FGFSafeZoneUnion>();
	LandShape->Own = &Circles;
	LandShape->Zones = Zones;
	LandShape->Skip = this;
	if (!ZoneOverride.IsValid())
	{
		SetSafeZone(LandShape);
	}
	if (const UDataTable* InfoTable = FGFDataTables::Get<FGFGiantInfoRow>(TEXT("DT_GiantDino")))
	{
		if (const FGFGiantInfoRow* Row = InfoTable->FindRow<FGFGiantInfoRow>(TEXT("Giant"), TEXT("Giant"), false))
		{
			Info = *Row;
		}
	}
	if (!IsServer())
	{
		return;
	}
	TauntHandle = FGFAttention::OnTaunt().AddUObject(this, &UGFGiantEventComponent::OnTaunt);
	if (!TunablesAsset.IsNull())
	{
		if (const UGFGiantTunables* Asset = TunablesAsset.LoadSynchronous())
		{
			Tuning = Asset->Tuning;
		}
	}
	DefaultFaint = NewObject<UGFFaintStub>(this);
	if (!FaintHandler.GetObject())
	{
		FaintHandler = DefaultFaint;
	}
	RefreshSafeZone(GatherPlayers().Num());
	if (AGFGameState* G = GS())
	{
		G->RegisterModeHandler(this); // restores the saved blob right away
	}
	FGFGiantRules::StartClockIfNeeded(Save, NowUtcTicks());
}

void UGFGiantEventComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (TauntHandle.IsValid())
	{
		FGFAttention::OnTaunt().Remove(TauntHandle);
		TauntHandle.Reset();
	}
	if (UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(this))
	{
		Zones->UnregisterProvider(this);
	}
	if (IsServer())
	{
		for (FBelly& B : Bellies)
		{
			FinishBelly(B, false);
		}
		Bellies.Reset();
		if (Giant)
		{
			Giant->Destroy();
			Giant = nullptr;
		}
		if (AGFGameState* G = GS())
		{
			G->UnregisterModeHandler(this);
		}
	}
	Super::EndPlay(Reason);
}

AGFGameState* UGFGiantEventComponent::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
}

int64 UGFGiantEventComponent::NowUtcTicks() const
{
	return FDateTime::UtcNow().GetTicks() + int64(DebugSecondsOffset * FGFGiantRules::TicksPerSecond);
}

void UGFGiantEventComponent::SetSafeRegions(const TArray<FGFSafeRegion>& Regions)
{
	bUseDefaultRegion = false;
	ZoneOverride = LandShape; // these circles plus land (only the circles before BeginPlay)
	SafeRegions = Regions;
	NetUpdate();
}

void UGFGiantEventComponent::SetSafeZone(TSharedPtr<IGFSafeZoneQuery> Zone)
{
	ZoneOverride = Zone;
}

const IGFSafeZoneQuery& UGFGiantEventComponent::GetSafeZone() const
{
	Circles.Regions = SafeRegions; // also the own part of LandShape
	if (ZoneOverride.IsValid())
	{
		return *ZoneOverride;
	}
	return Circles;
}

const IGFSafeZoneQuery& UGFGiantEventComponent::GetLandZone() const
{
	LandZone.Base = &GetSafeZone();
	LandZone.Lands = UGFSafeZoneSubsystem::Get(this);
	return LandZone;
}

TArray<FBox> UGFGiantEventComponent::GetNoEntry() const
{
	const UGFSafeZoneSubsystem* Sub = UGFSafeZoneSubsystem::Get(this);
	return Sub ? Sub->GetNoEntry() : TArray<FBox>();
}

const IGFSafeZoneQuery* UGFGiantEventComponent::GetSafeZoneQuery() const
{
	if (!ZoneOverride.IsValid() && SafeRegions.Num() == 0)
	{
		return nullptr;
	}
	return &GetSafeZone();
}

void UGFGiantEventComponent::RefreshSafeZone(int32 NumPlayers)
{
	if (bUseDefaultRegion)
	{
		const FGFSafeRegion R = FGFGiantRules::DefaultRegion(Tuning, DefaultSafeCenter, FMath::Max(1, NumPlayers));
		if (SafeRegions.Num() != 1 || SafeRegions[0].Members != R.Members || SafeRegions[0].Center != R.Center || SafeRegions[0].BaseRadius != R.BaseRadius
			|| SafeRegions[0].RadiusPerExtraMember != R.RadiusPerExtraMember || SafeRegions[0].MaxRadius != R.MaxRadius)
		{
			SafeRegions = { R };
		}
	}
}

AGFPlayerState* UGFGiantEventComponent::FindPS(const FString& Key) const
{
	const AGFGameState* G = GS();
	return G ? G->FindPlayerByKey(Key) : nullptr;
}

TArray<FGFGiantPlayerView> UGFGiantEventComponent::GatherPlayers() const
{
	TArray<FGFGiantPlayerView> Out;
	const AGFGameState* G = GS();
	if (!G)
	{
		return Out;
	}
	for (APlayerState* Base : G->PlayerArray)
	{
		const AGFPlayerState* PS = Cast<AGFPlayerState>(Base);
		const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (!Pawn || PS->PlayerKey.IsEmpty() || Bellies.ContainsByPredicate([&](const FBelly& B) { return B.PS.Get() == PS; }))
		{
			continue;
		}
		FGFGiantPlayerView V;
		V.Key = PS->PlayerKey;
		V.Name = PS->GetPlayerName();
		V.Location = Pawn->GetActorLocation();
		V.PlaySeconds = Save.SecondsOf(PS->PlayerKey);
		V.FirstSeenTicks = Save.FirstSeenOf(PS->PlayerKey);
		V.bBusy = BusyKeys.Contains(PS->PlayerKey);
		V.Dragger = PS->LastDrag.DraggerName;
		V.DraggerKey = PS->LastDrag.DraggerKey;
		V.DraggedAtUtcTicks = PS->LastDrag.AtUtcTicks;
		if (PS->LastDrag.IsSet())
		{
			const double Age = double(NowUtcTicks() - PS->LastDrag.AtUtcTicks) / FGFGiantRules::TicksPerSecond;
			V.DragAgeSeconds = FMath::Max(0.f, static_cast<float>(Age));
		}
		FGFAttentionHold Hold;
		if (FGFAttention::FindHold(GetWorld(), PS->PlayerKey, Hold))
		{
			V.bHeld = true;
			V.HolderKey = Hold.HolderKey;
			V.HolderName = Hold.HolderName;
		}
		V.GroupId = PS->FactionId;
		V.bHost = PS->bIsHost;
		if (const ACharacter* C = Cast<ACharacter>(Pawn))
		{
			V.RunSpeed = C->GetCharacterMovement() ? C->GetCharacterMovement()->MaxWalkSpeed : 0.f;
		}
		Out.Add(V);
	}
	return Out;
}

bool UGFGiantEventComponent::ServerStartEvent()
{
	if (!IsServer() || bEventActive)
	{
		return false;
	}
	const TArray<FGFGiantPlayerView> Players = GatherPlayers();
	if (Players.Num() == 0)
	{
		return false;
	}
	const bool bReturn = FGFGiantRules::MarkEventStarted(Save, NowUtcTicks());
	Sim = FGFGiantSim();
	Sim.Speaker = Info.DisplayName.IsEmpty() ? FString(TEXT("Giant")) : Info.DisplayName;
	Sim.Channel = Info.ChatChannel.IsNone() ? FName(TEXT("All")) : Info.ChatChannel;
	FGFGiantStepOut Out;
	RefreshSafeZone(Players.Num() + Bellies.Num());
	Sim.NoEntry = GetNoEntry();
	Sim.Begin(Tuning, Lines, GetLandZone(), Players, bReturn, ForcedSeed != 0 ? ForcedSeed : FMath::Rand(), Out);

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Giant = GetWorld()->SpawnActor<AGFGiantDino>(AGFGiantDino::StaticClass(), Sim.Location + FVector(0.f, 0.f, 600.f), FRotator::ZeroRotator, P);
	if (Giant)
	{
		Giant->OnAttackedBy.AddUObject(this, &UGFGiantEventComponent::OnGiantAttacked);
		Giant->SetLook(Sim.Speaker, Info.AssetKey, Info.MeshScale);
		Giant->State = Sim.State;
		Giant->TargetKey = Sim.TargetKey;
	}
	bEventActive = true;
	++EventsStarted;
	WatchPawns();
	UE_LOG(LogGF, Log, TEXT("Giant: %s arrives (event %d%s)."), *Sim.Speaker, Save.EventCount, bReturn ? TEXT(", return visit") : TEXT(""));
	Apply(Out);
	NetUpdate();
	return true;
}

void UGFGiantEventComponent::OnGiantAttacked(AGFGiantDino* Dino, AGFPlayerState* By)
{
	if (Dino == Giant)
	{
		ServerOnAttacked(By);
	}
}

void UGFGiantEventComponent::OnTaunt(UWorld* World, const FGFGiantTaunt& Taunt)
{
	if (World != GetWorld() || !IsServer() || !bEventActive || !Sim.IsActive())
	{
		return;
	}
	Sim.NoticeTaunt(Taunt.Location, Taunt.Radius);
}

bool UGFGiantEventComponent::ServerOnAttacked(AGFPlayerState* By)
{
	if (!IsServer() || !By || !bEventActive || !Sim.IsActive())
	{
		return false;
	}
	FGFGiantStepOut Out;
	Sim.Zone = &GetLandZone();
	Sim.NoEntry = GetNoEntry();
	const bool bEaten = Sim.Attacked(By->PlayerKey, GatherPlayers(), Out);
	Apply(Out);
	return bEaten;
}

void UGFGiantEventComponent::Apply(const FGFGiantStepOut& Out)
{
	for (const FGFGiantChatLine& L : Out.Chat)
	{
		Chat.Add(L);
		UE_LOG(LogGF, Log, TEXT("[%s] %s: %s"), *L.Channel.ToString(), *L.Speaker, *L.Text);
	}
	if (Chat.Num() > MaxChat)
	{
		Chat.RemoveAt(0, Chat.Num() - MaxChat);
	}
	if (Out.Chat.Num() > 0)
	{
		OnRep_Chat(); // server-side listeners too
	}
	for (const FGFGiantShove& S : Out.Shoves)
	{
		AGFPlayerState* PS = FindPS(S.Key);
		APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (ACharacter* C = Cast<ACharacter>(Pawn))
		{
			C->LaunchCharacter(S.Velocity, true, true);
			++ShovesApplied;
		}
		else if (Pawn)
		{
			Pawn->SetActorLocation(Pawn->GetActorLocation() + S.Velocity.GetSafeNormal2D() * 200.f);
			++ShovesApplied;
		}
	}
	if (!Out.Eaten.IsEmpty())
	{
		EatenKeys.Add(Out.Eaten);
		if (AGFPlayerState* PS = FindPS(Out.Eaten))
		{
			BeginBelly(PS);
		}
	}
	if (Giant)
	{
		const AGFPlayerState* T = FindPS(Sim.TargetKey);
		const APawn* TP = T ? T->GetPawn() : nullptr;
		const FVector Face = TP ? TP->GetActorLocation() : FVector::ZeroVector;
		Giant->MoveBody(Sim.Location, TP ? &Face : nullptr);
		Giant->State = Sim.State;
		Giant->TargetKey = Sim.TargetKey;
	}
	if (Out.bEnded)
	{
		EndEvent();
	}
}

void UGFGiantEventComponent::WatchPawns()
{
	const AGFGameState* G = GS();
	if (!G)
	{
		return;
	}
	for (APlayerState* Base : G->PlayerArray)
	{
		if (APawn* Pawn = Base ? Base->GetPawn() : nullptr)
		{
			Pawn->OnTakeAnyDamage.AddUniqueDynamic(this, &UGFGiantEventComponent::OnPlayerPawnDamaged);
		}
	}
}

void UGFGiantEventComponent::OnPlayerPawnDamaged(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	if (Giant && (DamageCauser == Giant || DamageCauser == GetOwner()))
	{
		++DamageFromGiant;
		UE_LOG(LogGF, Error, TEXT("Giant: %s took damage from the giant (must never happen)."), *GetNameSafe(DamagedActor));
	}
}

void UGFGiantEventComponent::EndEvent()
{
	if (Giant)
	{
		Giant->Destroy();
		Giant = nullptr;
	}
	bEventActive = false;
	NetUpdate();
}

void UGFGiantEventComponent::BeginBelly(AGFPlayerState* PS)
{
	APawn* Pawn = PS->GetPawn();
	FBelly B;
	B.PS = PS;
	B.Left = Tuning.BellyCamSeconds;
	B.From = Pawn ? Pawn->GetActorLocation() : DefaultSafeCenter;
	if (Pawn)
	{
		Pawn->SetActorHiddenInGame(true);
		if (ACharacter* C = Cast<ACharacter>(Pawn))
		{
			C->GetCharacterMovement()->StopMovementImmediately();
			C->GetCharacterMovement()->DisableMovement();
		}
	}
	FActorSpawnParameters P;
	P.Owner = PS->GetOwner(); // the player's controller: owner-only relevancy
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	B.Cam = GetWorld()->SpawnActor<AGFGiantBellyCam>(AGFGiantBellyCam::StaticClass(), FVector(B.From.X, B.From.Y, B.From.Z - 50000.f), FRotator::ZeroRotator, P);
	if (B.Cam.IsValid())
	{
		++BellyCamsSpawned;
	}
	Bellies.Add(B);
}

void UGFGiantEventComponent::FinishBelly(FBelly& B, bool bFaint)
{
	if (B.Cam.IsValid())
	{
		B.Cam->Destroy();
	}
	AGFPlayerState* PS = B.PS.Get();
	APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	if (Pawn)
	{
		Pawn->SetActorHiddenInGame(false);
		if (ACharacter* C = Cast<ACharacter>(Pawn))
		{
			C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
	if (bFaint && PS)
	{
		const FVector At = FGFGiantRules::RespawnPoint(Tuning, GetSafeZone(), B.From);
		if (IGFFaintHandler* H = Cast<IGFFaintHandler>(FaintHandler.GetObject()))
		{
			H->HandleOpenWorldFaint(PS, At, TEXT("GiantSnack"));
		}
	}
}

void UGFGiantEventComponent::OnModePlayerLeft(AGFPlayerState* PS)
{
	for (int32 i = Bellies.Num() - 1; i >= 0; --i)
	{
		if (Bellies[i].PS.Get() == PS)
		{
			FinishBelly(Bellies[i], false);
			Bellies.RemoveAt(i);
		}
	}
}

FGFModeResult UGFGiantEventComponent::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& R)
{
	if (!By)
	{
		return FGFModeResult::Fail(TEXT("NoPlayer"));
	}
	if (R.Verb == TEXT("Hit"))
	{
		if (!bEventActive || !Giant || !Sim.IsActive())
		{
			return FGFModeResult::Fail(TEXT("NoGiant"));
		}
		const APawn* Pawn = By->GetPawn();
		if (!Pawn || FVector::Dist2D(Pawn->GetActorLocation(), Giant->GetActorLocation()) > Tuning.AttackRange)
		{
			return FGFModeResult::Fail(TEXT("TooFar"));
		}
		FGFModeResult Res = FGFModeResult::Ok();
		const int32 EatenBefore = EatenKeys.Num();
		Giant->NotifyAttackedBy(By); // same path as a damage hit
		Res.Ints = { EatenKeys.Num() > EatenBefore ? 1 : 0 };
		return Res;
	}
	return FGFModeResult::Fail(TEXT("UnknownVerb"));
}

void UGFGiantEventComponent::CaptureModeState(TArray<uint8>& Out) const
{
	FGFModeBytes::Write(Save, Out);
}

void UGFGiantEventComponent::RestoreModeState(const TArray<uint8>& In)
{
	FGFGiantSave Loaded;
	if (In.Num() > 0 && FGFModeBytes::Read(Loaded, In))
	{
		Save = Loaded;
	}
}

void UGFGiantEventComponent::OnRep_Chat()
{
	for (const FGFGiantChatLine& L : Chat)
	{
		if (L.Seq > LastBroadcastSeq)
		{
			LastBroadcastSeq = L.Seq;
			OnGiantChat.Broadcast(L);
		}
	}
}

void UGFGiantEventComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
	if (!IsServer())
	{
		return;
	}
	const int64 Now = NowUtcTicks();
	const TArray<FGFGiantPlayerView> Players = GatherPlayers();

	// Play time: per player (who is newest) and summed (the trigger).
	for (const FGFGiantPlayerView& P : Players)
	{
		const int32 i = Save.Touch(P.Key, Now);
		Save.PlayerSeconds[i] += DeltaSeconds;
	}
	FGFGiantRules::AccumulatePlay(Save, DeltaSeconds, Players.Num());
	FGFGiantRules::StartClockIfNeeded(Save, Now);

	RefreshSafeZone(Players.Num() + Bellies.Num());

	// Belly cams: after BellyCamSeconds the player faints (hook) and respawns just inside the nearest safe border.
	for (int32 i = Bellies.Num() - 1; i >= 0; --i)
	{
		Bellies[i].Left -= DeltaSeconds;
		if (Bellies[i].Left <= 0.f)
		{
			FinishBelly(Bellies[i], true);
			Bellies.RemoveAt(i);
		}
	}

	if (bEventActive)
	{
		WatchPawns(); // late joiners / respawned pawns
		FGFGiantStepOut Out;
		Sim.Zone = &GetLandZone();
		Sim.NoEntry = GetNoEntry();
		Sim.Step(DeltaSeconds, Players, Out);
		Apply(Out);
		return;
	}
	TriggerCheckIn -= DeltaSeconds;
	if (bAutoTrigger && TriggerCheckIn <= 0.f)
	{
		TriggerCheckIn = Tuning.TriggerCheckSeconds;
		const bool bAnyoneOutside = FGFGiantRules::AnyoneOutside(GetLandZone(), Players, GetNoEntry());
		if (FGFGiantRules::ShouldTrigger(Tuning, Save, Now, bAnyoneOutside))
		{
			ServerStartEvent();
		}
	}
}
