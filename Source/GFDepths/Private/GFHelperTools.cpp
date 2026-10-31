#include "GFHelperTools.h"

const FName FGFHelperTools::CatchPlayer(TEXT("catch_player"));
const FName FGFHelperTools::FloatWater(TEXT("float_water_area"));
const FName FGFHelperTools::FlyOver(TEXT("fly_player_over"));

namespace
{
	static constexpr int32 ArgLimit = 64;

	struct FSpeciesRow
	{
		const TCHAR* Id;
		EGFHelperLocomotion Kind;
	};

	static const FSpeciesRow Species[] = {
		{ TEXT("lanternmaw"), EGFHelperLocomotion::Water },
		{ TEXT("ripwing"), EGFHelperLocomotion::Flyer },
		{ TEXT("tusksaw"), EGFHelperLocomotion::Land },
		{ TEXT("scythepaw"), EGFHelperLocomotion::Land },
		{ TEXT("big-grumble"), EGFHelperLocomotion::Land },
		{ TEXT("chatty-bird"), EGFHelperLocomotion::Land },
	};

	static FString SpeciesKey(FString Id)
	{
		Id.TrimStartAndEndInline();
		Id.ToLowerInline();
		Id.ReplaceInline(TEXT(" "), TEXT("-"));
		Id.ReplaceInline(TEXT("_"), TEXT("-"));
		while (Id.Contains(TEXT("--")))
		{
			Id.ReplaceInline(TEXT("--"), TEXT("-"));
		}
		return Id;
	}

	static const FGFHelperTool* FindTool(FName Id)
	{
		for (const FGFHelperTool& Tool : FGFHelperTools::Catalog())
		{
			if (Tool.Id == Id)
			{
				return &Tool;
			}
		}
		return nullptr;
	}

	static FString JoinVerbs(const TArray<FString>& Verbs)
	{
		if (Verbs.Num() <= 1)
		{
			return Verbs.Num() == 1 ? Verbs[0] : FString();
		}
		if (Verbs.Num() == 2)
		{
			return Verbs[0] + TEXT(" or ") + Verbs[1];
		}
		FString Joined;
		for (int32 i = 0; i < Verbs.Num(); ++i)
		{
			if (i > 0)
			{
				Joined += (i == Verbs.Num() - 1) ? TEXT(", or ") : TEXT(", ");
			}
			Joined += Verbs[i];
		}
		return Joined;
	}

	static FString CleanPlace(const FString& Value)
	{
		FString Out;
		Out.Reserve(Value.Len());
		for (const TCHAR C : Value)
		{
			if (C >= 32)
			{
				Out.AppendChar(C);
			}
		}
		Out.TrimStartAndEndInline();
		return Out.Len() > ArgLimit ? Out.Left(ArgLimit) : Out;
	}

	static FString ArgHint(FName Id)
	{
		if (Id == FGFHelperTools::CatchPlayer)
		{
			return TEXT("catch_player may include where");
		}
		if (Id == FGFHelperTools::FloatWater)
		{
			return TEXT("float_water_area needs area");
		}
		return TEXT("fly_player_over needs level");
	}

	static bool ReadArg(const FString& Raw, bool bPresent, bool bRequired, const TCHAR* Key, FString& Out, FString& Detail)
	{
		if (!bPresent)
		{
			if (bRequired)
			{
				Detail = FString::Printf(TEXT("%s is required"), Key);
				return false;
			}
			return true;
		}
		FString Text = Raw;
		Text.TrimStartAndEndInline();
		if (Text.IsEmpty())
		{
			if (bRequired)
			{
				Detail = FString::Printf(TEXT("%s is required"), Key);
				return false;
			}
			return true;
		}
		if (Text.Len() > ArgLimit)
		{
			Detail = FString::Printf(TEXT("%s is too long"), Key);
			return false;
		}
		for (const TCHAR C : Text)
		{
			if (C < 32)
			{
				Detail = FString::Printf(TEXT("%s has control characters"), Key);
				return false;
			}
		}
		Out = Text;
		return true;
	}
}

const TArray<FGFHelperTool>& FGFHelperTools::Catalog()
{
	static const TArray<FGFHelperTool> Tools = []()
	{
		TArray<FGFHelperTool> Out;
		auto Add = [&](FName Id, const TCHAR* Offer, const TCHAR* Behavior, EGFHelperLocomotion Kind, const TCHAR* Verb, const TCHAR* Summary)
		{
			FGFHelperTool& Tool = Out.AddDefaulted_GetRef();
			Tool.Id = Id;
			Tool.Offer = Offer;
			Tool.BehaviorId = Behavior;
			Tool.Locomotion = Kind;
			Tool.Verb = Verb;
			Tool.Summary = Summary;
		};
		Add(FGFHelperTools::CatchPlayer, TEXT("Want me to catch you?"), TEXT("catch-player"), EGFHelperLocomotion::Flyer, TEXT("catch you"), TEXT("Catch the player. A flyer can carry them."));
		Add(FGFHelperTools::FloatWater, TEXT("Want me to float the water area?"), TEXT("float-water-area"), EGFHelperLocomotion::Water, TEXT("float the water area"), TEXT("Float a water area so the player can cross. Water dinos do this."));
		Add(FGFHelperTools::FlyOver, TEXT("Want me to just fly you over?"), TEXT("fly-player-over"), EGFHelperLocomotion::Flyer, TEXT("just fly you over"), TEXT("Carry the player over a gap. A flyer does this."));
		return Out;
	}();
	return Tools;
}

EGFHelperLocomotion FGFHelperTools::LocomotionFor(const FString& DinoId)
{
	const FString Key = SpeciesKey(DinoId);
	for (const FSpeciesRow& Row : Species)
	{
		if (Key == Row.Id)
		{
			return Row.Kind;
		}
	}
	return EGFHelperLocomotion::Land;
}

FGFHelperBearer FGFHelperTools::BearerFor(const FString& DinoId)
{
	FGFHelperBearer Bearer;
	Bearer.Id = DinoId;
	Bearer.Traits.Add(LocomotionFor(DinoId));
	return Bearer;
}

FGFHelperBearer FGFHelperTools::BearerFor(const FString& DinoId, std::initializer_list<EGFHelperLocomotion> Traits)
{
	FGFHelperBearer Bearer;
	Bearer.Id = DinoId;
	for (const EGFHelperLocomotion Trait : Traits)
	{
		Bearer.Traits.AddUnique(Trait);
	}
	if (Bearer.Traits.Num() == 0)
	{
		Bearer.Traits.Add(EGFHelperLocomotion::Land);
	}
	return Bearer;
}

TArray<FGFHelperTool> FGFHelperTools::ToolsFor(const FGFHelperBearer& Bearer)
{
	TArray<EGFHelperLocomotion> Traits = Bearer.Traits;
	if (Traits.Num() == 0)
	{
		Traits.Add(EGFHelperLocomotion::Land);
	}
	TArray<FGFHelperTool> Out;
	for (const FGFHelperTool& Tool : Catalog())
	{
		if (Traits.Contains(Tool.Locomotion))
		{
			Out.Add(Tool);
		}
	}
	return Out;
}

FString FGFHelperTools::UnpromptedOffer(const FGFHelperBearer& Bearer, const FString& Level)
{
	const TArray<FGFHelperTool> Tools = ToolsFor(Bearer);
	if (Tools.Num() == 0)
	{
		return FString();
	}
	TArray<FString> Verbs;
	for (const FGFHelperTool& Tool : Tools)
	{
		Verbs.Add(Tool.Verb);
	}
	const FString Clean = CleanPlace(Level);
	const FString Place = Clean.IsEmpty() ? TEXT("this level") : Clean;
	return FString::Printf(TEXT("I can see you're on %s, do you want me to %s?"), *Place, *JoinVerbs(Verbs));
}

FString FGFHelperTools::SystemLine(const FGFHelperBearer& Bearer)
{
	const TArray<FGFHelperTool> Tools = ToolsFor(Bearer);
	if (Tools.Num() == 0)
	{
		return TEXT("This dino cannot offer a helper tool. Reply {\"name\":null}.");
	}
	FString Names;
	FString Hints;
	for (int32 i = 0; i < Tools.Num(); ++i)
	{
		if (i > 0)
		{
			Names += TEXT(", ");
			Hints += TEXT(". ");
		}
		Names += Tools[i].Id.ToString();
		Hints += ArgHint(Tools[i].Id);
	}
	return FString::Printf(
		TEXT("The player asked for help in plain words. Reply with one JSON object only: {\"name\":\"tool_name\",\"arguments\":{}}. name must be one of: %s. Use null when none of them fit. %s."),
		*Names,
		*Hints);
}

FGFHelperResolution FGFHelperTools::Resolve(const FGFHelperBearer& Bearer, FName ToolId, const FGFHelperArgs& Args)
{
	FGFHelperResolution Out;
	if (ToolId.IsNone())
	{
		Out.bOk = true;
		Out.bDecline = true;
		return Out;
	}
	const FGFHelperTool* Tool = FindTool(ToolId);
	if (!Tool)
	{
		Out.Fail = EGFHelperFail::UnknownTool;
		Out.Detail = ToolId.ToString();
		return Out;
	}
	bool bOffered = false;
	for (const FGFHelperTool& Offered : ToolsFor(Bearer))
	{
		if (Offered.Id == Tool->Id)
		{
			bOffered = true;
			break;
		}
	}
	if (!bOffered)
	{
		Out.Fail = EGFHelperFail::NotOffered;
		Out.Detail = FString::Printf(TEXT("%s cannot call %s"), *Bearer.Id, *Tool->Id.ToString());
		return Out;
	}

	auto Take = [&](const FString& Raw, bool bPresent, bool bRequired, const TCHAR* Key, FString& Dest, bool& bSet) -> bool
	{
		FString Value;
		if (!ReadArg(Raw, bPresent, bRequired, Key, Value, Out.Detail))
		{
			return false;
		}
		if (!Value.IsEmpty())
		{
			Dest = Value;
			bSet = true;
		}
		return true;
	};

	FGFHelperArgs Clean;
	const bool bCatch = Tool->Id == CatchPlayer;
	const bool bFloat = Tool->Id == FloatWater;
	const bool bFly = Tool->Id == FlyOver;
	if (!Take(Args.Where, bCatch && Args.bWhere, false, TEXT("where"), Clean.Where, Clean.bWhere)
		|| !Take(Args.Area, bFloat && Args.bArea, bFloat, TEXT("area"), Clean.Area, Clean.bArea)
		|| !Take(Args.Level, bFly && Args.bLevel, bFly, TEXT("level"), Clean.Level, Clean.bLevel))
	{
		Out.Fail = EGFHelperFail::BadArgs;
		return Out;
	}

	Out.bOk = true;
	Out.ToolId = Tool->Id;
	Out.BehaviorId = Tool->BehaviorId;
	Out.Offer = Tool->Offer;
	Out.Args = Clean;
	return Out;
}
