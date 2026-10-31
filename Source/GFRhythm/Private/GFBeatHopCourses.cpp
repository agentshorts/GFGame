#include "GFBeatHopCourses.h"

int32 FGFBeatHopCourseDef::DifficultyScore() const
{
	int32 Score = Order * 10;
	if (GapScale > 1.f)
	{
		Score += 2;
	}
	if (WeaveEvery > 0 && WeaveLanes != 0)
	{
		Score += 2;
	}
	if (SkidCmPerSec > 0.f)
	{
		Score += 2;
	}
	if (Revenge != EGFBeatHopRevengeRole::None)
	{
		Score += 2;
	}
	return Score;
}

const TArray<FGFBeatHopCourseDef>& FGFBeatHopCourses::All()
{
	// Order is the difficulty. Fern is the chart as authored. Each later course adds a hazard.
	static const TArray<FGFBeatHopCourseDef> Courses = []
	{
		TArray<FGFBeatHopCourseDef> Out;

		FGFBeatHopCourseDef Fern;
		Fern.Id = TEXT("Fern");
		Fern.Title = TEXT("Fern Lane");
		Fern.Order = 0;
		Fern.Hazard = EGFBeatHopCourseHazard::None;
		Fern.Revenge = EGFBeatHopRevengeRole::None;
		Fern.GapScale = 1.f;
		Fern.Tint = FLinearColor(0.392f, 0.791f, 0.624f);
		Out.Add(Fern);

		FGFBeatHopCourseDef Rime;
		Rime.Id = TEXT("Rime");
		Rime.Title = TEXT("Rime Skate");
		Rime.Order = 1;
		Rime.Hazard = EGFBeatHopCourseHazard::Ice;
		Rime.Revenge = EGFBeatHopRevengeRole::Chase;
		Rime.RevengeSpecies = TEXT("Raptor");
		Rime.WeaveEvery = 4;
		Rime.WeaveLanes = 1;
		Rime.GapScale = 1.f;
		Rime.SkidCmPerSec = 280.f;
		Rime.Tint = FLinearColor(0.209f, 0.578f, 0.888f);
		Out.Add(Rime);

		FGFBeatHopCourseDef Cinder;
		Cinder.Id = TEXT("Cinder");
		Cinder.Title = TEXT("Cinder Drop");
		Cinder.Order = 2;
		Cinder.Hazard = EGFBeatHopCourseHazard::Lava;
		Cinder.Revenge = EGFBeatHopRevengeRole::KnockOff;
		Cinder.RevengeSpecies = TEXT("Scythepaw");
		Cinder.GapScale = 1.4f;
		Cinder.Tint = FLinearColor(1.f, 0.254f, 0.13f);
		Out.Add(Cinder);

		return Out;
	}();
	return Courses;
}

const FGFBeatHopCourseDef& FGFBeatHopCourses::Default()
{
	return All()[0];
}

const FGFBeatHopCourseDef* FGFBeatHopCourses::Find(FName Id)
{
	for (const FGFBeatHopCourseDef& Course : All())
	{
		if (Course.Id == Id)
		{
			return &Course;
		}
	}
	return nullptr;
}

FVector2D FGFBeatHopCourses::Place(const FGFBeatHopCourseDef& Course, int32 BrickIndex, double ChartX, double ChartY, float LaneWidthCm)
{
	const double Scale = FMath::Max(0.1, static_cast<double>(Course.GapScale));
	double Y = ChartY;
	if (Course.WeaveEvery > 0 && Course.WeaveLanes != 0)
	{
		const int32 Flip = ((FMath::Max(0, BrickIndex) / Course.WeaveEvery) % 2) == 0 ? 1 : -1;
		Y += static_cast<double>(Flip * Course.WeaveLanes) * static_cast<double>(LaneWidthCm);
	}
	return FVector2D(ChartX * Scale, Y);
}

FVector FGFBeatHopCourses::ApplySkid(const FVector& Velocity, const FVector& Right, float SkidCmPerSec, float DeltaSeconds)
{
	if (SkidCmPerSec <= 0.f || DeltaSeconds <= 0.f)
	{
		return Velocity;
	}
	const FVector Side = Right.GetSafeNormal2D();
	if (Side.IsNearlyZero())
	{
		return Velocity;
	}
	FVector Out = Velocity;
	const double Along = FVector::DotProduct(Out, Side);
	const double Cap = static_cast<double>(SkidCmPerSec);
	const double Target = FMath::Clamp(Along + Cap * static_cast<double>(DeltaSeconds), -Cap, Cap);
	Out += Side * (Target - Along);
	return Out;
}

FName FGFBeatHopRescueRules::Species()
{
	return TEXT("Ripwing");
}

FName FGFBeatHopRescueRules::CostItem()
{
	return TEXT("GlowDust");
}

const FGFBeatHopRescueBook::FHire* FGFBeatHopRescueBook::Find(const FString& PlayerKey) const
{
	return Hires.FindByPredicate([&](const FHire& Hire) { return Hire.PlayerKey == PlayerKey; });
}

bool FGFBeatHopRescueBook::Hire(const FString& PlayerKey, int32 Catches)
{
	if (PlayerKey.IsEmpty() || Catches <= 0 || Find(PlayerKey))
	{
		return false;
	}
	FHire& Hire = Hires.AddDefaulted_GetRef();
	Hire.PlayerKey = PlayerKey;
	Hire.CatchesLeft = Catches;
	return true;
}

bool FGFBeatHopRescueBook::SpendCatch(const FString& PlayerKey)
{
	FHire* Hire = Hires.FindByPredicate([&](const FHire& Row) { return Row.PlayerKey == PlayerKey; });
	if (!Hire || Hire->CatchesLeft <= 0)
	{
		return false;
	}
	--Hire->CatchesLeft;
	return true;
}

void FGFBeatHopRescueBook::Reset()
{
	Hires.Reset();
}

void FGFBeatHopRevengeState::Reset(const FGFBeatHopCourseDef& Course, float StationX, float RunnerX)
{
	Knocks = 0;
	Knocked.Reset();
	bSpawned = Course.Revenge != EGFBeatHopRevengeRole::None;
	if (!bSpawned)
	{
		Local = FVector::ZeroVector;
		return;
	}
	if (Course.Revenge == EGFBeatHopRevengeRole::Chase)
	{
		// Behind the runner, on the lane, so the chase has somewhere to come from.
		Local = FVector(RunnerX - 800.f, 0.f, 80.f);
	}
	else
	{
		// Off to the side of a post on the lava, waiting for someone to pass.
		Local = FVector(StationX, 280.f, 80.f);
	}
}

FGFBeatHopRevengeState::FStep FGFBeatHopRevengeState::Step(const FString& PlayerKey, const FVector& RunnerLocal, float DeltaSeconds, const FGFBeatHopCourseDef& Course)
{
	FStep Out;
	if (!bSpawned || DeltaSeconds <= 0.f || Course.Revenge == EGFBeatHopRevengeRole::None)
	{
		return Out;
	}
	if (Course.Revenge == EGFBeatHopRevengeRole::Chase)
	{
		const FVector Goal = RunnerLocal + FVector(0.f, 0.f, 40.f);
		const FVector Delta = Goal - Local;
		const double Dist = Delta.Size();
		if (Dist > 1.0)
		{
			const double StepCm = 900.0 * static_cast<double>(DeltaSeconds);
			Local += Delta.GetSafeNormal() * FMath::Min(StepCm, Dist);
		}
		Out.bChasing = true;
		if (Dist < 180.f)
		{
			const float Sign = RunnerLocal.Y >= Local.Y ? 1.f : -1.f;
			Out.Push = FVector(0.f, Sign * 500.f, 0.f);
		}
		return Out;
	}

	// KnockOff: slide onto the runner's lane, then swat once. Alpha matches FInterpTo at speed 4.
	const double Alpha = FMath::Clamp(static_cast<double>(DeltaSeconds) * 4.0, 0.0, 1.0);
	Local.Y += (RunnerLocal.Y - Local.Y) * Alpha;
	const float Dx = FMath::Abs(RunnerLocal.X - Local.X);
	const float Dy = FMath::Abs(RunnerLocal.Y - Local.Y);
	if (Dx < 160.f && Dy < 140.f && !PlayerKey.IsEmpty() && !Knocked.Contains(PlayerKey))
	{
		Knocked.Add(PlayerKey);
		++Knocks;
		Out.bKnocked = true;
		Out.Push = FVector(0.f, 1400.f, 200.f);
	}
	return Out;
}
