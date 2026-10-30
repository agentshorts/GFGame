#include "GFBeatmap.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/FileHelper.h"

double FGFBeatmap::GetLastBeat() const
{
	double Last = 0.0;
	for (const FGFBeatNote& N : Notes)
	{
		Last = FMath::Max(Last, N.Beat + (N.Type == EGFBeatNoteType::Hold ? N.LenBeats : 0.0));
	}
	return Last;
}

int32 FGFBeatmap::CountJudgedNotes() const
{
	int32 Count = 0;
	for (const FGFBeatNote& N : Notes)
	{
		Count += N.IsJudged() ? 1 : 0;
	}
	return Count;
}

const TCHAR* FGFBeatmapIO::DifficultyToString(EGFBeatDifficulty D)
{
	switch (D)
	{
	case EGFBeatDifficulty::Easy: return TEXT("Easy");
	case EGFBeatDifficulty::Hard: return TEXT("Hard");
	default: return TEXT("Normal");
	}
}

bool FGFBeatmapIO::DifficultyFromString(const FString& S, EGFBeatDifficulty& Out)
{
	if (S.Equals(TEXT("Easy"), ESearchCase::IgnoreCase)) { Out = EGFBeatDifficulty::Easy; return true; }
	if (S.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) { Out = EGFBeatDifficulty::Normal; return true; }
	if (S.Equals(TEXT("Hard"), ESearchCase::IgnoreCase)) { Out = EGFBeatDifficulty::Hard; return true; }
	return false;
}

const TCHAR* FGFBeatmapIO::NoteTypeToString(EGFBeatNoteType T)
{
	switch (T)
	{
	case EGFBeatNoteType::Hold: return TEXT("Hold");
	case EGFBeatNoteType::Gap: return TEXT("Gap");
	case EGFBeatNoteType::Pad: return TEXT("Pad");
	case EGFBeatNoteType::Switch: return TEXT("Switch");
	default: return TEXT("Tap");
	}
}

bool FGFBeatmapIO::NoteTypeFromString(const FString& S, EGFBeatNoteType& Out)
{
	static const EGFBeatNoteType All[] = { EGFBeatNoteType::Tap, EGFBeatNoteType::Hold, EGFBeatNoteType::Gap, EGFBeatNoteType::Pad, EGFBeatNoteType::Switch };
	for (EGFBeatNoteType T : All)
	{
		if (S.Equals(NoteTypeToString(T), ESearchCase::IgnoreCase))
		{
			Out = T;
			return true;
		}
	}
	return false;
}

bool FGFBeatmapIO::FromJson(const FString& Json, FGFBeatmap& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not valid JSON");
		return false;
	}
	int32 Format = 1;
	Root->TryGetNumberField(TEXT("format"), Format);
	if (Format < 1 || Format > FormatVersion)
	{
		OutError = FString::Printf(TEXT("unsupported format %d"), Format);
		return false;
	}

	FGFBeatmap Map;
	FString Song;
	if (!Root->TryGetStringField(TEXT("song"), Song) || Song.IsEmpty())
	{
		OutError = TEXT("missing \"song\"");
		return false;
	}
	Map.SongId = FName(*Song);
	Root->TryGetStringField(TEXT("title"), Map.Title);
	FString Diff;
	if (!Root->TryGetStringField(TEXT("difficulty"), Diff) || !DifficultyFromString(Diff, Map.Difficulty))
	{
		OutError = TEXT("missing or unknown \"difficulty\" (Easy, Normal, Hard)");
		return false;
	}
	double Number = 0.0;
	if (!Root->TryGetNumberField(TEXT("bpm"), Number))
	{
		OutError = TEXT("missing \"bpm\"");
		return false;
	}
	Map.Bpm = static_cast<float>(Number);
	if (Root->TryGetNumberField(TEXT("offset_ms"), Number)) { Map.OffsetMs = static_cast<float>(Number); }
	if (Root->TryGetNumberField(TEXT("lanes"), Number)) { Map.Lanes = static_cast<int32>(Number); }
	if (Root->TryGetNumberField(TEXT("ref_speed_cms"), Number)) { Map.RefSpeedCms = static_cast<float>(Number); }

	const TArray<TSharedPtr<FJsonValue>>* Notes = nullptr;
	if (!Root->TryGetArrayField(TEXT("notes"), Notes) || !Notes)
	{
		OutError = TEXT("missing \"notes\"");
		return false;
	}
	for (int32 i = 0; i < Notes->Num(); ++i)
	{
		const TSharedPtr<FJsonObject>* NoteObj = nullptr;
		if (!(*Notes)[i].IsValid() || !(*Notes)[i]->TryGetObject(NoteObj) || !NoteObj)
		{
			OutError = FString::Printf(TEXT("note %d is not an object"), i);
			return false;
		}
		FGFBeatNote Note;
		double Beat = 0.0, Lane = 0.0;
		if (!(*NoteObj)->TryGetNumberField(TEXT("beat"), Beat) || !(*NoteObj)->TryGetNumberField(TEXT("lane"), Lane))
		{
			OutError = FString::Printf(TEXT("note %d needs \"beat\" and \"lane\""), i);
			return false;
		}
		Note.Beat = Beat;
		Note.Lane = static_cast<int32>(Lane);
		FString Type = TEXT("Tap");
		(*NoteObj)->TryGetStringField(TEXT("type"), Type);
		if (!NoteTypeFromString(Type, Note.Type))
		{
			OutError = FString::Printf(TEXT("note %d has unknown type \"%s\""), i, *Type);
			return false;
		}
		double Len = 0.0;
		if ((*NoteObj)->TryGetNumberField(TEXT("len_beats"), Len)) { Note.LenBeats = static_cast<float>(Len); }
		Map.Notes.Add(Note);
	}
	Out = MoveTemp(Map);
	return true;
}

FString FGFBeatmapIO::ToJson(const FGFBeatmap& Map)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("format"), FormatVersion);
	Root->SetStringField(TEXT("song"), Map.SongId.ToString());
	Root->SetStringField(TEXT("title"), Map.Title);
	Root->SetStringField(TEXT("difficulty"), DifficultyToString(Map.Difficulty));
	Root->SetNumberField(TEXT("bpm"), Map.Bpm);
	Root->SetNumberField(TEXT("offset_ms"), Map.OffsetMs);
	Root->SetNumberField(TEXT("lanes"), Map.Lanes);
	Root->SetNumberField(TEXT("ref_speed_cms"), Map.RefSpeedCms);
	TArray<TSharedPtr<FJsonValue>> Notes;
	for (const FGFBeatNote& N : Map.Notes)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("beat"), N.Beat);
		O->SetNumberField(TEXT("lane"), N.Lane);
		O->SetStringField(TEXT("type"), NoteTypeToString(N.Type));
		if (N.Type == EGFBeatNoteType::Hold)
		{
			O->SetNumberField(TEXT("len_beats"), N.LenBeats);
		}
		Notes.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("notes"), Notes);
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root, Writer);
	return Out;
}

bool FGFBeatmapIO::LoadFile(const FString& Path, FGFBeatmap& Out, FString& OutError)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = TEXT("could not read file");
		return false;
	}
	return FromJson(Json, Out, OutError);
}

TArray<FString> FGFBeatmapIO::Validate(const FGFBeatmap& Map, float MaxHopCm)
{
	TArray<FString> Problems;
	const FString Name = FString::Printf(TEXT("%s/%s"), *Map.SongId.ToString(), DifficultyToString(Map.Difficulty));
	if (Map.SongId.IsNone()) { Problems.Add(TEXT("no song id")); }
	if (Map.Bpm < 40.f || Map.Bpm > 300.f) { Problems.Add(FString::Printf(TEXT("%s: bpm %.1f outside 40-300"), *Name, Map.Bpm)); }
	if (Map.Lanes < 3 || Map.Lanes > 5) { Problems.Add(FString::Printf(TEXT("%s: %d lanes (3-5 allowed)"), *Name, Map.Lanes)); }
	if (Map.RefSpeedCms <= 0.f) { Problems.Add(FString::Printf(TEXT("%s: ref speed must be positive"), *Name)); }
	if (Map.CountJudgedNotes() == 0) { Problems.Add(FString::Printf(TEXT("%s: no judged notes"), *Name)); }

	const FGFBeatNote* PrevBrick = nullptr;
	for (int32 i = 0; i < Map.Notes.Num(); ++i)
	{
		const FGFBeatNote& N = Map.Notes[i];
		if (N.Beat < 0.0) { Problems.Add(FString::Printf(TEXT("%s: note %d before beat 0"), *Name, i)); }
		if (N.Lane < 0 || N.Lane >= Map.Lanes) { Problems.Add(FString::Printf(TEXT("%s: note %d lane %d out of range"), *Name, i, N.Lane)); }
		if (N.Type == EGFBeatNoteType::Hold && N.LenBeats <= 0.f) { Problems.Add(FString::Printf(TEXT("%s: hold %d has no length"), *Name, i)); }
		if (N.Type != EGFBeatNoteType::Hold && N.LenBeats != 0.f) { Problems.Add(FString::Printf(TEXT("%s: note %d has a length but is not a Hold"), *Name, i)); }
		if (i > 0)
		{
			const FGFBeatNote& P = Map.Notes[i - 1];
			if (N.Beat < P.Beat) { Problems.Add(FString::Printf(TEXT("%s: note %d not sorted by beat"), *Name, i)); }
			else if (FMath::IsNearlyEqual(N.Beat, P.Beat) && N.Lane == P.Lane) { Problems.Add(FString::Printf(TEXT("%s: notes %d and %d overlap"), *Name, i - 1, i)); }
		}
		if (!N.HasBrick())
		{
			continue;
		}
		if (PrevBrick)
		{
			const double PrevEnd = PrevBrick->Beat + (PrevBrick->Type == EGFBeatNoteType::Hold ? PrevBrick->LenBeats : 0.0);
			if (PrevEnd > N.Beat + KINDA_SMALL_NUMBER && PrevBrick->Lane == N.Lane)
			{
				Problems.Add(FString::Printf(TEXT("%s: note %d starts inside a hold"), *Name, i));
			}
			const double JumpCm = Map.BeatToDistanceCm(N.Beat - PrevEnd);
			const double Allowed = MaxHopCm * (PrevBrick->Type == EGFBeatNoteType::Pad ? PadHopScale : 1.f);
			if (JumpCm > Allowed + 0.5)
			{
				Problems.Add(FString::Printf(TEXT("%s: jump to note %d is %.0f cm (max %.0f)"), *Name, i, JumpCm, Allowed));
			}
		}
		PrevBrick = &N;
	}
	return Problems;
}
