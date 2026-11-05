#include "GFAccelHopTutorialRules.h"

namespace
{
	FGFAccelHopLessonSpec MakeLesson(EGFAccelHopLesson Lesson, const TCHAR* Id, const TCHAR* Title, const TCHAR* Body, int32 Goal)
	{
		FGFAccelHopLessonSpec Spec;
		Spec.Lesson = Lesson;
		Spec.Id = Id;
		Spec.Title = Title;
		Spec.Body = Body;
		Spec.Goal = Goal;
		return Spec;
	}
}

const TArray<FGFAccelHopLessonSpec>& FGFAccelHopTutorialRules::Lessons()
{
	// Design defaults (#348, recorded in the PR). Goals are small so the whole tutorial takes about two minutes.
	static const TArray<FGFAccelHopLessonSpec> Specs = {
		MakeLesson(EGFAccelHopLesson::Beat, TEXT("Beat"), TEXT("Hop on the beat"),
			TEXT("Jump so you land on the thump. Your boots know the rhythm."), 4),
		MakeLesson(EGFAccelHopLesson::Steer, TEXT("Steer"), TEXT("Steer in the air"),
			TEXT("Mid-hop, look where you want to land. Zig, then zag."), 3),
		MakeLesson(EGFAccelHopLesson::Lit, TEXT("Lit"), TEXT("Hit the lit tile"),
			TEXT("Land on the magenta diamond. Big boost, bigger grin."), 2),
		MakeLesson(EGFAccelHopLesson::Chain, TEXT("Chain"), TEXT("Put it all together"),
			TEXT("Beat, steer, light. Keep the chain alive to the gate."), 6),
	};
	return Specs;
}

FName FGFAccelHopTutorialRules::LessonTag(int32 Index)
{
	const TArray<FGFAccelHopLessonSpec>& Specs = Lessons();
	if (!Specs.IsValidIndex(Index))
	{
		return NAME_None;
	}
	return FName(*(FString(TEXT("GFAccelHopTutorial.Lesson.")) + Specs[Index].Id.ToString()));
}

int32 FGFAccelHopTutorialRules::LessonFromTags(const TArray<FName>& Tags)
{
	for (int32 Index = 0; Index < LessonCount; ++Index)
	{
		if (Tags.Contains(LessonTag(Index)))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FGFAccelHopTutorialRules::IsInTime(EGFJudgement Judgement)
{
	return Judgement == EGFJudgement::Perfect || Judgement == EGFJudgement::Good;
}

int32 FGFAccelHopTutorialRules::SideOf(float LocalY)
{
	if (LocalY > SideDeadZoneCm)
	{
		return 1;
	}
	if (LocalY < -SideDeadZoneCm)
	{
		return -1;
	}
	return 0;
}

void FGFAccelHopTutorialRules::EnterLesson(FGFAccelHopTutorialProgress& Progress, int32 Index)
{
	const int32 Serial = Progress.Serial;
	Progress = FGFAccelHopTutorialProgress();
	Progress.Lesson = FMath::Clamp(Index, 0, LessonCount);
	Progress.Serial = Serial + 1;
}

bool FGFAccelHopTutorialRules::OnReachLesson(FGFAccelHopTutorialProgress& Progress, int32 Index)
{
	if (Index == INDEX_NONE || Index <= Progress.Lesson || IsDone(Progress))
	{
		return false;
	}
	EnterLesson(Progress, Index);
	return true;
}

bool FGFAccelHopTutorialRules::IsLessonComplete(const FGFAccelHopTutorialProgress& Progress)
{
	const TArray<FGFAccelHopLessonSpec>& Specs = Lessons();
	if (!Specs.IsValidIndex(Progress.Lesson))
	{
		return true;
	}
	const FGFAccelHopLessonSpec& Spec = Specs[Progress.Lesson];
	if (Spec.Lesson == EGFAccelHopLesson::Chain)
	{
		return Progress.BestChain >= Spec.Goal && Progress.Zigs >= ChainZigGoal && Progress.Lits >= ChainLitGoal;
	}
	return Progress.Count >= Spec.Goal;
}

bool FGFAccelHopTutorialRules::OnLanding(FGFAccelHopTutorialProgress& Progress, const FGFAccelHopTutorialLanding& Landing)
{
	if (IsDone(Progress) || Landing.LessonIndex == INDEX_NONE || Landing.Judgement == EGFJudgement::None)
	{
		return false;
	}
	// A later lesson's tiles skip ahead; an earlier lesson's tiles (walking back) do nothing.
	OnReachLesson(Progress, Landing.LessonIndex);
	if (Landing.LessonIndex != Progress.Lesson)
	{
		return false;
	}
	const bool bInTime = IsInTime(Landing.Judgement);
	const bool bSwitched = Landing.Side != 0 && Progress.LastSide != 0 && Landing.Side != Progress.LastSide;
	switch (Lessons()[Progress.Lesson].Lesson)
	{
	case EGFAccelHopLesson::Beat:
		Progress.Count += bInTime ? 1 : 0;
		break;
	case EGFAccelHopLesson::Steer:
		// Steering is the lesson, so the beat is not checked here: any judged landing on the other side counts.
		Progress.Count += bSwitched ? 1 : 0;
		break;
	case EGFAccelHopLesson::Lit:
		Progress.Count += Landing.bLit ? 1 : 0;
		break;
	case EGFAccelHopLesson::Chain:
		Progress.Chain = bInTime ? Progress.Chain + 1 : 0;
		Progress.BestChain = FMath::Max(Progress.BestChain, Progress.Chain);
		Progress.Zigs += bSwitched ? 1 : 0;
		Progress.Lits += Landing.bLit ? 1 : 0;
		break;
	default:
		break;
	}
	if (Landing.Side != 0)
	{
		Progress.LastSide = Landing.Side;
	}
	if (!IsLessonComplete(Progress))
	{
		return false;
	}
	EnterLesson(Progress, Progress.Lesson + 1);
	return true;
}

FGFAccelHopTutorialPrompt FGFAccelHopTutorialRules::PromptFor(const FGFAccelHopTutorialProgress& Progress)
{
	FGFAccelHopTutorialPrompt Prompt;
	const TArray<FGFAccelHopLessonSpec>& Specs = Lessons();
	if (IsDone(Progress) || !Specs.IsValidIndex(Progress.Lesson))
	{
		Prompt.bDone = true;
		Prompt.Step = TEXT("Done");
		Prompt.Title = TEXT("Hop certified");
		Prompt.Body = TEXT("That is the whole trick. The HUB gate at the end takes you home.");
		Prompt.Goal = TEXT("Run it again any time.");
		return Prompt;
	}
	const FGFAccelHopLessonSpec& Spec = Specs[Progress.Lesson];
	Prompt.Step = FString::Printf(TEXT("%d/%d"), Progress.Lesson + 1, LessonCount);
	Prompt.Title = Spec.Title;
	Prompt.Body = Spec.Body;
	switch (Spec.Lesson)
	{
	case EGFAccelHopLesson::Beat:
		Prompt.Goal = FString::Printf(TEXT("On-beat landings %d/%d"), FMath::Min(Progress.Count, Spec.Goal), Spec.Goal);
		break;
	case EGFAccelHopLesson::Steer:
		Prompt.Goal = FString::Printf(TEXT("Zigzag hops %d/%d"), FMath::Min(Progress.Count, Spec.Goal), Spec.Goal);
		break;
	case EGFAccelHopLesson::Lit:
		Prompt.Goal = FString::Printf(TEXT("Lit landings %d/%d"), FMath::Min(Progress.Count, Spec.Goal), Spec.Goal);
		break;
	case EGFAccelHopLesson::Chain:
		Prompt.Goal = FString::Printf(TEXT("Chain %d/%d   Zigzag %d/%d   Lit %d/%d"),
			FMath::Min(Progress.BestChain, Spec.Goal), Spec.Goal,
			FMath::Min(Progress.Zigs, ChainZigGoal), ChainZigGoal,
			FMath::Min(Progress.Lits, ChainLitGoal), ChainLitGoal);
		break;
	default:
		break;
	}
	return Prompt;
}

TArray<FString> FGFAccelHopTutorialRules::PromptLines(const FGFAccelHopTutorialPrompt& Prompt, bool bFlash)
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("%s%s  %s"), bFlash ? TEXT("Nice!  ") : TEXT(""), *Prompt.Step, *Prompt.Title));
	Lines.Add(Prompt.Body);
	Lines.Add(Prompt.Goal);
	return Lines;
}

int32 FGFAccelHopTutorialRules::LessonAtAlong(const TArray<float>& StartAlong, float Along)
{
	int32 Found = INDEX_NONE;
	for (int32 Index = 0; Index < StartAlong.Num(); ++Index)
	{
		if (StartAlong[Index] <= Along)
		{
			Found = Index;
		}
	}
	return Found;
}
