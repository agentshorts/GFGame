#include "GFSocialComponent.h"
#include "GFJailCell.h"
#include "GFGameState.h"
#include "GFPlayerState.h"
#include "GFSafeZone.h"
#include "GFNoise.h"
#include "GFAnimMap.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogGFSocial, Log, All);

const FName UGFSocialComponent::ModeName(TEXT("Social"));
const FName UGFSocialComponent::DefaultPlayerMode(TEXT("OpenWorld"));

namespace GFSocialVerbs
{
	static const FName Grow(TEXT("Grow"));
	static const FName Shrink(TEXT("Shrink"));
	static const FName Grab(TEXT("Grab"));
	static const FName Drop(TEXT("Drop"));
	static const FName Jail(TEXT("Jail"));
	static const FName Unjail(TEXT("Unjail"));
	static const FName Kick(TEXT("Kick"));
	static const FName Ban(TEXT("Ban"));
	static const FName Unban(TEXT("Unban"));
	static const FName Emote(TEXT("Emote"));
}

void UGFVoiceMuteStub::SetVoiceMuted(const FString& Key, bool bMuted)
{
	++Calls;
	if (bMuted)
	{
		Muted.Add(Key);
	}
	else
	{
		Muted.Remove(Key);
	}
	UE_LOG(LogGFSocial, Log, TEXT("Voice (stub): %s %s"), *Key, bMuted ? TEXT("muted") : TEXT("unmuted"));
}

UGFSocialComponent::UGFSocialComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UGFSocialComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGFSocialComponent, Jailed);
	DOREPLIFETIME(UGFSocialComponent, Bans);
	DOREPLIFETIME(UGFSocialComponent, Scales);
	DOREPLIFETIME(UGFSocialComponent, Tethers);
	DOREPLIFETIME(UGFSocialComponent, Emotes);
}

void UGFSocialComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsServer())
	{
		return;
	}
	Voice = NewObject<UGFVoiceMuteStub>(this);
	if (AGFGameState* G = GS())
	{
		G->RegisterModeHandler(this); // restores the saved bans right away
	}
}

void UGFSocialComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (DripAudio)
	{
		DripAudio->Stop();
		DripAudio = nullptr;
	}
	if (IsServer())
	{
		if (AGFGameState* G = GS())
		{
			G->UnregisterModeHandler(this);
		}
		if (Cell)
		{
			Cell->Destroy();
			Cell = nullptr;
		}
	}
	Super::EndPlay(Reason);
}

bool UGFSocialComponent::IsServer() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

AGFGameState* UGFSocialComponent::GS() const
{
	return GetWorld() ? GetWorld()->GetGameState<AGFGameState>() : nullptr;
}

AGFPlayerState* UGFSocialComponent::FindPS(const FString& Key) const
{
	AGFGameState* G = GS();
	return G && !Key.IsEmpty() ? G->FindPlayerByKey(Key) : nullptr;
}

void UGFSocialComponent::NetUpdate()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->ForceNetUpdate();
	}
}

// ---------------- Queries ----------------

const FGFJailEntry* UGFSocialComponent::FindJail(const FString& Key) const
{
	return Key.IsEmpty() ? nullptr : Jailed.FindByPredicate([&](const FGFJailEntry& J) { return J.Key == Key; });
}

bool UGFSocialComponent::IsBanned(const FString& Key) const
{
	return !Key.IsEmpty() && Bans.ContainsByPredicate([&](const FGFBanEntry& B) { return B.Key == Key; });
}

float UGFSocialComponent::ScaleOf(const FString& Key) const
{
	const FGFScaleEntry* E = Scales.FindByPredicate([&](const FGFScaleEntry& S) { return S.Key == Key; });
	return E ? E->Scale : 1.f;
}

bool UGFSocialComponent::IsPlayerSafe(const AGFPlayerState* PS) const
{
	const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	const UGFSafeZoneSubsystem* Zones = UGFSafeZoneSubsystem::Get(this);
	return Pawn && Zones && Zones->IsInsideSafe(Pawn->GetActorLocation());
}

FGFSocialPlayer UGFSocialComponent::Describe(const AGFPlayerState* PS) const
{
	FGFSocialPlayer P;
	if (PS)
	{
		P.Key = PS->PlayerKey;
		P.Faction = PS->FactionId;
		P.bHost = PS->bIsHost;
		P.bSafe = IsPlayerSafe(PS);
		P.bJailed = IsJailed(PS->PlayerKey);
		const AGFGameState* G = GS();
		P.bLeader = G && G->IsFactionLeader(PS);
	}
	return P;
}

void UGFSocialComponent::ServerSetPlayerMode(const FString& Key, FName Mode)
{
	PlayerModes.Add(Key, Mode);
}

FName UGFSocialComponent::GetPlayerMode(const FString& Key) const
{
	const FName* M = PlayerModes.Find(Key);
	return M ? *M : DefaultPlayerMode;
}

// ---------------- Mode handler ----------------

FGFModeResult UGFSocialComponent::HandleModeRequest(AGFPlayerState* By, const FGFModeRequest& Request)
{
	using namespace GFSocialVerbs;
	AGFPlayerState* Target = nullptr;
	if (AGFGameState* G = GS(); G && Request.Ints.Num() > 0)
	{
		for (APlayerState* P : G->PlayerArray)
		{
			if (P && P->GetPlayerId() == Request.Ints[0])
			{
				Target = Cast<AGFPlayerState>(P);
			}
		}
	}
	FName Code;
	const FName V = Request.Verb;
	if (V == Grow) Code = ServerPower(By, Target, EGFSocialPower::Grow);
	else if (V == Shrink) Code = ServerPower(By, Target, EGFSocialPower::Shrink);
	else if (V == Grab) Code = ServerPower(By, Target, EGFSocialPower::Grab);
	else if (V == Drop) Code = ServerDrop(By);
	else if (V == Jail) Code = ServerJail(By, Target);
	else if (V == Unjail) Code = ServerRelease(By, Target);
	else if (V == Kick) Code = ServerKick(By, Target);
	else if (V == Ban) Code = ServerBan(By, Target);
	else if (V == Unban) Code = ServerUnban(By, Request.Name(0).ToString());
	else if (V == Emote) Code = ServerEmote(By, Request.Name(0));
	else return FGFModeResult::Fail(TEXT("UnknownVerb"));
	return Code == FGFSocialCodes::Ok ? FGFModeResult::Ok() : FGFModeResult::Fail(Code);
}

void UGFSocialComponent::OnModePlayerReady(AGFPlayerState* PS)
{
	if (PS && IsJailed(PS->PlayerKey))
	{
		ApplyJailTo(PS, true); // jail lasts the whole session, also across a rejoin
	}
	if (PS)
	{
		ApplyScale(PS->PlayerKey);
	}
}

void UGFSocialComponent::OnModePlayerLeft(AGFPlayerState* PS)
{
	if (!PS)
	{
		return;
	}
	BreakTethersOf(PS->PlayerKey);
	ScaleBook.Reset(PS->PlayerKey);
	SyncScales();
}

void UGFSocialComponent::CaptureModeState(TArray<uint8>& Out) const
{
	FGFSocialSave S;
	S.Bans = Bans;
	if (S.Bans.Num() > 0)
	{
		FGFModeBytes::Write(S, Out);
	}
}

void UGFSocialComponent::RestoreModeState(const TArray<uint8>& In)
{
	FGFSocialSave S;
	if (In.Num() > 0 && FGFModeBytes::Read(S, In))
	{
		Bans = S.Bans;
		NetUpdate();
	}
}

FName UGFSocialComponent::ChatBlockReason(const AGFPlayerState* By) const
{
	return By && IsJailed(By->PlayerKey) ? FGFSocialCodes::Muted : NAME_None;
}

bool UGFSocialComponent::RefuseJoin(const FString& PlayerKey, FString& OutWhy) const
{
	if (IsBanned(PlayerKey))
	{
		OutWhy = TEXT("You are banned from this world.");
		return true;
	}
	return false;
}

// ---------------- Powers ----------------

FName UGFSocialComponent::ServerPower(AGFPlayerState* By, AGFPlayerState* Target, EGFSocialPower Power)
{
	if (!IsServer() || !By)
	{
		return FGFSocialCodes::NotAllowed;
	}
	const APawn* A = By->GetPawn();
	const APawn* B = Target ? Target->GetPawn() : nullptr;
	if (!A || !B)
	{
		return FGFSocialCodes::NoSuchPlayer;
	}
	const FName Action = StaticEnum<EGFSocialPower>()->GetNameByValue(int64(Power));
	const double Now = GetWorld()->GetTimeSeconds();
	const FName Code = FGFSocialRules::CheckPower(Describe(By), Describe(Target), FVector::Dist(A->GetActorLocation(), B->GetActorLocation()),
		Cooldowns.IsCooling(By->PlayerKey, Action, Now), Tuning);
	if (Code != FGFSocialCodes::Ok)
	{
		return Code;
	}
	Cooldowns.Start(By->PlayerKey, Action, Now, Tuning.PowerCooldownSeconds);
	if (Power == EGFSocialPower::Grab)
	{
		Tethers.RemoveAll([&](const FGFTether& T) { return T.CasterKey == By->PlayerKey || T.TargetKey == Target->PlayerKey; });
		FGFTether T;
		T.CasterKey = By->PlayerKey;
		T.TargetKey = Target->PlayerKey;
		Tethers.Add(T);
	}
	else
	{
		ScaleBook.Step(Target->PlayerKey, Power == EGFSocialPower::Grow, Tuning);
		SyncScales();
	}
	NetUpdate();
	return FGFSocialCodes::Ok;
}

FName UGFSocialComponent::ServerDrop(AGFPlayerState* By)
{
	const int32 N = By ? Tethers.RemoveAll([&](const FGFTether& T) { return T.CasterKey == By->PlayerKey; }) : 0;
	NetUpdate();
	return N > 0 ? FGFSocialCodes::Ok : FName(TEXT("NoTether"));
}

void UGFSocialComponent::BreakTethersOf(const FString& Key)
{
	if (Tethers.RemoveAll([&](const FGFTether& T) { return T.CasterKey == Key || T.TargetKey == Key; }) > 0)
	{
		NetUpdate();
	}
}

void UGFSocialComponent::SyncScales()
{
	TArray<FString> Changed;
	for (const FGFScaleEntry& E : Scales)
	{
		Changed.AddUnique(E.Key);
	}
	Scales = ScaleBook.Entries;
	for (const FGFScaleEntry& E : Scales)
	{
		Changed.AddUnique(E.Key);
	}
	for (const FString& Key : Changed)
	{
		ApplyScale(Key);
	}
	NetUpdate();
}

void UGFSocialComponent::ApplyScale(const FString& Key)
{
	AGFPlayerState* PS = FindPS(Key);
	APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	const FVector Want(ScaleOf(Key));
	if (!Pawn->GetActorScale3D().Equals(Want, 0.001f))
	{
		Pawn->SetActorScale3D(Want);
	}
}

void UGFSocialComponent::ApplyAllScales()
{
	if (AGFGameState* G = GS())
	{
		for (APlayerState* P : G->PlayerArray)
		{
			if (const AGFPlayerState* PS = Cast<AGFPlayerState>(P))
			{
				ApplyScale(PS->PlayerKey);
			}
		}
	}
}

void UGFSocialComponent::OnRep_Scales()
{
	ApplyAllScales();
}

// ---------------- Jail ----------------

void UGFSocialComponent::EnsureCell()
{
	if (!Cell && IsServer())
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Cell = GetWorld()->SpawnActor<AGFJailCell>(AGFJailCell::StaticClass(), FTransform(Tuning.JailCellLocation), P);
	}
}

FVector UGFSocialComponent::CellSpot() const
{
	return Cell ? Cell->GetStandSpot() : Tuning.JailCellLocation + FVector(0.f, 0.f, 100.f);
}

void UGFSocialComponent::TeleportPawn(AGFPlayerState* PS, const FVector& To)
{
	APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	if (ACharacter* C = Cast<ACharacter>(Pawn))
	{
		if (UCharacterMovementComponent* Move = C->GetCharacterMovement())
		{
			Move->StopMovementImmediately();
		}
	}
	Pawn->TeleportTo(To, Pawn->GetActorRotation(), false, true);
}

void UGFSocialComponent::ApplyJailTo(AGFPlayerState* PS, bool bJail)
{
	if (!PS)
	{
		return;
	}
	if (bJail)
	{
		EnsureCell();
		TeleportPawn(PS, CellSpot());
	}
	if (Voice && Voice->IsVoiceMuted(PS->PlayerKey) != bJail)
	{
		Voice->SetVoiceMuted(PS->PlayerKey, bJail);
	}
}

FName UGFSocialComponent::ServerJail(AGFPlayerState* By, AGFPlayerState* Target)
{
	if (!IsServer() || !By)
	{
		return FGFSocialCodes::NotAllowed;
	}
	if (!Target)
	{
		return FGFSocialCodes::NoSuchPlayer;
	}
	const FName Code = FGFSocialRules::CanJail(Describe(By), Describe(Target));
	if (Code != FGFSocialCodes::Ok)
	{
		return Code;
	}
	FGFJailEntry J;
	J.Key = Target->PlayerKey;
	J.JailerKey = By->PlayerKey;
	J.ReturnLocation = Target->GetPawn() ? Target->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	J.Since = GetWorld()->GetTimeSeconds();
	Jailed.Add(J);
	ScaleBook.Reset(Target->PlayerKey);
	SyncScales();
	BreakTethersOf(Target->PlayerKey);
	ApplyJailTo(Target, true);
	UE_LOG(LogGFSocial, Log, TEXT("Jail: %s jailed %s"), *By->PlayerKey, *Target->PlayerKey);
	NetUpdate();
	UpdateLocalDrip();
	return FGFSocialCodes::Ok;
}

FName UGFSocialComponent::ServerRelease(AGFPlayerState* By, AGFPlayerState* Target)
{
	if (!IsServer() || !By)
	{
		return FGFSocialCodes::NotAllowed;
	}
	if (!Target)
	{
		return FGFSocialCodes::NoSuchPlayer;
	}
	const FGFJailEntry* J = FindJail(Target->PlayerKey);
	const FName Code = FGFSocialRules::CanRelease(Describe(By), Describe(Target));
	if (Code != FGFSocialCodes::Ok)
	{
		return Code;
	}
	const FVector Back = J ? J->ReturnLocation : FVector::ZeroVector;
	Jailed.RemoveAll([&](const FGFJailEntry& E) { return E.Key == Target->PlayerKey; });
	TeleportPawn(Target, Back);
	ApplyJailTo(Target, false);
	UE_LOG(LogGFSocial, Log, TEXT("Jail: %s released %s"), *By->PlayerKey, *Target->PlayerKey);
	NetUpdate();
	UpdateLocalDrip();
	return FGFSocialCodes::Ok;
}

void UGFSocialComponent::OnRep_Jailed()
{
	UpdateLocalDrip();
}

void UGFSocialComponent::UpdateLocalDrip()
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const AGFPlayerState* Local = PC && PC->IsLocalController() ? PC->GetPlayerState<AGFPlayerState>() : nullptr;
	const bool bWant = Local && IsJailed(Local->PlayerKey);
	if (bWant == bDripPlaying)
	{
		return;
	}
	bDripPlaying = bWant;
	if (bWant)
	{
		++DripStarts;
		if (USoundBase* Cue = DripLoopCue.IsNull() ? nullptr : DripLoopCue.LoadSynchronous())
		{
			DripAudio = UGameplayStatics::SpawnSound2D(this, Cue, 0.4f, 1.f, 0.f, nullptr, false, false);
		}
		UE_LOG(LogGFSocial, Log, TEXT("Jail: drip loop on%s"), DripAudio ? TEXT("") : TEXT(" (stub, no cue)"));
	}
	else
	{
		if (DripAudio)
		{
			DripAudio->Stop();
			DripAudio = nullptr;
		}
		UE_LOG(LogGFSocial, Log, TEXT("Jail: drip loop off"));
	}
}

// ---------------- Kick / ban ----------------

bool UGFSocialComponent::DoKick(AGFPlayerState* Target, const FString& Why)
{
	APlayerController* PC = Target ? Cast<APlayerController>(Target->GetOwner()) : nullptr;
	AGameModeBase* GM = GetWorld()->GetAuthGameMode();
	if (!PC || PC->IsLocalController() || !GM || !GM->GameSession)
	{
		return false;
	}
	BreakTethersOf(Target->PlayerKey);
	ScaleBook.Reset(Target->PlayerKey);
	SyncScales();
	++KicksDone;
	UE_LOG(LogGFSocial, Log, TEXT("Kick: %s (%s)"), *Target->PlayerKey, *Why);
	return GM->GameSession->KickPlayer(PC, FText::FromString(Why));
}

FName UGFSocialComponent::ServerKick(AGFPlayerState* By, AGFPlayerState* Target)
{
	if (!IsServer() || !By)
	{
		return FGFSocialCodes::NotAllowed;
	}
	if (!Target)
	{
		return FGFSocialCodes::NoSuchPlayer;
	}
	const FName Code = FGFSocialRules::CanKickOrBan(Describe(By), Describe(Target));
	if (Code != FGFSocialCodes::Ok)
	{
		return Code;
	}
	DoKick(Target, TEXT("You were kicked by the host."));
	return FGFSocialCodes::Ok;
}

FName UGFSocialComponent::ServerBan(AGFPlayerState* By, AGFPlayerState* Target)
{
	if (!IsServer() || !By)
	{
		return FGFSocialCodes::NotAllowed;
	}
	if (!Target)
	{
		return FGFSocialCodes::NoSuchPlayer;
	}
	const FName Code = FGFSocialRules::CanKickOrBan(Describe(By), Describe(Target));
	if (Code != FGFSocialCodes::Ok)
	{
		return Code;
	}
	if (!IsBanned(Target->PlayerKey))
	{
		FGFBanEntry B;
		B.Key = Target->PlayerKey;
		B.DisplayName = Target->GetPlayerName();
		B.ByKey = By->PlayerKey;
		B.AtUtcTicks = FDateTime::UtcNow().GetTicks();
		Bans.Add(B);
		NetUpdate();
	}
	DoKick(Target, TEXT("You are banned from this world."));
	return FGFSocialCodes::Ok;
}

FName UGFSocialComponent::ServerUnban(AGFPlayerState* By, const FString& Key)
{
	if (!IsServer() || !By)
	{
		return FGFSocialCodes::NotAllowed;
	}
	FGFSocialPlayer Banned;
	Banned.Key = Key;
	const FName Code = FGFSocialRules::CanKickOrBan(Describe(By), Banned);
	if (Code != FGFSocialCodes::Ok)
	{
		return Code;
	}
	if (Bans.RemoveAll([&](const FGFBanEntry& B) { return B.Key == Key; }) == 0)
	{
		return FGFSocialCodes::NotBanned;
	}
	NetUpdate();
	return FGFSocialCodes::Ok;
}

// ---------------- Emotes ----------------

FName UGFSocialComponent::ServerEmote(AGFPlayerState* By, FName EmoteId)
{
	if (!IsServer() || !By)
	{
		return FGFSocialCodes::NotAllowed;
	}
	if (IsJailed(By->PlayerKey))
	{
		return FGFSocialCodes::Jailed;
	}
	const FGFEmoteRow* Row = FGFSocialRules::FindEmote(EmoteId);
	if (!Row)
	{
		return FGFSocialCodes::UnknownEmote;
	}
	static const FName Action(TEXT("Emote"));
	const double Now = GetWorld()->GetTimeSeconds();
	if (Cooldowns.IsCooling(By->PlayerKey, Action, Now))
	{
		return FGFSocialCodes::Cooldown;
	}
	Cooldowns.Start(By->PlayerKey, Action, Now, Tuning.EmoteCooldownSeconds);

	FGFEmoteEvent E;
	E.Seq = NextEmoteSeq++;
	E.Key = By->PlayerKey;
	E.EmoteId = EmoteId;
	E.AnimKey = FGFSocialRules::EmoteAnimKey(*Row);
	E.Icon = Row->Icon;
	E.Seconds = Row->Seconds;
	const APawn* Pawn = By->GetPawn();
	const FName Mode = GetPlayerMode(By->PlayerKey);
	E.bNoise = Pawn && FGFSocialRules::EmoteMakesNoise(IsPlayerSafe(By), Mode);
	Emotes.Add(E);
	if (Emotes.Num() > MaxEmotes)
	{
		Emotes.RemoveAt(0, Emotes.Num() - MaxEmotes);
	}
	NetUpdate();
	if (E.bNoise)
	{
		// The newbie trap: a wave out in the wilds tells every hungry dino nearby where you are.
		const FGFModeRulesRow* Rules = FGFSocialRules::FindModeRules(Mode);
		FGFNoiseEvent Noise;
		Noise.Location = Pawn->GetActorLocation();
		Noise.Radius = Rules ? Rules->NoiseRadius : 0.f;
		Noise.SourceKey = By->PlayerKey;
		Noise.Tag = Action;
		++NoisesRaised;
		FGFNoise::Raise(GetWorld(), Noise);
	}
	LastEmoteBroadcast = E.Seq;
	ShowEmote(E);
	return FGFSocialCodes::Ok;
}

void UGFSocialComponent::OnRep_Emotes()
{
	for (const FGFEmoteEvent& E : Emotes)
	{
		if (E.Seq > LastEmoteBroadcast)
		{
			LastEmoteBroadcast = E.Seq;
			ShowEmote(E);
		}
	}
}

void UGFSocialComponent::ShowEmote(const FGFEmoteEvent& E)
{
	++EmotesSeen;
	LastEmoteSeen = E;
	OnEmote.Broadcast(E);
	if (GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	AGFPlayerState* PS = FindPS(E.Key);
	APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	// The clip, if there is one and it's imported; otherwise the icon bubble.
	if (!E.AnimKey.IsNone())
	{
		UAnimSequenceBase* Clip = FGFAnimMap::Load(E.AnimKey);
		const USkeletalMeshComponent* Mesh = Pawn->FindComponentByClass<USkeletalMeshComponent>();
		UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
		if (Clip && Anim)
		{
			const FGFAnimMapRow* Row = FGFAnimMap::Find(E.AnimKey);
			const int32 Loops = Row && Row->bLoop && Clip->GetPlayLength() > 0.f ? FMath::Max(1, FMath::CeilToInt(E.Seconds / Clip->GetPlayLength())) : 1;
			Anim->PlaySlotAnimationAsDynamicMontage(Clip, TEXT("DefaultSlot"), 0.2f, 0.2f, 1.f, Loops);
			return;
		}
	}
	++BubblesShown;
	if (!FApp::CanEverRender())
	{
		return; // headless: counted, nothing to draw
	}
	UTextRenderComponent* Bubble = NewObject<UTextRenderComponent>(Pawn);
	Bubble->SetupAttachment(Pawn->GetRootComponent());
	Bubble->SetRelativeLocation(FVector(0.f, 0.f, 130.f));
	Bubble->SetHorizontalAlignment(EHTA_Center);
	Bubble->SetWorldSize(40.f);
	Bubble->SetText(FText::FromString(E.Icon));
	Bubble->RegisterComponent();
	TWeakObjectPtr<UTextRenderComponent> Weak(Bubble);
	FTimerHandle H;
	GetWorld()->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Pawn, [Weak]()
	{
		if (UTextRenderComponent* B = Weak.Get())
		{
			B->DestroyComponent();
		}
	}), FMath::Max(0.5f, E.Seconds), false);
}

// ---------------- Tick ----------------

void UGFSocialComponent::TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaSeconds, TickType, ThisTickFunction);
	if (IsServer())
	{
		TickServer(DeltaSeconds);
		return;
	}
	// Clients: pawns that became relevant after the last OnRep still need their scale; the local jail state too.
	ClientScaleCheckIn -= DeltaSeconds;
	if (ClientScaleCheckIn <= 0.f)
	{
		ClientScaleCheckIn = 0.25f;
		ApplyAllScales();
		UpdateLocalDrip();
	}
}

void UGFSocialComponent::TickServer(float Dt)
{
	// Leaving safety undoes every power on you.
	if (ScaleBook.Entries.Num() > 0)
	{
		const TArray<FString> Reverted = ScaleBook.RevertUnsafe([this](const FString& Key)
		{
			const AGFPlayerState* PS = FindPS(Key);
			return PS && IsPlayerSafe(PS);
		});
		if (Reverted.Num() > 0)
		{
			SyncScales();
		}
	}
	for (int32 i = Tethers.Num() - 1; i >= 0; --i)
	{
		FGFTether& T = Tethers[i];
		T.Age += Dt;
		AGFPlayerState* Caster = FindPS(T.CasterKey);
		AGFPlayerState* Target = FindPS(T.TargetKey);
		APawn* CP = Caster ? Caster->GetPawn() : nullptr;
		APawn* TP = Target ? Target->GetPawn() : nullptr;
		if (!CP || !TP || T.Age > Tuning.TetherMaxSeconds || !IsPlayerSafe(Caster) || !IsPlayerSafe(Target)
			|| IsJailed(T.CasterKey) || IsJailed(T.TargetKey))
		{
			Tethers.RemoveAt(i);
			NetUpdate();
			continue;
		}
		FVector Goal = CP->GetActorLocation() + CP->GetActorForwardVector().GetSafeNormal2D() * Tuning.TetherLength;
		const FVector From = TP->GetActorLocation();
		Goal.Z = From.Z;
		const FVector Delta = Goal - From;
		const float Step = FMath::Min(Delta.Size(), Tuning.PullSpeed * Dt);
		if (Step > 1.f)
		{
			TP->SetActorLocation(From + Delta.GetSafeNormal() * Step, true);
		}
	}
	// A jailed pawn that somehow left the cell goes back in.
	for (const FGFJailEntry& J : Jailed)
	{
		AGFPlayerState* PS = FindPS(J.Key);
		const APawn* Pawn = PS ? PS->GetPawn() : nullptr;
		if (Pawn && FVector::Dist(Pawn->GetActorLocation(), CellSpot()) > Tuning.JailLeash)
		{
			TeleportPawn(PS, CellSpot());
		}
	}
}
