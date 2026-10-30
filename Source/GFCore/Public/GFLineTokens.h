#pragma once

// Tokens shared by chat line templates (data tables and config strings). Lines never hard-code player names:
// they carry {Target} (who the line is about) and {Dragger} (who turned that player's protection off).
#include "CoreMinimal.h"

struct FGFLineTokens
{
	static constexpr const TCHAR* Target = TEXT("{Target}");
	static constexpr const TCHAR* Dragger = TEXT("{Dragger}");
	static constexpr const TCHAR* Group = TEXT("{Faction}");

	/**
	 * Replace Token with Name. With no name, a token after a comma is dropped with the comma
	 * ("Out you go, {Dragger}!" -> "Out you go!") and any other use reads as Fallback
	 * ("{Dragger} did it" -> "someone did it"). Case-insensitive.
	 */
	static FString Fill(const FString& Line, const TCHAR* Token, const FString& Name, const TCHAR* Fallback = TEXT("someone"))
	{
		if (!Name.IsEmpty())
		{
			return Line.Replace(Token, *Name, ESearchCase::IgnoreCase);
		}
		FString Out = Line;
		const FString Tok(Token);
		Out.ReplaceInline(*(TEXT(", ") + Tok), TEXT(""), ESearchCase::IgnoreCase);
		Out.ReplaceInline(*Tok, Fallback, ESearchCase::IgnoreCase);
		return Out;
	}

	static FString FillDragger(const FString& Line, const FString& DraggerName) { return Fill(Line, Dragger, DraggerName); }
};
