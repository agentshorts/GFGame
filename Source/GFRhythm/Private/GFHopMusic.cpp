// Acceleration Hop local music (issue 343): the folder scan, WAV reading and the playlist rules. See GFHopMusic.h.
#include "GFHopMusic.h"
#include "GFLog.h"
#include "GFRhythmSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

const TCHAR* const FGFHopMusicLibrary::SubFolder = TEXT("LicensedMusic/AccelHop");

namespace GFHopMusicPrivate
{
	/** Enough of a file to reach the data chunk past any LIST/bext metadata. */
	static constexpr int64 HeaderReadBytes = 512 * 1024;

	static const TCHAR* const OtherAudioExtensions[] = { TEXT("mp3"), TEXT("ogg"), TEXT("flac"), TEXT("m4a"), TEXT("aac"), TEXT("opus"), TEXT("wma"), TEXT("aif"), TEXT("aiff") };

	static bool IsNameSeparator(TCHAR C)
	{
		return C == TEXT(' ') || C == TEXT('_') || C == TEXT('-') || C == TEXT('=') || C == TEXT(':');
	}

	static bool IsNumberChar(TCHAR C)
	{
		return FChar::IsDigit(C) || C == TEXT('.');
	}

	/** "128", "92.5": two or three whole digits, at most one decimal point, inside the tempo range. */
	static bool TryBpmNumber(const FString& Text, float& Out)
	{
		if (Text.IsEmpty() || Text.StartsWith(TEXT(".")) || Text.EndsWith(TEXT(".")))
		{
			return false;
		}
		int32 Dot = INDEX_NONE;
		Text.FindChar(TEXT('.'), Dot);
		if (Dot != INDEX_NONE && Text.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromStart, Dot + 1) != INDEX_NONE)
		{
			return false;
		}
		const int32 Whole = Dot == INDEX_NONE ? Text.Len() : Dot;
		if (Whole < 2 || Whole > 3)
		{
			return false;
		}
		const float Value = FCString::Atof(*Text);
		if (Value < FGFHopMusicLibrary::MinBpm || Value > FGFHopMusicLibrary::MaxBpm)
		{
			return false;
		}
		Out = Value;
		return true;
	}

	static FString TidyTitle(FString Title)
	{
		Title.ReplaceInline(TEXT("_"), TEXT(" "));
		for (const TCHAR* Empty : { TEXT("[ ]"), TEXT("( )"), TEXT("{ }"), TEXT("[]"), TEXT("()"), TEXT("{}") })
		{
			Title.ReplaceInline(Empty, TEXT(""));
		}
		while (Title.ReplaceInline(TEXT("  "), TEXT(" ")) > 0)
		{
		}
		int32 Start = 0;
		int32 End = Title.Len();
		auto IsTrim = [](TCHAR C) { return C == TEXT(' ') || C == TEXT('-') || C == TEXT('.') || C == TEXT(':') || C == TEXT('='); };
		while (Start < End && IsTrim(Title[Start]))
		{
			++Start;
		}
		while (End > Start && IsTrim(Title[End - 1]))
		{
			--End;
		}
		return Title.Mid(Start, End - Start);
	}

	static uint16 ReadU16(const TArray<uint8>& B, int64 At)
	{
		return static_cast<uint16>(B[At] | (B[At + 1] << 8));
	}

	static uint32 ReadU32(const TArray<uint8>& B, int64 At)
	{
		return static_cast<uint32>(B[At]) | (static_cast<uint32>(B[At + 1]) << 8) | (static_cast<uint32>(B[At + 2]) << 16) | (static_cast<uint32>(B[At + 3]) << 24);
	}

	static bool ChunkIs(const TArray<uint8>& B, int64 At, const char* Id)
	{
		return B[At] == Id[0] && B[At + 1] == Id[1] && B[At + 2] == Id[2] && B[At + 3] == Id[3];
	}

	static bool LoadPrefix(const FString& Path, int64 MaxBytes, TArray<uint8>& Out, int64& OutFileSize)
	{
		TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*Path));
		if (!Reader)
		{
			return false;
		}
		OutFileSize = Reader->TotalSize();
		const int64 Count = FMath::Min(OutFileSize, MaxBytes);
		Out.SetNumUninitialized(static_cast<int32>(Count));
		Reader->Serialize(Out.GetData(), Count);
		return Reader->Close() && Count > 0;
	}
}

double FGFWavInfo::DurationSec() const
{
	const int32 Frame = BytesPerFrame();
	return Frame > 0 && SampleRate > 0 ? static_cast<double>(DataBytes / Frame) / SampleRate : 0.0;
}

TArray<FString> FGFHopLibraryScan::Keys() const
{
	TArray<FString> Out;
	for (const FGFHopTrack& T : Tracks)
	{
		Out.Add(T.Key);
	}
	return Out;
}

const FGFHopTrack* FGFHopLibraryScan::Find(const FString& Key) const
{
	return Key.IsEmpty() ? nullptr : Tracks.FindByPredicate([&Key](const FGFHopTrack& T) { return T.Key == Key; });
}

TArray<FString> FGFHopMusicLibrary::CandidateFolders()
{
	TArray<FString> Out;
	FString FromCommandLine;
	if (FParse::Value(FCommandLine::Get(), TEXT("AccelHopMusicDir="), FromCommandLine) && !FromCommandLine.IsEmpty())
	{
		Out.Add(FPaths::ConvertRelativePathToFull(FromCommandLine));
	}
	for (const FString& Root : { FPaths::LaunchDir(), FPaths::ProjectDir(), FPaths::ProjectContentDir() })
	{
		const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::Combine(Root, SubFolder));
		Out.AddUnique(Folder);
	}
	return Out;
}

bool FGFHopMusicLibrary::ParseBpmFromName(const FString& BaseName, float& OutBpm, FString* OutTitle)
{
	using namespace GFHopMusicPrivate;
	const FString Lower = BaseName.ToLower();
	int32 SearchFrom = 0;
	while (true)
	{
		const int32 At = Lower.Find(TEXT("bpm"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (At == INDEX_NONE)
		{
			return false;
		}
		SearchFrom = At + 3;
		const bool bWordAfter = At + 3 >= Lower.Len() || !FChar::IsAlpha(Lower[At + 3]);
		const bool bWordBefore = At == 0 || !FChar::IsAlpha(Lower[At - 1]);

		// "128bpm", "128 BPM", "128_bpm"
		int32 NumEnd = At;
		while (NumEnd > 0 && IsNameSeparator(Lower[NumEnd - 1]))
		{
			--NumEnd;
		}
		int32 NumStart = NumEnd;
		while (NumStart > 0 && IsNumberChar(Lower[NumStart - 1]))
		{
			--NumStart;
		}
		float Value = 0.f;
		if (bWordAfter && NumEnd > NumStart && (NumStart == 0 || !FChar::IsAlpha(Lower[NumStart - 1]))
			&& TryBpmNumber(Lower.Mid(NumStart, NumEnd - NumStart), Value))
		{
			OutBpm = Value;
			if (OutTitle)
			{
				*OutTitle = TidyTitle(BaseName.Left(NumStart) + BaseName.Mid(At + 3));
			}
			return true;
		}

		// "bpm128", "BPM 128", "bpm-128", "bpm=128"
		int32 R = At + 3;
		while (R < Lower.Len() && IsNameSeparator(Lower[R]))
		{
			++R;
		}
		int32 REnd = R;
		while (REnd < Lower.Len() && IsNumberChar(Lower[REnd]))
		{
			++REnd;
		}
		if (bWordBefore && REnd > R && (REnd >= Lower.Len() || !FChar::IsAlnum(Lower[REnd])) && TryBpmNumber(Lower.Mid(R, REnd - R), Value))
		{
			OutBpm = Value;
			if (OutTitle)
			{
				*OutTitle = TidyTitle(BaseName.Left(At) + BaseName.Mid(REnd));
			}
			return true;
		}
	}
}

bool FGFHopMusicLibrary::ApplySidecarJson(const FString& Json, FGFHopTrack& Track)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	double Bpm = 0.0;
	if (!Root->TryGetNumberField(TEXT("bpm"), Bpm) || Bpm < MinBpm || Bpm > MaxBpm)
	{
		return false;
	}
	Track.Bpm = static_cast<float>(Bpm);
	double Offset = 0.0;
	Track.OffsetMs = Root->TryGetNumberField(TEXT("offset_ms"), Offset) ? FMath::Clamp(static_cast<float>(Offset), -2000.f, 10000.f) : 0.f;
	FString Title;
	if (Root->TryGetStringField(TEXT("title"), Title) && !Title.TrimStartAndEnd().IsEmpty())
	{
		Track.Title = Title.TrimStartAndEnd();
	}
	Track.BpmFrom = TEXT("sidecar");
	return true;
}

bool FGFHopMusicLibrary::ReadWavInfo(const TArray<uint8>& Bytes, FGFWavInfo& Out, FString& OutError)
{
	using namespace GFHopMusicPrivate;
	Out = FGFWavInfo();
	if (Bytes.Num() < 12 || !ChunkIs(Bytes, 0, "RIFF") || !ChunkIs(Bytes, 8, "WAVE"))
	{
		OutError = TEXT("not a RIFF/WAVE file");
		return false;
	}
	bool bFmt = false;
	int64 Pos = 12;
	while (Pos + 8 <= Bytes.Num())
	{
		const int64 Size = ReadU32(Bytes, Pos + 4);
		const int64 Body = Pos + 8;
		if (ChunkIs(Bytes, Pos, "fmt "))
		{
			if (Size < 16 || Body + 16 > Bytes.Num())
			{
				OutError = TEXT("short fmt chunk");
				return false;
			}
			int32 Format = ReadU16(Bytes, Body);
			Out.Channels = ReadU16(Bytes, Body + 2);
			Out.SampleRate = static_cast<int32>(ReadU32(Bytes, Body + 4));
			Out.BitsPerSample = ReadU16(Bytes, Body + 14);
			if (Format == 0xFFFE)
			{
				// WAVE_FORMAT_EXTENSIBLE: the real format is the first two bytes of the SubFormat GUID.
				if (Size < 40 || Body + 26 > Bytes.Num())
				{
					OutError = TEXT("short extensible fmt chunk");
					return false;
				}
				Format = ReadU16(Bytes, Body + 24);
			}
			Out.Format = Format;
			bFmt = true;
		}
		else if (ChunkIs(Bytes, Pos, "data"))
		{
			if (!bFmt)
			{
				OutError = TEXT("data chunk before fmt");
				return false;
			}
			Out.DataOffset = Body;
			Out.DataBytes = Size;
			const bool bInt = Out.Format == 1 && (Out.BitsPerSample == 8 || Out.BitsPerSample == 16 || Out.BitsPerSample == 24 || Out.BitsPerSample == 32);
			const bool bFloat = Out.Format == 3 && Out.BitsPerSample == 32;
			if (!bInt && !bFloat)
			{
				OutError = FString::Printf(TEXT("unsupported WAV encoding (format %d, %d bit)"), Out.Format, Out.BitsPerSample);
				return false;
			}
			if (Out.Channels < 1 || Out.Channels > 2 || Out.SampleRate < 8000 || Out.SampleRate > 192000)
			{
				OutError = FString::Printf(TEXT("unsupported WAV layout (%d channels, %d Hz)"), Out.Channels, Out.SampleRate);
				return false;
			}
			return true;
		}
		Pos = Body + Size + (Size & 1);
	}
	OutError = bFmt ? TEXT("no data chunk") : TEXT("no fmt chunk");
	return false;
}

bool FGFHopMusicLibrary::DecodeWav(const TArray<uint8>& Bytes, FGFWavPcm& Out, FString& OutError)
{
	FGFWavInfo Info;
	if (!ReadWavInfo(Bytes, Info, OutError))
	{
		return false;
	}
	const int32 Frame = Info.BytesPerFrame();
	const int64 Available = FMath::Min<int64>(Info.DataBytes, Bytes.Num() - Info.DataOffset);
	const int64 Frames = Frame > 0 ? Available / Frame : 0;
	if (Frames <= 0)
	{
		OutError = TEXT("no audio in the data chunk");
		return false;
	}
	const int64 Count = Frames * Info.Channels;
	if (Count > MAX_int32)
	{
		OutError = TEXT("track too long");
		return false;
	}
	Out.Channels = Info.Channels;
	Out.SampleRate = Info.SampleRate;
	Out.Samples.SetNumUninitialized(static_cast<int32>(Count));
	const uint8* Src = Bytes.GetData() + Info.DataOffset;
	const int32 Step = Info.BitsPerSample / 8;
	for (int64 I = 0; I < Count; ++I)
	{
		const uint8* S = Src + I * Step;
		int16 V = 0;
		if (Info.Format == 3)
		{
			uint32 Raw = static_cast<uint32>(S[0]) | (static_cast<uint32>(S[1]) << 8) | (static_cast<uint32>(S[2]) << 16) | (static_cast<uint32>(S[3]) << 24);
			float F = 0.f;
			FMemory::Memcpy(&F, &Raw, sizeof(float));
			F = FMath::IsFinite(F) ? FMath::Clamp(F, -1.f, 1.f) : 0.f;
			V = static_cast<int16>(FMath::RoundToInt32(F * 32767.f));
		}
		else if (Step == 1)
		{
			V = static_cast<int16>((static_cast<int32>(S[0]) - 128) << 8);
		}
		else
		{
			// Keep the top 16 bits of 16, 24 or 32-bit little-endian samples.
			V = static_cast<int16>(static_cast<uint16>(S[Step - 2]) | (static_cast<uint16>(S[Step - 1]) << 8));
		}
		Out.Samples[static_cast<int32>(I)] = V;
	}
	return true;
}

FGFHopLibraryScan FGFHopMusicLibrary::ScanFolder(const FString& Folder)
{
	using namespace GFHopMusicPrivate;
	FGFHopLibraryScan Scan;
	Scan.Folder = Folder;
	IFileManager& Files = IFileManager::Get();
	if (Folder.IsEmpty() || !Files.DirectoryExists(*Folder))
	{
		return Scan;
	}
	TArray<FString> Names;
	Files.FindFiles(Names, *FPaths::Combine(Folder, TEXT("*")), true, false);
	Names.Sort();
	for (const FString& Name : Names)
	{
		const FString Ext = FPaths::GetExtension(Name).ToLower();
		if (Ext != TEXT("wav"))
		{
			for (const TCHAR* Other : OtherAudioExtensions)
			{
				if (Ext == Other)
				{
					Scan.Skipped.Add(FString::Printf(TEXT("%s: only .wav plays in game (Tools/hop_music_prep.py converts it)"), *Name));
					break;
				}
			}
			continue;
		}
		FGFHopTrack Track;
		Track.Key = Name;
		Track.Path = FPaths::Combine(Folder, Name);
		const FString Base = FPaths::GetBaseFilename(Name);
		FString NameTitle;
		float NameBpm = 0.f;
		const bool bNameBpm = ParseBpmFromName(Base, NameBpm, &NameTitle);
		Track.Title = bNameBpm && !NameTitle.IsEmpty() ? NameTitle : TidyTitle(Base);
		FString Sidecar;
		const FString SidecarPath = FPaths::Combine(Folder, Base + TEXT(".json"));
		const bool bSidecar = Files.FileExists(*SidecarPath) && FFileHelper::LoadFileToString(Sidecar, *SidecarPath) && ApplySidecarJson(Sidecar, Track);
		if (!bSidecar)
		{
			if (!bNameBpm)
			{
				Scan.Skipped.Add(FString::Printf(TEXT("%s: no BPM (add %s.json with {\"bpm\": 120}, or put 120bpm in the file name)"), *Name, *Base));
				continue;
			}
			Track.Bpm = NameBpm;
			Track.OffsetMs = 0.f;
			Track.BpmFrom = TEXT("name");
		}
		TArray<uint8> Head;
		int64 FileSize = 0;
		FGFWavInfo Info;
		FString Error;
		if (!LoadPrefix(Track.Path, HeaderReadBytes, Head, FileSize))
		{
			Scan.Skipped.Add(FString::Printf(TEXT("%s: could not be read"), *Name));
			continue;
		}
		if (!ReadWavInfo(Head, Info, Error))
		{
			Scan.Skipped.Add(FString::Printf(TEXT("%s: %s"), *Name, *Error));
			continue;
		}
		Info.DataBytes = FMath::Min<int64>(Info.DataBytes, FileSize - Info.DataOffset);
		Track.DurationSec = static_cast<float>(Info.DurationSec());
		if (Track.DurationSec <= 0.f)
		{
			Scan.Skipped.Add(FString::Printf(TEXT("%s: no audio"), *Name));
			continue;
		}
		Scan.Tracks.Add(MoveTemp(Track));
	}
	return Scan;
}

FGFHopLibraryScan FGFHopMusicLibrary::ScanFirstUsable(const TArray<FString>& Folders)
{
	FGFHopLibraryScan FirstWithNotes;
	bool bHaveNotes = false;
	for (const FString& Folder : Folders)
	{
		FGFHopLibraryScan Scan = ScanFolder(Folder);
		if (Scan.Tracks.Num() > 0)
		{
			return Scan;
		}
		if (!bHaveNotes && Scan.Skipped.Num() > 0)
		{
			FirstWithNotes = MoveTemp(Scan);
			bHaveNotes = true;
		}
	}
	if (bHaveNotes)
	{
		return FirstWithNotes;
	}
	FGFHopLibraryScan Empty;
	Empty.Folder = Folders.Num() > 0 ? Folders[0] : FString();
	return Empty;
}

TArray<uint8> FGFHopMusicLibrary::MakeTestWav(int32 SampleRate, float Seconds, float Bpm)
{
	const int32 Rate = FMath::Max(8000, SampleRate);
	const int32 Frames = FMath::Max(1, FMath::RoundToInt32(Rate * Seconds));
	const int32 DataBytes = Frames * 2;
	TArray<uint8> Out;
	Out.Reserve(44 + DataBytes);
	auto Tag = [&Out](const char* Id) { Out.Append(reinterpret_cast<const uint8*>(Id), 4); };
	auto U32 = [&Out](uint32 V) { for (int32 I = 0; I < 4; ++I) { Out.Add(static_cast<uint8>((V >> (8 * I)) & 0xFF)); } };
	auto U16 = [&Out](uint16 V) { Out.Add(static_cast<uint8>(V & 0xFF)); Out.Add(static_cast<uint8>(V >> 8)); };
	Tag("RIFF");
	U32(36 + DataBytes);
	Tag("WAVE");
	Tag("fmt ");
	U32(16);
	U16(1);
	U16(1);
	U32(Rate);
	U32(Rate * 2);
	U16(2);
	U16(16);
	Tag("data");
	U32(DataBytes);
	const double BeatSec = Bpm > 0.f ? 60.0 / Bpm : 0.5;
	for (int32 I = 0; I < Frames; ++I)
	{
		const double T = static_cast<double>(I) / Rate;
		const double IntoBeat = FMath::Fmod(T, BeatSec);
		const double Sample = IntoBeat < 0.02 ? 0.5 * FMath::Sin(2.0 * UE_DOUBLE_PI * 1000.0 * T) : 0.0;
		U16(static_cast<uint16>(static_cast<int16>(FMath::RoundToInt32(Sample * 32767.0))));
	}
	return Out;
}

bool FGFHopPlaylistRules::Sync(FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found)
{
	bool bChanged = false;
	for (const FString& Key : Found)
	{
		if (!Prefs.Entries.ContainsByPredicate([&Key](const FGFHopPlaylistEntry& E) { return E.Key == Key; }))
		{
			FGFHopPlaylistEntry Row;
			Row.Key = Key;
			Row.bIncluded = true;
			Prefs.Entries.Add(Row);
			bChanged = true;
		}
	}
	return bChanged;
}

TArray<FString> FGFHopPlaylistRules::ActiveKeys(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found)
{
	TArray<FString> Out;
	for (const FGFHopPlaylistEntry& E : Prefs.Entries)
	{
		if (E.bIncluded && Found.Contains(E.Key))
		{
			Out.AddUnique(E.Key);
		}
	}
	return Out;
}

TArray<FString> FGFHopPlaylistRules::OrderedFound(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found)
{
	TArray<FString> Out;
	for (const FGFHopPlaylistEntry& E : Prefs.Entries)
	{
		if (Found.Contains(E.Key))
		{
			Out.AddUnique(E.Key);
		}
	}
	// Found but not synced yet: still listed, after the saved ones.
	for (const FString& Key : Found)
	{
		Out.AddUnique(Key);
	}
	return Out;
}

FString FGFHopPlaylistRules::PickNext(const TArray<FString>& Active, EGFHopPlayMode Mode, const FString& Current, FGFHopShuffleBag& Bag, FRandomStream& Rng)
{
	if (Active.Num() == 0)
	{
		return FString();
	}
	switch (Mode)
	{
	case EGFHopPlayMode::RepeatOne:
		return Active.Contains(Current) ? Current : Active[0];
	case EGFHopPlayMode::Shuffle:
	{
		if (Active.Num() == 1)
		{
			Bag.Remaining.Reset();
			return Active[0];
		}
		Bag.Remaining.RemoveAll([&Active, &Current](const FString& Key) { return Key == Current || !Active.Contains(Key); });
		if (Bag.Remaining.Num() == 0)
		{
			for (const FString& Key : Active)
			{
				if (Key != Current)
				{
					Bag.Remaining.Add(Key);
				}
			}
		}
		const int32 Pick = Rng.RandRange(0, Bag.Remaining.Num() - 1);
		const FString Key = Bag.Remaining[Pick];
		Bag.Remaining.RemoveAt(Pick);
		return Key;
	}
	case EGFHopPlayMode::InOrder:
	default:
	{
		const int32 At = Active.Find(Current);
		return At == INDEX_NONE ? Active[0] : Active[(At + 1) % Active.Num()];
	}
	}
}

FGFHopChoice FGFHopPlaylistRules::ChooseOnEnter(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Active, float LastDurationSec, FGFHopShuffleBag& Bag, FRandomStream& Rng)
{
	FGFHopChoice Choice;
	if (Active.Num() == 0)
	{
		return Choice;
	}
	const bool bLastActive = !Prefs.LastKey.IsEmpty() && Active.Contains(Prefs.LastKey);
	const bool bTimeLeft = Prefs.ResumeSec >= 0.f && (LastDurationSec <= 0.f || Prefs.ResumeSec < LastDurationSec - ResumeTailSec);
	if (Prefs.bResume && bLastActive && bTimeLeft)
	{
		Choice.Key = Prefs.LastKey;
		Choice.StartSec = FMath::Max(0.f, Prefs.ResumeSec);
		Choice.bResumed = true;
		return Choice;
	}
	if (Prefs.LastKey.IsEmpty())
	{
		Choice.Key = Prefs.Mode == EGFHopPlayMode::Shuffle ? PickNext(Active, EGFHopPlayMode::Shuffle, FString(), Bag, Rng) : Active[0];
		return Choice;
	}
	Choice.Key = PickNext(Active, Prefs.Mode, Prefs.LastKey, Bag, Rng);
	return Choice;
}

FGFHopChoice FGFHopPlaylistRules::ChooseAfterEnd(const FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Active, const FString& Finished, FGFHopShuffleBag& Bag, FRandomStream& Rng)
{
	FGFHopChoice Choice;
	Choice.Key = PickNext(Active, Prefs.Mode, Finished, Bag, Rng);
	return Choice;
}

void FGFHopPlaylistRules::NoteStopped(FGFHopPlaylistPrefs& Prefs, const FString& Key, float PositionSec)
{
	Prefs.LastKey = Key;
	Prefs.ResumeSec = FMath::Max(0.f, PositionSec);
}

void FGFHopPlaylistRules::SetIncluded(FGFHopPlaylistPrefs& Prefs, const FString& Key, bool bIncluded)
{
	for (FGFHopPlaylistEntry& E : Prefs.Entries)
	{
		if (E.Key == Key)
		{
			E.bIncluded = bIncluded;
			return;
		}
	}
	FGFHopPlaylistEntry Row;
	Row.Key = Key;
	Row.bIncluded = bIncluded;
	Prefs.Entries.Add(Row);
}

bool FGFHopPlaylistRules::MoveAmongFound(FGFHopPlaylistPrefs& Prefs, const TArray<FString>& Found, const FString& Key, int32 Delta)
{
	Sync(Prefs, Found);
	TArray<int32> Rows;
	for (int32 I = 0; I < Prefs.Entries.Num(); ++I)
	{
		if (Found.Contains(Prefs.Entries[I].Key))
		{
			Rows.Add(I);
		}
	}
	const int32 From = Rows.IndexOfByPredicate([&Prefs, &Key](int32 Row) { return Prefs.Entries[Row].Key == Key; });
	const int32 To = From + Delta;
	if (From == INDEX_NONE || Delta == 0 || To < 0 || To >= Rows.Num())
	{
		return false;
	}
	Prefs.Entries.Swap(Rows[From], Rows[To]);
	return true;
}

FGFHopMusicDirector::FGFHopMusicDirector()
{
	Rng.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
}

void FGFHopMusicDirector::SetFolderOverride(const FString& Folder)
{
	FolderOverride = Folder;
	Scan = FGFHopLibraryScan();
	bScanned = false;
	Bag.Remaining.Reset();
}

void FGFHopMusicDirector::SetPrefsOverride(FGFHopPlaylistPrefs* InPrefs)
{
	PrefsOverride = InPrefs;
	Bag.Remaining.Reset();
}

FGFHopPlaylistPrefs& FGFHopMusicDirector::Prefs()
{
	return PrefsOverride ? *PrefsOverride : UGFRhythmSettings::GetMutable()->AccelHopPlaylist;
}

void FGFHopMusicDirector::SavePrefs()
{
	if (!PrefsOverride)
	{
		UGFRhythmSettings::GetMutable()->SaveSettings();
	}
}

const FGFHopLibraryScan& FGFHopMusicDirector::Rescan()
{
	Scan = FolderOverride.IsEmpty() ? FGFHopMusicLibrary::ScanFirstUsable(FGFHopMusicLibrary::CandidateFolders()) : FGFHopMusicLibrary::ScanFolder(FolderOverride);
	bScanned = true;
	for (const FString& Note : Scan.Skipped)
	{
		UE_LOG(LogGF, Log, TEXT("Acceleration Hop music: skipped %s"), *Note);
	}
	if (FGFHopPlaylistRules::Sync(Prefs(), Scan.Keys()))
	{
		SavePrefs();
	}
	return Scan;
}

TArray<FString> FGFHopMusicDirector::ActiveKeys()
{
	if (!bScanned)
	{
		Rescan();
	}
	return FGFHopPlaylistRules::ActiveKeys(Prefs(), Scan.Keys());
}

FGFHopChoice FGFHopMusicDirector::ChooseOnEnter()
{
	Rescan();
	const TArray<FString> Active = FGFHopPlaylistRules::ActiveKeys(Prefs(), Scan.Keys());
	const FGFHopTrack* Last = Scan.Find(Prefs().LastKey);
	return FGFHopPlaylistRules::ChooseOnEnter(Prefs(), Active, Last ? Last->DurationSec : 0.f, Bag, Rng);
}

FGFHopChoice FGFHopMusicDirector::ChooseAfterEnd(const FString& Finished)
{
	return FGFHopPlaylistRules::ChooseAfterEnd(Prefs(), ActiveKeys(), Finished, Bag, Rng);
}

void FGFHopMusicDirector::NoteStarted(const FString& Key, float StartSec)
{
	FGFHopPlaylistRules::NoteStopped(Prefs(), Key, StartSec);
}

void FGFHopMusicDirector::NoteStopped(const FString& Key, float PositionSec)
{
	FGFHopPlaylistRules::NoteStopped(Prefs(), Key, PositionSec);
	SavePrefs();
}
