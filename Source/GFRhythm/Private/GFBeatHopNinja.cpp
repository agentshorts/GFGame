#include "GFBeatHopNinja.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"

namespace GFBeatHopNinjaDetail
{
	GFBeatHopNinja::FProfileProvider& ProfileProvider()
	{
		static GFBeatHopNinja::FProfileProvider Provider = nullptr;
		return Provider;
	}

	GFBeatHopNinja::FMantleProbe& MantleProbe()
	{
		static GFBeatHopNinja::FMantleProbe Probe = nullptr;
		return Probe;
	}

	constexpr int32 Bit(int32 Index)
	{
		return 1 << Index;
	}

	// Easy: mantle ledge, dash gate, landing marker, crumble brick, beat wall.
	constexpr int32 EasyMask = Bit(0) | Bit(1) | Bit(9) | Bit(10) | Bit(11);
	// Normal adds wall run and grapple.
	constexpr int32 NormalMask = EasyMask | Bit(2) | Bit(3);
	// Hard adds swivel, ledge grab, air dash, triple jump and dash grip.
	constexpr int32 HardMask = NormalMask | Bit(4) | Bit(5) | Bit(6) | Bit(7) | Bit(8);

	static_assert(static_cast<int32>(EGFBeatHopKitMove::MantleLedge) == 0, "MantleLedge bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::DashGate) == 1, "DashGate bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::WallRun) == 2, "WallRun bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::Grapple) == 3, "Grapple bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::Swivel) == 4, "Swivel bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::LedgeGrab) == 5, "LedgeGrab bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::AirDash) == 6, "AirDash bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::TripleJump) == 7, "TripleJump bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::DashGrip) == 8, "DashGrip bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::LandingMarker) == 9, "LandingMarker bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::CrumbleBrick) == 10, "CrumbleBrick bit");
	static_assert(static_cast<int32>(EGFBeatHopKitMove::BeatWall) == 11, "BeatWall bit");
	static_assert(EasyMask == 3587, "Easy move mask");
	static_assert(NormalMask == 3599, "Normal move mask");
	static_assert(HardMask == 4095, "Hard move mask");

	double PeriodOrStation(double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile)
	{
		return PeriodSeconds > 0.0 ? PeriodSeconds : static_cast<double>(Profile.StationPeriodSeconds);
	}

	double GoodWindowSeconds(const FGFJudgeWindows& Windows, const FGFBeatHopTraversalProfile& Profile)
	{
		return static_cast<double>(Windows.GoodMs) * static_cast<double>(Profile.MillisecondsToSeconds);
	}

	bool NeedsGround(EGFBeatHopObstacle Kind)
	{
		return Kind == EGFBeatHopObstacle::CrumbleBrick;
	}

	bool NeedsAir(EGFBeatHopObstacle Kind)
	{
		return Kind == EGFBeatHopObstacle::WallRunPanel
			|| Kind == EGFBeatHopObstacle::DashGate
			|| Kind == EGFBeatHopObstacle::GrapplePost
			|| Kind == EGFBeatHopObstacle::SwivelPole;
	}
}

int32 GFBeatHopNinja::MoveBit(EGFBeatHopKitMove Move)
{
	return GFBeatHopNinjaDetail::Bit(static_cast<int32>(Move));
}

FGFBeatHopTraversalProfile GFBeatHopNinja::Profile()
{
	if (const FProfileProvider Provider = GFBeatHopNinjaDetail::ProfileProvider())
	{
		return Provider();
	}
	return FGFBeatHopTraversalProfile();
}

void GFBeatHopNinja::SetProfileProvider(FProfileProvider Provider)
{
	GFBeatHopNinjaDetail::ProfileProvider() = Provider;
}

bool GFBeatHopNinja::IsMantleImplemented()
{
	const FMantleProbe Probe = GFBeatHopNinjaDetail::MantleProbe();
	return Probe && Probe();
}

void GFBeatHopNinja::SetMantleProbe(FMantleProbe Probe)
{
	GFBeatHopNinjaDetail::MantleProbe() = Probe;
}

bool GFBeatHopNinja::Allows(EGFBeatDifficulty Difficulty, EGFBeatHopKitMove Move, const FGFBeatHopTraversalProfile& Profile)
{
	int32 Mask = Profile.EasyMoves;
	if (Difficulty == EGFBeatDifficulty::Normal)
	{
		Mask = Profile.NormalMoves;
	}
	else if (Difficulty == EGFBeatDifficulty::Hard)
	{
		Mask = Profile.HardMoves;
	}
	return (Mask & MoveBit(Move)) != 0;
}

int32 GFBeatHopNinja::JumpsFor(EGFBeatDifficulty Difficulty, const FGFBeatHopTraversalProfile& Profile)
{
	if (Difficulty == EGFBeatDifficulty::Hard)
	{
		return Profile.HardJumps;
	}
	if (Difficulty == EGFBeatDifficulty::Normal)
	{
		return Profile.NormalJumps;
	}
	return Profile.EasyJumps;
}

EGFBeatHopMantleBand GFBeatHopNinja::MantleBand(float HeightCm, EGFBeatDifficulty Difficulty, const FGFBeatHopTraversalProfile& Profile)
{
	if (HeightCm <= Profile.AutoMantleMinCm)
	{
		return EGFBeatHopMantleBand::None;
	}
	if (HeightCm <= Profile.AutoMantleMaxCm)
	{
		return EGFBeatHopMantleBand::Auto;
	}
	if (HeightCm >= Profile.LedgeGrabMinCm && HeightCm <= Profile.LedgeGrabMaxCm)
	{
		if (Profile.bLedgeGrabHardOnly && Difficulty != EGFBeatDifficulty::Hard)
		{
			return EGFBeatHopMantleBand::TooHigh;
		}
		return EGFBeatHopMantleBand::Grab;
	}
	return EGFBeatHopMantleBand::TooHigh;
}

EGFBeatHopKitMove GFBeatHopNinja::MoveFor(EGFBeatHopObstacle Kind, float HeightCm, EGFBeatDifficulty Difficulty, const FGFBeatHopTraversalProfile& Profile)
{
	switch (Kind)
	{
	case EGFBeatHopObstacle::WallRunPanel:
		return EGFBeatHopKitMove::WallRun;
	case EGFBeatHopObstacle::MantleLedge:
		return MantleBand(HeightCm, Difficulty, Profile) == EGFBeatHopMantleBand::Grab
			? EGFBeatHopKitMove::LedgeGrab
			: EGFBeatHopKitMove::MantleLedge;
	case EGFBeatHopObstacle::GrapplePost:
		return EGFBeatHopKitMove::Grapple;
	case EGFBeatHopObstacle::DashGate:
		return EGFBeatHopKitMove::DashGate;
	case EGFBeatHopObstacle::SwivelPole:
		return EGFBeatHopKitMove::Swivel;
	case EGFBeatHopObstacle::CrumbleBrick:
		return EGFBeatHopKitMove::CrumbleBrick;
	case EGFBeatHopObstacle::BeatWall:
		return EGFBeatHopKitMove::BeatWall;
	default:
		return EGFBeatHopKitMove::MantleLedge;
	}
}

float GFBeatHopNinja::ChartDistanceCm(float Beats, float RefSpeedCms)
{
	return FMath::Max(0.f, Beats) * FMath::Max(0.f, RefSpeedCms);
}

float GFBeatHopNinja::DurationForBeats(float Beats, float RefSpeedCms, float SpeedCms)
{
	const float Speed = FMath::Max(SpeedCms, KINDA_SMALL_NUMBER);
	return ChartDistanceCm(Beats, RefSpeedCms) / Speed;
}

float GFBeatHopNinja::SpeedForBeats(float Beats, float RefSpeedCms, float DurationSeconds)
{
	const float Duration = FMath::Max(DurationSeconds, KINDA_SMALL_NUMBER);
	return ChartDistanceCm(Beats, RefSpeedCms) / Duration;
}

bool GFBeatHopNinja::IsWithinBuffer(double PressTime, double NoteTime, const FGFBeatHopTraversalProfile& Profile)
{
	const double Early = NoteTime - PressTime;
	return Early >= 0.0 && Early <= static_cast<double>(Profile.InputBufferSeconds);
}

bool GFBeatHopNinja::IsWithinCoyote(double LeftGroundAt, double PressTime, const FGFBeatHopTraversalProfile& Profile)
{
	if (LeftGroundAt < 0.0)
	{
		return false;
	}
	const double Since = PressTime - LeftGroundAt;
	return Since >= 0.0 && Since <= static_cast<double>(Profile.CoyoteSeconds);
}

bool GFBeatHopNinja::GroundedEnough(const FGFBeatHopNinjaPress& Press, const FGFBeatHopTraversalProfile& Profile)
{
	return Press.bGrounded || IsWithinCoyote(Press.LeftGroundAt, Press.Time, Profile);
}

FVector GFBeatHopNinja::ObstacleExtent(EGFBeatHopObstacle Kind, float HeightCm, const FGFBeatHopTraversalProfile& Profile)
{
	const float MantleHeight = HeightCm > 0.f ? HeightCm : Profile.MantleAutoSampleCm;
	switch (Kind)
	{
	case EGFBeatHopObstacle::WallRunPanel:
		return FVector(Profile.WallRunPanelLengthCm, Profile.WallRunPanelThicknessCm, Profile.WallRunPanelHeightCm);
	case EGFBeatHopObstacle::MantleLedge:
		return FVector(Profile.MantleLedgeDepthCm, Profile.MantleLedgeWidthCm, MantleHeight);
	case EGFBeatHopObstacle::GrapplePost:
		return FVector(Profile.GrapplePostRadiusCm, Profile.GrapplePostRadiusCm, Profile.GrapplePostHeightCm);
	case EGFBeatHopObstacle::DashGate:
		return FVector(Profile.DashGateDepthCm, Profile.DashGateRadiusCm, Profile.DashGateRadiusCm);
	case EGFBeatHopObstacle::SwivelPole:
		return FVector(Profile.SwivelPoleRadiusCm, Profile.SwivelPoleRadiusCm, Profile.SwivelPoleHeightCm);
	case EGFBeatHopObstacle::CrumbleBrick:
		return FVector(Profile.CrumbleBrickSizeCm, Profile.CrumbleBrickSizeCm, Profile.CrumbleBrickSizeCm);
	case EGFBeatHopObstacle::BeatWall:
		return FVector(Profile.BeatWallThicknessCm, Profile.BeatWallWidthCm, Profile.BeatWallHeightCm);
	default:
		return FVector(Profile.MeshUnitCm, Profile.MeshUnitCm, Profile.MeshUnitCm);
	}
}

float GFBeatHopNinja::CameraPullbackCm(bool bWallRunning, bool bGrappling, const FGFBeatHopTraversalProfile& Profile)
{
	if (bWallRunning)
	{
		return Profile.WallRunCameraPullbackCm;
	}
	if (bGrappling)
	{
		return Profile.GrappleCameraPullbackCm;
	}
	return 0.f;
}

bool GFBeatHopNinja::IsGlowing(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, double WindowSeconds, const FGFBeatHopTraversalProfile& Profile)
{
	const double Period = GFBeatHopNinjaDetail::PeriodOrStation(PeriodSeconds, Profile);
	const double Lead = static_cast<double>(Profile.GlowLeadBeats) * Period;
	return Now >= Obstacle.BeatTime - Lead && Now <= Obstacle.BeatTime + WindowSeconds;
}

bool GFBeatHopNinja::IsDashGateOpen(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile)
{
	if (Obstacle.Kind != EGFBeatHopObstacle::DashGate)
	{
		return false;
	}
	const double Period = GFBeatHopNinjaDetail::PeriodOrStation(PeriodSeconds, Profile);
	const double Open = static_cast<double>(Profile.DashGateOpenBeats) * Period;
	const double Early = static_cast<double>(Profile.InputBufferSeconds);
	return Now >= Obstacle.BeatTime - Early && Now < Obstacle.BeatTime + Open;
}

bool GFBeatHopNinja::IsGrappleLit(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile)
{
	if (Obstacle.Kind != EGFBeatHopObstacle::GrapplePost)
	{
		return false;
	}
	const double Period = GFBeatHopNinjaDetail::PeriodOrStation(PeriodSeconds, Profile);
	const double Lit = static_cast<double>(Profile.GrappleLitBeats) * Period;
	const double Early = static_cast<double>(Profile.InputBufferSeconds);
	return Now >= Obstacle.BeatTime - Early && Now < Obstacle.BeatTime + Lit;
}

bool GFBeatHopNinja::IsBeatWallRaised(double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile)
{
	const double Period = GFBeatHopNinjaDetail::PeriodOrStation(PeriodSeconds, Profile);
	if (Period <= 0.0 || Profile.BeatWallCycleBeats <= 0.f)
	{
		return false;
	}
	const double Cycle = static_cast<double>(Profile.BeatWallCycleBeats);
	double Phase = FMath::Fmod(Now / Period, Cycle);
	if (Phase < 0.0)
	{
		Phase += Cycle;
	}
	return Phase >= static_cast<double>(Profile.BeatWallHoldBeats);
}

bool GFBeatHopNinja::HasCrumbleFallen(const FGFBeatHopObstacleInstance& Obstacle, double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile)
{
	if (Obstacle.Kind != EGFBeatHopObstacle::CrumbleBrick || Obstacle.TouchedAt < 0.0)
	{
		return false;
	}
	const double Period = GFBeatHopNinjaDetail::PeriodOrStation(PeriodSeconds, Profile);
	const double Delay = static_cast<double>(Profile.CrumbleDelayBeats) * Period;
	return Now >= Obstacle.TouchedAt + Delay;
}

bool GFBeatHopNinja::IsDownbeat(double Now, double PeriodSeconds, const FGFBeatHopTraversalProfile& Profile)
{
	const double Period = GFBeatHopNinjaDetail::PeriodOrStation(PeriodSeconds, Profile);
	if (Period <= 0.0)
	{
		return false;
	}
	const double Span = static_cast<double>(Profile.BeatPhaseSpan);
	const double Phase = (Now / Period) - FMath::FloorToDouble(Now / Period);
	double Delta = FMath::Abs(Phase * Span - static_cast<double>(Profile.SwivelBeatPhase));
	if (Delta > Span - Delta)
	{
		Delta = Span - Delta;
	}
	return Delta <= static_cast<double>(Profile.SwivelPhaseWindow);
}

EGFJudgement GFBeatHopNinja::JudgeTiming(double PressTime, double NoteTime, const FGFJudgeWindows& Windows, const FGFBeatHopTraversalProfile& Profile)
{
	const double Error = PressTime - NoteTime;
	const EGFJudgement Raw = FGFRhythmJudge::Classify(Error, Windows);
	if (Raw != EGFJudgement::Miss)
	{
		return Raw;
	}
	if (IsWithinBuffer(PressTime, NoteTime, Profile))
	{
		return EGFJudgement::Good;
	}
	return EGFJudgement::Miss;
}

FGFBeatHopNinjaVerdict GFBeatHopNinja::Judge(
	const FGFBeatHopObstacleInstance& Obstacle,
	const FGFBeatHopNinjaPress& Press,
	EGFBeatHopNinjaContact Contact,
	EGFBeatDifficulty Difficulty,
	const FGFJudgeWindows& Windows,
	double PeriodSeconds,
	const FGFBeatHopTraversalProfile& Profile,
	bool bMantleImplemented)
{
	FGFBeatHopNinjaVerdict Out;
	const double Period = GFBeatHopNinjaDetail::PeriodOrStation(PeriodSeconds, Profile);
	const double Window = GFBeatHopNinjaDetail::GoodWindowSeconds(Windows, Profile);
	Out.bGlowing = IsGlowing(Obstacle, Press.Time, Period, Window, Profile);
	Out.bGateOpen = IsDashGateOpen(Obstacle, Press.Time, Period, Profile);
	Out.bWallRaised = Obstacle.Kind == EGFBeatHopObstacle::BeatWall && IsBeatWallRaised(Press.Time, Period, Profile);

	if (Obstacle.Kind == EGFBeatHopObstacle::MantleLedge && !bMantleImplemented)
	{
		Out.bPendingMantle = true;
		Out.bMoveAllowed = Allows(Difficulty, MoveFor(Obstacle.Kind, Obstacle.HeightCm, Difficulty, Profile), Profile);
		return Out;
	}

	const EGFBeatHopKitMove Required = MoveFor(Obstacle.Kind, Obstacle.HeightCm, Difficulty, Profile);
	Out.bMoveAllowed = Allows(Difficulty, Required, Profile);
	const bool bPressed = Press.Move == Required || (Required == EGFBeatHopKitMove::DashGate && Press.Move == EGFBeatHopKitMove::AirDash);

	EGFBeatHopNinjaContact Used = Contact;
	if (Obstacle.Kind == EGFBeatHopObstacle::BeatWall && Out.bWallRaised)
	{
		Used = EGFBeatHopNinjaContact::Bonk;
	}
	if (Obstacle.Kind == EGFBeatHopObstacle::DashGate && !Out.bGateOpen)
	{
		Used = EGFBeatHopNinjaContact::Bonk;
	}
	if (Obstacle.Kind == EGFBeatHopObstacle::GrapplePost && !IsGrappleLit(Obstacle, Press.Time, Period, Profile))
	{
		Used = EGFBeatHopNinjaContact::Bonk;
	}
	if (Obstacle.Kind == EGFBeatHopObstacle::SwivelPole && !IsDownbeat(Obstacle.BeatTime, Period, Profile))
	{
		Used = EGFBeatHopNinjaContact::Bonk;
	}

	if (GFBeatHopNinjaDetail::NeedsGround(Obstacle.Kind) && !GroundedEnough(Press, Profile))
	{
		Out.Judgement = EGFJudgement::Miss;
		return Out;
	}
	if (GFBeatHopNinjaDetail::NeedsAir(Obstacle.Kind) && Press.bGrounded)
	{
		Out.Judgement = EGFJudgement::Miss;
		return Out;
	}
	if (!Out.bMoveAllowed || !bPressed || Used != EGFBeatHopNinjaContact::Clear)
	{
		Out.Judgement = EGFJudgement::Miss;
		return Out;
	}

	if (Obstacle.Kind == EGFBeatHopObstacle::MantleLedge)
	{
		const EGFBeatHopMantleBand Band = MantleBand(Obstacle.HeightCm, Difficulty, Profile);
		if (Band == EGFBeatHopMantleBand::None || Band == EGFBeatHopMantleBand::TooHigh)
		{
			Out.Judgement = EGFJudgement::Miss;
			return Out;
		}
		const EGFJudgement Timing = JudgeTiming(Press.Time, Obstacle.BeatTime, Windows, Profile);
		Out.Judgement = Timing == EGFJudgement::Perfect ? EGFJudgement::Perfect : EGFJudgement::Good;
		return Out;
	}

	Out.Judgement = JudgeTiming(Press.Time, Obstacle.BeatTime, Windows, Profile);
	if (Obstacle.Kind == EGFBeatHopObstacle::CrumbleBrick
		&& (Out.Judgement == EGFJudgement::Perfect || Out.Judgement == EGFJudgement::Good))
	{
		Out.bTouchCrumble = true;
	}
	if (Out.Judgement == EGFJudgement::Perfect || Out.Judgement == EGFJudgement::Good)
	{
		Out.CameraPullbackCm = CameraPullbackCm(
			Obstacle.Kind == EGFBeatHopObstacle::WallRunPanel,
			Obstacle.Kind == EGFBeatHopObstacle::GrapplePost,
			Profile);
	}
	return Out;
}

void FGFBeatHopInputBuffer::Note(double Time, EGFBeatHopKitMove Move)
{
	FEntry Entry;
	Entry.Time = Time;
	Entry.Move = Move;
	Presses.Add(Entry);
}

bool FGFBeatHopInputBuffer::Find(double NoteTime, EGFBeatHopKitMove Move, const FGFBeatHopTraversalProfile& Profile, double& OutTime) const
{
	const double Earliest = NoteTime - static_cast<double>(Profile.InputBufferSeconds);
	for (const FEntry& Entry : Presses)
	{
		if (Entry.Move == Move && Entry.Time >= Earliest && Entry.Time <= NoteTime)
		{
			OutTime = Entry.Time;
			return true;
		}
	}
	return false;
}

namespace GFBeatHopNinjaDetail
{
	struct FStationRow
	{
		EGFBeatHopObstacle Kind;
		const TCHAR* Id;
		const TCHAR* Expected;
	};

	const TArray<FStationRow>& StationRows()
	{
		static const TArray<FStationRow> Rows = {
			{ EGFBeatHopObstacle::WallRunPanel, TEXT("WallRun"), TEXT("Wall run on the beat is Perfect. Off the beat is Miss.") },
			{ EGFBeatHopObstacle::MantleLedge, TEXT("Mantle"), TEXT("Mantle ledge. Pending until #259. On the beat would be Perfect.") },
			{ EGFBeatHopObstacle::GrapplePost, TEXT("Grapple"), TEXT("Grapple while the post is lit.") },
			{ EGFBeatHopObstacle::DashGate, TEXT("DashGate"), TEXT("Air dash through the open ring.") },
			{ EGFBeatHopObstacle::SwivelPole, TEXT("Swivel"), TEXT("Swivel on the downbeat.") },
			{ EGFBeatHopObstacle::CrumbleBrick, TEXT("Crumble"), TEXT("Land on the beat. The brick falls after the tuned delay.") },
			{ EGFBeatHopObstacle::BeatWall, TEXT("BeatWall"), TEXT("Pass while the wall is down on the beat.") },
		};
		return Rows;
	}
}

const TArray<FGFBeatHopNinjaStation>& GFBeatHopNinja::Stations()
{
	static TArray<FGFBeatHopNinjaStation> Cached;
	Cached.Reset();
	const FGFBeatHopTraversalProfile P = Profile();
	const TArray<FGFBeatHopObstacleInstance> Chart = BuildStationChart(P);
	const TArray<GFBeatHopNinjaDetail::FStationRow>& Rows = GFBeatHopNinjaDetail::StationRows();
	const int32 Count = FMath::Min(Chart.Num(), Rows.Num());
	Cached.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FGFBeatHopNinjaStation Station;
		Station.Kind = Chart[Index].Kind;
		Station.Obstacle = Chart[Index];
		Station.HeightCm = Chart[Index].HeightCm;
		Station.Id = Rows[Index].Id;
		Station.Expected = Rows[Index].Expected;
		Cached.Add(Station);
	}
	return Cached;
}

TArray<FGFBeatHopObstacleInstance> GFBeatHopNinja::BuildStationChart(const FGFBeatHopTraversalProfile& Profile)
{
	const double BeatTime = static_cast<double>(Profile.StationBeatIndex) * static_cast<double>(Profile.StationPeriodSeconds);
	TArray<FGFBeatHopObstacleInstance> Chart;
	for (const GFBeatHopNinjaDetail::FStationRow& Row : GFBeatHopNinjaDetail::StationRows())
	{
		FGFBeatHopObstacleInstance Obstacle;
		Obstacle.Kind = Row.Kind;
		Obstacle.BeatTime = BeatTime;
		Obstacle.HeightCm = Row.Kind == EGFBeatHopObstacle::MantleLedge ? Profile.MantleAutoSampleCm : 0.f;
		Chart.Add(Obstacle);
	}
	return Chart;
}

AGFBeatHopNinjaRange::AGFBeatHopNinjaRange()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AGFBeatHopNinjaRange::BeginPlay()
{
	Super::BeginPlay();
	Build();
}

void AGFBeatHopNinjaRange::Build()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	UsedProfile = GFBeatHopNinja::Profile();
	Built = GFBeatHopNinja::Stations();
	for (int32 Index = 0; Index < Built.Num(); ++Index)
	{
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
		if (!Label)
		{
			continue;
		}
		Label->SetupAttachment(Root);
		const float Along = static_cast<float>(Index) * UsedProfile.StationSpacingCm;
		Label->SetRelativeLocation(FVector(Along, 0.f, UsedProfile.StationLabelHeightCm));
		Label->SetText(FText::FromString(Built[Index].Id.ToString()));
		Label->SetWorldSize(UsedProfile.StationLabelSize);
		Label->RegisterComponent();
		Labels.Add(Label);
	}
}

FGFBeatHopNinjaVerdict AGFBeatHopNinjaRange::JudgeStation(int32 Index, bool bOnBeat, EGFBeatDifficulty Difficulty) const
{
	FGFBeatHopNinjaVerdict Empty;
	if (!Built.IsValidIndex(Index))
	{
		return Empty;
	}
	const FGFBeatHopObstacleInstance& Obstacle = Built[Index].Obstacle;
	FGFBeatHopNinjaPress Press;
	Press.Move = GFBeatHopNinja::MoveFor(Obstacle.Kind, Obstacle.HeightCm, Difficulty, UsedProfile);
	Press.bGrounded = Obstacle.Kind == EGFBeatHopObstacle::CrumbleBrick || Obstacle.Kind == EGFBeatHopObstacle::BeatWall;
	if (bOnBeat)
	{
		Press.Time = Obstacle.BeatTime;
	}
	else
	{
		Press.Time = Obstacle.BeatTime
			- static_cast<double>(UsedProfile.InputBufferSeconds)
			- static_cast<double>(UsedProfile.MissMarginSeconds);
	}
	const FGFJudgeWindows Windows = FGFJudgeWindows::For(EGFBeatHopMode::Songs, Difficulty);
	return GFBeatHopNinja::Judge(
		Obstacle,
		Press,
		EGFBeatHopNinjaContact::Clear,
		Difficulty,
		Windows,
		static_cast<double>(UsedProfile.StationPeriodSeconds),
		UsedProfile,
		GFBeatHopNinja::IsMantleImplemented());
}
