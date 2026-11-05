// Acceleration Hop local music and Beat Hop playlist (issue 343) tests. Every track here is a click track written by
// FGFHopMusicLibrary::MakeTestWav into the automation transient folder: no music file is ever read from or added to
// the repo.
// Run: UnrealEditor-Cmd GFGame.uproject -ExecCmds="Automation RunTests GFGame.HopMusic;Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "GFAccelHopRoad.h"
#include "GFAccelHopTile.h"
#include "GFBeatmap.h"
#include "GFBeatClockSubsystem.h"
#include "GFHopMusic.h"
#include "GFRhythmSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GFHopMusicTestsPrivate
{
	static constexpr EAutomationTestFlags HopMusicFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** A fresh empty folder under the automation transient dir, deleted again when the test ends. */
	struct FHopMusicTempFolder
	{
		FString Path;

		explicit FHopMusicTempFolder(const TCHAR* Name)
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("HopMusic"), FString::Printf(TEXT("%s_%s"), Name, *FGuid::NewGuid().ToString())));
			IFileManager::Get().MakeDirectory(*Path, true);
		}

		~FHopMusicTempFolder()
		{
			IFileManager::Get().DeleteDirectory(*Path, false, true);
		}

		bool AddWav(const FString& FileName, float Seconds, float Bpm, int32 Rate = 8000) const
		{
			return FFileHelper::SaveArrayToFile(FGFHopMusicLibrary::MakeTestWav(Rate, Seconds, Bpm), *FPaths::Combine(Path, FileName));
		}

		bool AddText(const FString& FileName, const FString& Text) const
		{
			return FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Path, FileName));
		}
	};

	/** A throwaway game world for the road tests (no game instance, so the road keeps its own music director). */
	struct FHopMusicTestWorld
	{
		UWorld* World = nullptr;

		explicit FHopMusicTestWorld(const TCHAR* Name)
		{
			if (!GEngine)
			{
				return;
			}
			World = UWorld::CreateWorld(EWorldType::Game, false, Name);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}

		~FHopMusicTestWorld()
		{
			if (World && GEngine)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	static FGFHopPlaylistPrefs HopPrefsWith(const TArray<FString>& Keys, EGFHopPlayMode Mode = EGFHopPlayMode::InOrder, bool bResume = true)
	{
		FGFHopPlaylistPrefs Prefs;
		Prefs.Mode = Mode;
		Prefs.bResume = bResume;
		FGFHopPlaylistRules::Sync(Prefs, Keys);
		return Prefs;
	}

	/** A WAV header in any format, for the parser (the samples are whatever Body holds). */
	static TArray<uint8> HopWavBytes(uint16 Format, uint16 Channels, uint32 Rate, uint16 Bits, const TArray<uint8>& Body, bool bExtensible = false)
	{
		TArray<uint8> Out;
		auto Tag = [&Out](const char* Id) { Out.Append(reinterpret_cast<const uint8*>(Id), 4); };
		auto U32 = [&Out](uint32 V) { for (int32 I = 0; I < 4; ++I) { Out.Add(static_cast<uint8>((V >> (8 * I)) & 0xFF)); } };
		auto U16 = [&Out](uint16 V) { Out.Add(static_cast<uint8>(V & 0xFF)); Out.Add(static_cast<uint8>(V >> 8)); };
		const uint16 Align = static_cast<uint16>(Channels * Bits / 8);
		const uint32 FmtSize = bExtensible ? 40 : 16;
		Tag("RIFF");
		U32(4 + 8 + FmtSize + 8 + 6 + 8 + Body.Num());
		Tag("WAVE");
		Tag("fmt ");
		U32(FmtSize);
		U16(bExtensible ? 0xFFFE : Format);
		U16(Channels);
		U32(Rate);
		U32(Rate * Align);
		U16(Align);
		U16(Bits);
		if (bExtensible)
		{
			U16(22);
			U16(Bits);
			U32(0);
			U16(Format); // sub-format GUID: the first two bytes are the real format tag
			const uint8 Rest[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
			Out.Append(Rest, 14);
		}
		Tag("LIST"); // an odd sized chunk before the data, as real files often have
		U32(5);
		Out.Append(reinterpret_cast<const uint8*>("abcde"), 5);
		Out.Add(0);
		Tag("data");
		U32(Body.Num());
		Out.Append(Body);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicBpmFromNameTest, "GFGame.HopMusic.BpmFromName", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicBpmFromNameTest::RunTest(const FString&)
{
	struct FCase { const TCHAR* Name; bool bOk; float Bpm; const TCHAR* Title; };
	const FCase Cases[] = {
		{ TEXT("Night Drive 128bpm"), true, 128.f, TEXT("Night Drive") },
		{ TEXT("Night Drive - 128 BPM"), true, 128.f, TEXT("Night Drive") },
		{ TEXT("[128 BPM] Night Drive"), true, 128.f, TEXT("Night Drive") },
		{ TEXT("night_drive_bpm-128"), true, 128.f, TEXT("night drive") },
		{ TEXT("Track 92.5 BPM"), true, 92.5f, TEXT("Track") },
		{ TEXT("Slow 40bpm"), true, 40.f, TEXT("Slow") },
		{ TEXT("Fast 300bpm"), true, 300.f, TEXT("Fast") },
		{ TEXT("Too slow 39bpm"), false, 0.f, nullptr },
		{ TEXT("Too fast 301bpm"), false, 0.f, nullptr },
		{ TEXT("Night Drive"), false, 0.f, nullptr },
		{ TEXT("Song 2024"), false, 0.f, nullptr },
		{ TEXT("subpm128x"), false, 0.f, nullptr },
		{ TEXT("Mix 1280bpm"), false, 0.f, nullptr },
	};
	for (const FCase& Case : Cases)
	{
		float Bpm = 0.f;
		FString Title;
		const bool bOk = FGFHopMusicLibrary::ParseBpmFromName(Case.Name, Bpm, &Title);
		TestTrue(*FString::Printf(TEXT("%s parses as expected"), Case.Name), bOk == Case.bOk);
		if (Case.bOk && bOk)
		{
			TestEqual(*FString::Printf(TEXT("%s BPM"), Case.Name), Bpm, Case.Bpm, 0.01f);
			TestEqual(*FString::Printf(TEXT("%s title"), Case.Name), Title, FString(Case.Title));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicSidecarTest, "GFGame.HopMusic.Sidecar", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicSidecarTest::RunTest(const FString&)
{
	FGFHopTrack Track;
	Track.Title = TEXT("From the name");
	TestTrue(TEXT("Full sidecar"), FGFHopMusicLibrary::ApplySidecarJson(TEXT("{\"bpm\": 124.5, \"offset_ms\": 37, \"title\": \"Neon\"}"), Track));
	TestEqual(TEXT("BPM"), Track.Bpm, 124.5f, 0.01f);
	TestEqual(TEXT("Offset"), Track.OffsetMs, 37.f, 0.01f);
	TestEqual(TEXT("Title"), Track.Title, FString(TEXT("Neon")));

	FGFHopTrack Bare;
	Bare.Title = TEXT("Kept");
	TestTrue(TEXT("BPM only"), FGFHopMusicLibrary::ApplySidecarJson(TEXT("{\"bpm\": 90}"), Bare));
	TestEqual(TEXT("Title kept"), Bare.Title, FString(TEXT("Kept")));
	TestEqual(TEXT("No offset"), Bare.OffsetMs, 0.f);

	FGFHopTrack Untouched;
	Untouched.Bpm = 77.f;
	TestFalse(TEXT("No bpm"), FGFHopMusicLibrary::ApplySidecarJson(TEXT("{\"title\": \"x\"}"), Untouched));
	TestFalse(TEXT("Out of range"), FGFHopMusicLibrary::ApplySidecarJson(TEXT("{\"bpm\": 900}"), Untouched));
	TestFalse(TEXT("Not JSON"), FGFHopMusicLibrary::ApplySidecarJson(TEXT("bpm=120"), Untouched));
	TestEqual(TEXT("Failed sidecars leave the track alone"), Untouched.Bpm, 77.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicWavTest, "GFGame.HopMusic.Wav", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicWavTest::RunTest(const FString&)
{
	using namespace GFHopMusicTestsPrivate;
	// The click track the tools and tests use.
	const TArray<uint8> Click = FGFHopMusicLibrary::MakeTestWav(8000, 2.f, 120.f);
	FGFWavInfo Info;
	FString Error;
	TestTrue(TEXT("Click header"), FGFHopMusicLibrary::ReadWavInfo(Click, Info, Error));
	TestEqual(TEXT("Mono"), Info.Channels, 1);
	TestEqual(TEXT("Rate"), Info.SampleRate, 8000);
	TestEqual(TEXT("Duration"), Info.DurationSec(), 2.0, 1.0e-3);
	FGFWavPcm Pcm;
	TestTrue(TEXT("Click decodes"), FGFHopMusicLibrary::DecodeWav(Click, Pcm, Error));
	TestEqual(TEXT("Frames"), Pcm.NumFrames(), static_cast<int64>(16000));
	TestEqual(TEXT("Decoded duration"), Pcm.DurationSec(), 2.0, 1.0e-3);
	TestTrue(TEXT("Clicks are audible"), Pcm.Samples.ContainsByPredicate([](int16 S) { return S > 8000; }));

	// A header-only prefix still gives the format and length (the scan reads 512 KB at most).
	TArray<uint8> Prefix(Click.GetData(), 60);
	TestTrue(TEXT("Prefix header"), FGFHopMusicLibrary::ReadWavInfo(Prefix, Info, Error));
	TestEqual(TEXT("Prefix duration"), Info.DurationSec(), 2.0, 1.0e-3);

	// 24-bit stereo: full scale positive and negative.
	TArray<uint8> Body24 = { 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x80 };
	TestTrue(TEXT("24-bit decodes"), FGFHopMusicLibrary::DecodeWav(HopWavBytes(1, 2, 44100, 24, Body24), Pcm, Error));
	if (TestEqual(TEXT("24-bit samples"), Pcm.Samples.Num(), 2))
	{
		TestEqual(TEXT("24-bit max"), static_cast<int32>(Pcm.Samples[0]), 32767);
		TestEqual(TEXT("24-bit min"), static_cast<int32>(Pcm.Samples[1]), -32768);
	}

	// 32-bit float through WAVE_FORMAT_EXTENSIBLE, clipped to range.
	const float Floats[3] = { 0.5f, -2.f, 0.f };
	TArray<uint8> BodyF(reinterpret_cast<const uint8*>(Floats), sizeof(Floats));
	TestTrue(TEXT("Float decodes"), FGFHopMusicLibrary::DecodeWav(HopWavBytes(3, 1, 48000, 32, BodyF, true), Pcm, Error));
	if (TestEqual(TEXT("Float samples"), Pcm.Samples.Num(), 3))
	{
		TestTrue(TEXT("Half scale"), FMath::Abs(Pcm.Samples[0] - 16384) <= 2);
		TestEqual(TEXT("Clipped"), static_cast<int32>(Pcm.Samples[1]), -32767);
		TestEqual(TEXT("Silence"), static_cast<int32>(Pcm.Samples[2]), 0);
	}

	// 8-bit is unsigned.
	TArray<uint8> Body8 = { 128, 255, 0 };
	TestTrue(TEXT("8-bit decodes"), FGFHopMusicLibrary::DecodeWav(HopWavBytes(1, 1, 22050, 8, Body8), Pcm, Error));
	if (TestEqual(TEXT("8-bit samples"), Pcm.Samples.Num(), 3))
	{
		TestEqual(TEXT("8-bit centre"), static_cast<int32>(Pcm.Samples[0]), 0);
		TestTrue(TEXT("8-bit top"), Pcm.Samples[1] > 32000);
		TestTrue(TEXT("8-bit bottom"), Pcm.Samples[2] < -32000);
	}

	TestFalse(TEXT("Not a WAV"), FGFHopMusicLibrary::ReadWavInfo(TArray<uint8>({ 'I', 'D', '3', 4, 0, 0 }), Info, Error));
	TestFalse(TEXT("Six channels are refused"), FGFHopMusicLibrary::ReadWavInfo(HopWavBytes(1, 6, 48000, 16, TArray<uint8>()), Info, Error));
	TestFalse(TEXT("Compressed WAV is refused"), FGFHopMusicLibrary::ReadWavInfo(HopWavBytes(2, 1, 48000, 4, TArray<uint8>()), Info, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicScanTest, "GFGame.HopMusic.Scan", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicScanTest::RunTest(const FString&)
{
	using namespace GFHopMusicTestsPrivate;
	const FHopMusicTempFolder Folder(TEXT("Scan"));
	TestTrue(TEXT("Write B"), Folder.AddWav(TEXT("B Side 140bpm.wav"), 3.f, 140.f));
	TestTrue(TEXT("Write A"), Folder.AddWav(TEXT("A Side.wav"), 4.f, 100.f));
	TestTrue(TEXT("Sidecar for A"), Folder.AddText(TEXT("A Side.json"), TEXT("{\"bpm\": 100, \"offset_ms\": 12, \"title\": \"Alpha\"}")));
	TestTrue(TEXT("Sidecar wins over the name"), Folder.AddWav(TEXT("C 90bpm.wav"), 2.f, 120.f) && Folder.AddText(TEXT("C 90bpm.json"), TEXT("{\"bpm\": 120}")));
	TestTrue(TEXT("No BPM anywhere"), Folder.AddWav(TEXT("Mystery.wav"), 2.f, 120.f));
	TestTrue(TEXT("Not a wav"), Folder.AddText(TEXT("Song 120bpm.mp3"), TEXT("not really audio")));
	TestTrue(TEXT("Not audio at all"), Folder.AddText(TEXT("notes.txt"), TEXT("hello")));

	const FGFHopLibraryScan Scan = FGFHopMusicLibrary::ScanFolder(Folder.Path);
	TestEqual(TEXT("Three usable tracks"), Scan.Tracks.Num(), 3);
	const TArray<FString> Keys = Scan.Keys();
	TestTrue(TEXT("Sorted by file name"), Keys == TArray<FString>({ TEXT("A Side.wav"), TEXT("B Side 140bpm.wav"), TEXT("C 90bpm.wav") }));
	if (const FGFHopTrack* A = Scan.Find(TEXT("A Side.wav")))
	{
		TestEqual(TEXT("A BPM from sidecar"), A->Bpm, 100.f);
		TestEqual(TEXT("A offset"), A->OffsetMs, 12.f);
		TestEqual(TEXT("A title"), A->Title, FString(TEXT("Alpha")));
		TestEqual(TEXT("A duration"), A->DurationSec, 4.f, 0.01f);
		TestEqual(TEXT("A source"), A->BpmFrom, FString(TEXT("sidecar")));
	}
	if (const FGFHopTrack* B = Scan.Find(TEXT("B Side 140bpm.wav")))
	{
		TestEqual(TEXT("B BPM from name"), B->Bpm, 140.f);
		TestEqual(TEXT("B title"), B->Title, FString(TEXT("B Side")));
		TestEqual(TEXT("B source"), B->BpmFrom, FString(TEXT("name")));
	}
	if (const FGFHopTrack* C = Scan.Find(TEXT("C 90bpm.wav")))
	{
		TestEqual(TEXT("C sidecar beats name"), C->Bpm, 120.f);
	}
	TestEqual(TEXT("Mystery and the mp3 are skipped, notes.txt ignored"), Scan.Skipped.Num(), 2);

	// The fallback: an empty folder and a missing one both give no tracks.
	const FHopMusicTempFolder Empty(TEXT("Empty"));
	TestEqual(TEXT("Empty folder"), FGFHopMusicLibrary::ScanFolder(Empty.Path).Tracks.Num(), 0);
	TestEqual(TEXT("Missing folder"), FGFHopMusicLibrary::ScanFolder(FPaths::Combine(Empty.Path, TEXT("nope"))).Tracks.Num(), 0);
	const FGFHopLibraryScan First = FGFHopMusicLibrary::ScanFirstUsable({ Empty.Path, Folder.Path });
	TestEqual(TEXT("First folder with tracks wins"), First.Folder, Folder.Path);
	TestEqual(TEXT("Nothing anywhere"), FGFHopMusicLibrary::ScanFirstUsable({ Empty.Path }).Tracks.Num(), 0);
	TestTrue(TEXT("Candidates end in the music folder"), FGFHopMusicLibrary::CandidateFolders().Num() >= 3 && FGFHopMusicLibrary::CandidateFolders().Last().EndsWith(TEXT("LicensedMusic/AccelHop")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicShuffleTest, "GFGame.HopMusic.Shuffle", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicShuffleTest::RunTest(const FString&)
{
	const TArray<FString> Active = { TEXT("a.wav"), TEXT("b.wav"), TEXT("c.wav"), TEXT("d.wav") };
	for (int32 Seed = 1; Seed <= 25; ++Seed)
	{
		FRandomStream Rng(Seed);
		FGFHopShuffleBag Bag;
		FString Current = FGFHopPlaylistRules::PickNext(Active, EGFHopPlayMode::Shuffle, FString(), Bag, Rng);
		TSet<FString> FirstRound = { Current };
		for (int32 I = 0; I < 200; ++I)
		{
			const FString Next = FGFHopPlaylistRules::PickNext(Active, EGFHopPlayMode::Shuffle, Current, Bag, Rng);
			if (Next == Current || !Active.Contains(Next))
			{
				AddError(FString::Printf(TEXT("Seed %d pick %d: %s after %s"), Seed, I, *Next, *Current));
				return false;
			}
			if (I < Active.Num() - 1)
			{
				FirstRound.Add(Next);
			}
			Current = Next;
		}
		TestEqual(*FString::Printf(TEXT("Seed %d plays every track in the first round"), Seed), FirstRound.Num(), Active.Num());
	}

	// Two tracks alternate; one track is allowed to repeat (nothing else to play).
	FRandomStream Rng(7);
	FGFHopShuffleBag Bag;
	const TArray<FString> Two = { TEXT("a.wav"), TEXT("b.wav") };
	FString Current = TEXT("a.wav");
	for (int32 I = 0; I < 20; ++I)
	{
		const FString Next = FGFHopPlaylistRules::PickNext(Two, EGFHopPlayMode::Shuffle, Current, Bag, Rng);
		TestNotEqual(TEXT("Two tracks alternate"), Next, Current);
		Current = Next;
	}
	TestEqual(TEXT("One track repeats"), FGFHopPlaylistRules::PickNext({ TEXT("a.wav") }, EGFHopPlayMode::Shuffle, TEXT("a.wav"), Bag, Rng), FString(TEXT("a.wav")));

	// An excluded track leaves the bag at once.
	Bag.Remaining = { TEXT("gone.wav") };
	const FString AfterExclude = FGFHopPlaylistRules::PickNext(Two, EGFHopPlayMode::Shuffle, TEXT("a.wav"), Bag, Rng);
	TestEqual(TEXT("Excluded track never plays"), AfterExclude, FString(TEXT("b.wav")));

	// In Order and Repeat One.
	TestEqual(TEXT("In Order next"), FGFHopPlaylistRules::PickNext(Active, EGFHopPlayMode::InOrder, TEXT("b.wav"), Bag, Rng), FString(TEXT("c.wav")));
	TestEqual(TEXT("In Order wraps"), FGFHopPlaylistRules::PickNext(Active, EGFHopPlayMode::InOrder, TEXT("d.wav"), Bag, Rng), FString(TEXT("a.wav")));
	TestEqual(TEXT("In Order after a gone track starts over"), FGFHopPlaylistRules::PickNext(Active, EGFHopPlayMode::InOrder, TEXT("gone.wav"), Bag, Rng), FString(TEXT("a.wav")));
	TestEqual(TEXT("Repeat One"), FGFHopPlaylistRules::PickNext(Active, EGFHopPlayMode::RepeatOne, TEXT("c.wav"), Bag, Rng), FString(TEXT("c.wav")));
	TestEqual(TEXT("Repeat One of a gone track"), FGFHopPlaylistRules::PickNext(Active, EGFHopPlayMode::RepeatOne, TEXT("gone.wav"), Bag, Rng), FString(TEXT("a.wav")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicResumeTest, "GFGame.HopMusic.Resume", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicResumeTest::RunTest(const FString&)
{
	using namespace GFHopMusicTestsPrivate;
	const TArray<FString> Keys = { TEXT("a.wav"), TEXT("b.wav"), TEXT("c.wav") };
	FRandomStream Rng(3);
	FGFHopShuffleBag Bag;

	FGFHopPlaylistPrefs Prefs = HopPrefsWith(Keys);
	FGFHopChoice First = FGFHopPlaylistRules::ChooseOnEnter(Prefs, Keys, 0.f, Bag, Rng);
	TestEqual(TEXT("Nothing played yet: the first track"), First.Key, FString(TEXT("a.wav")));
	TestEqual(TEXT("From the start"), First.StartSec, 0.f);

	FGFHopPlaylistRules::NoteStopped(Prefs, TEXT("b.wav"), 42.5f);
	TestEqual(TEXT("Leaving saves the track"), Prefs.LastKey, FString(TEXT("b.wav")));
	TestEqual(TEXT("Leaving saves the position"), Prefs.ResumeSec, 42.5f);
	const FGFHopChoice Resumed = FGFHopPlaylistRules::ChooseOnEnter(Prefs, Keys, 180.f, Bag, Rng);
	TestEqual(TEXT("Resume: same track"), Resumed.Key, FString(TEXT("b.wav")));
	TestEqual(TEXT("Resume: saved position"), Resumed.StartSec, 42.5f);
	TestTrue(TEXT("Resume: flagged"), Resumed.bResumed);
	TestEqual(TEXT("Unknown length trusts the point"), FGFHopPlaylistRules::ChooseOnEnter(Prefs, Keys, 0.f, Bag, Rng).StartSec, 42.5f);

	FGFHopPlaylistRules::NoteStopped(Prefs, TEXT("b.wav"), 179.f);
	const FGFHopChoice Tail = FGFHopPlaylistRules::ChooseOnEnter(Prefs, Keys, 180.f, Bag, Rng);
	TestEqual(TEXT("Inside the last 2 s counts as finished: the next track"), Tail.Key, FString(TEXT("c.wav")));
	TestEqual(TEXT("Next track from the start"), Tail.StartSec, 0.f);
	TestFalse(TEXT("Not a resume"), Tail.bResumed);

	FGFHopPlaylistRules::NoteStopped(Prefs, TEXT("b.wav"), 42.5f);
	Prefs.bResume = false;
	const FGFHopChoice Off = FGFHopPlaylistRules::ChooseOnEnter(Prefs, Keys, 180.f, Bag, Rng);
	TestEqual(TEXT("Resume off: the next track"), Off.Key, FString(TEXT("c.wav")));
	TestEqual(TEXT("Resume off: from the start"), Off.StartSec, 0.f);

	Prefs.bResume = true;
	FGFHopPlaylistRules::SetIncluded(Prefs, TEXT("b.wav"), false);
	const TArray<FString> Active = FGFHopPlaylistRules::ActiveKeys(Prefs, Keys);
	const FGFHopChoice Excluded = FGFHopPlaylistRules::ChooseOnEnter(Prefs, Active, 180.f, Bag, Rng);
	TestFalse(TEXT("An excluded track is not resumed"), Excluded.bResumed);
	TestTrue(TEXT("Something else plays"), Excluded.Key == TEXT("a.wav") || Excluded.Key == TEXT("c.wav"));

	Prefs.Mode = EGFHopPlayMode::RepeatOne;
	FGFHopPlaylistRules::SetIncluded(Prefs, TEXT("b.wav"), true);
	TestEqual(TEXT("Repeat One after the end: same track"), FGFHopPlaylistRules::ChooseAfterEnd(Prefs, Keys, TEXT("b.wav"), Bag, Rng).Key, FString(TEXT("b.wav")));
	Prefs.Mode = EGFHopPlayMode::InOrder;
	TestEqual(TEXT("In Order after the end: next"), FGFHopPlaylistRules::ChooseAfterEnd(Prefs, Keys, TEXT("b.wav"), Bag, Rng).Key, FString(TEXT("c.wav")));
	TestEqual(TEXT("After the end starts at 0"), FGFHopPlaylistRules::ChooseAfterEnd(Prefs, Keys, TEXT("b.wav"), Bag, Rng).StartSec, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicPlaylistEditTest, "GFGame.HopMusic.PlaylistEdit", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicPlaylistEditTest::RunTest(const FString&)
{
	using namespace GFHopMusicTestsPrivate;
	FRandomStream Rng(5);
	FGFHopShuffleBag Bag;

	// Empty playlist and everything excluded both mean the committed song.
	FGFHopPlaylistPrefs None;
	TestFalse(TEXT("No tracks: fallback"), FGFHopPlaylistRules::ChooseOnEnter(None, {}, 0.f, Bag, Rng).IsLocal());
	FGFHopPlaylistPrefs AllOff = HopPrefsWith({ TEXT("a.wav"), TEXT("b.wav") });
	FGFHopPlaylistRules::SetIncluded(AllOff, TEXT("a.wav"), false);
	FGFHopPlaylistRules::SetIncluded(AllOff, TEXT("b.wav"), false);
	const TArray<FString> NoneActive = FGFHopPlaylistRules::ActiveKeys(AllOff, { TEXT("a.wav"), TEXT("b.wav") });
	TestEqual(TEXT("All excluded: nothing active"), NoneActive.Num(), 0);
	TestFalse(TEXT("All excluded: fallback"), FGFHopPlaylistRules::ChooseOnEnter(AllOff, NoneActive, 0.f, Bag, Rng).IsLocal());

	// New files join at the end, included; rows for missing files stay put for when they come back.
	FGFHopPlaylistPrefs Prefs = HopPrefsWith({ TEXT("a.wav"), TEXT("b.wav"), TEXT("c.wav") });
	TestFalse(TEXT("Sync is idempotent"), FGFHopPlaylistRules::Sync(Prefs, { TEXT("a.wav"), TEXT("b.wav"), TEXT("c.wav") }));
	TestTrue(TEXT("A new file joins"), FGFHopPlaylistRules::Sync(Prefs, { TEXT("a.wav"), TEXT("c.wav"), TEXT("d.wav") }));
	TestEqual(TEXT("Four rows (b kept while missing)"), Prefs.Entries.Num(), 4);
	TestTrue(TEXT("Missing b is not active"), FGFHopPlaylistRules::ActiveKeys(Prefs, { TEXT("a.wav"), TEXT("c.wav"), TEXT("d.wav") }) == TArray<FString>({ TEXT("a.wav"), TEXT("c.wav"), TEXT("d.wav") }));

	// Reorder among the found tracks only.
	const TArray<FString> Found = { TEXT("a.wav"), TEXT("c.wav"), TEXT("d.wav") };
	TestTrue(TEXT("d moves up"), FGFHopPlaylistRules::MoveAmongFound(Prefs, Found, TEXT("d.wav"), -1));
	TestTrue(TEXT("Order after moving d up"), FGFHopPlaylistRules::OrderedFound(Prefs, Found) == TArray<FString>({ TEXT("a.wav"), TEXT("d.wav"), TEXT("c.wav") }));
	TestTrue(TEXT("a moves down"), FGFHopPlaylistRules::MoveAmongFound(Prefs, Found, TEXT("a.wav"), 1));
	TestTrue(TEXT("Order after moving a down"), FGFHopPlaylistRules::OrderedFound(Prefs, Found) == TArray<FString>({ TEXT("d.wav"), TEXT("a.wav"), TEXT("c.wav") }));
	TestFalse(TEXT("The top cannot move up"), FGFHopPlaylistRules::MoveAmongFound(Prefs, Found, TEXT("d.wav"), -1));
	TestFalse(TEXT("The bottom cannot move down"), FGFHopPlaylistRules::MoveAmongFound(Prefs, Found, TEXT("c.wav"), 1));
	TestTrue(TEXT("In Order follows the playlist"), FGFHopPlaylistRules::PickNext(FGFHopPlaylistRules::ActiveKeys(Prefs, Found), EGFHopPlayMode::InOrder, TEXT("d.wav"), Bag, Rng) == TEXT("a.wav"));
	TestTrue(TEXT("b still has its row"), Prefs.Entries.ContainsByPredicate([](const FGFHopPlaylistEntry& E) { return E.Key == TEXT("b.wav"); }));

	FGFHopPlaylistRules::SetIncluded(Prefs, TEXT("a.wav"), false);
	TestTrue(TEXT("Excluded a is skipped"), FGFHopPlaylistRules::ActiveKeys(Prefs, Found) == TArray<FString>({ TEXT("d.wav"), TEXT("c.wav") }));
	TestEqual(TEXT("Excluded a is still listed"), FGFHopPlaylistRules::OrderedFound(Prefs, Found).Num(), 3);

	// Defaults the PR promises.
	const FGFHopPlaylistPrefs Defaults;
	TestTrue(TEXT("Default mode In Order"), Defaults.Mode == EGFHopPlayMode::InOrder);
	TestTrue(TEXT("Resume on by default"), Defaults.bResume);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicDirectorTest, "GFGame.HopMusic.Director", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicDirectorTest::RunTest(const FString&)
{
	using namespace GFHopMusicTestsPrivate;
	const FHopMusicTempFolder Folder(TEXT("Director"));
	FGFHopPlaylistPrefs Prefs;
	FGFHopMusicDirector Director;
	Director.SetPrefsOverride(&Prefs);
	Director.SetFolderOverride(Folder.Path);
	Director.SetSeed(11);

	TestFalse(TEXT("Empty folder: fallback"), Director.ChooseOnEnter().IsLocal());
	TestEqual(TEXT("Empty folder: no rows"), Prefs.Entries.Num(), 0);

	Folder.AddWav(TEXT("One 100bpm.wav"), 6.f, 100.f);
	Folder.AddWav(TEXT("Two 150bpm.wav"), 6.f, 150.f);
	const FGFHopChoice First = Director.ChooseOnEnter();
	TestEqual(TEXT("Tracks appear without a restart"), First.Key, FString(TEXT("One 100bpm.wav")));
	TestEqual(TEXT("Rows synced"), Prefs.Entries.Num(), 2);
	Director.NoteStarted(First.Key, 0.f);
	Director.NoteStopped(First.Key, 2.5f);
	const FGFHopChoice Again = Director.ChooseOnEnter();
	TestTrue(TEXT("Resumes"), Again.bResumed && Again.Key == First.Key);
	TestEqual(TEXT("At the saved point"), Again.StartSec, 2.5f);
	Director.NoteStopped(First.Key, 5.5f);
	TestEqual(TEXT("Near the end moves on"), Director.ChooseOnEnter().Key, FString(TEXT("Two 150bpm.wav")));

	IFileManager::Get().Delete(*FPaths::Combine(Folder.Path, TEXT("One 100bpm.wav")));
	IFileManager::Get().Delete(*FPaths::Combine(Folder.Path, TEXT("Two 150bpm.wav")));
	TestFalse(TEXT("Files removed: back to the fallback"), Director.ChooseOnEnter().IsLocal());
	TestEqual(TEXT("Rows kept for when they return"), Prefs.Entries.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicRoadTest, "GFGame.HopMusic.Road", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicRoadTest::RunTest(const FString&)
{
	using namespace GFHopMusicTestsPrivate;
	FHopMusicTestWorld W(TEXT("GFHopMusicRoadWorld"));
	if (!TestNotNull(TEXT("World"), W.World))
	{
		return false;
	}
	AGFAccelHopRoad* Road = W.World->SpawnActor<AGFAccelHopRoad>(FVector::ZeroVector, FRotator::ZeroRotator);
	UGFBeatClockSubsystem* Beat = W.World->GetSubsystem<UGFBeatClockSubsystem>();
	if (!TestNotNull(TEXT("Road"), Road) || !TestNotNull(TEXT("Clock"), Beat))
	{
		return false;
	}
	const FHopMusicTempFolder Folder(TEXT("Road"));
	FGFHopPlaylistPrefs Prefs;
	FGFHopMusicDirector& Director = Road->GetMusicDirector();
	Director.SetPrefsOverride(&Prefs);
	Director.SetFolderOverride(Folder.Path);

	// Empty folder: the committed fallback song, exactly as before issue 343.
	Road->StartMusicNow();
	TestTrue(TEXT("Empty folder: the road owns the clock"), Road->OwnsClock());
	TestFalse(TEXT("Empty folder: no local track"), Road->IsPlayingLocalTrack());
	TestTrue(TEXT("Empty folder: the built-in song"), Beat->GetBeatSource() == EGFBeatSource::Song);
	TestEqual(TEXT("Empty folder: fallback tempo"), Beat->GetBPM(), 130.f, 0.01f);
	Road->StopMusicNow();
	TestFalse(TEXT("Stopped"), Beat->IsRunning());
	TestTrue(TEXT("The fallback leaves no resume point"), Prefs.LastKey.IsEmpty());

	// A local track: its own BPM drives the clock.
	TestTrue(TEXT("Write a click track"), Folder.AddWav(TEXT("Click 100bpm.wav"), 8.f, 100.f));
	Road->StartMusicNow();
	TestTrue(TEXT("Local track plays"), Road->IsPlayingLocalTrack());
	TestEqual(TEXT("Its key"), Road->GetLocalTrackKey(), FString(TEXT("Click 100bpm.wav")));
	TestEqual(TEXT("Clock on the track's BPM"), Beat->GetBPM(), 100.f, 0.01f);
	TestTrue(TEXT("From the start"), Road->GetLocalTrackPosition() < 0.5f);
	Road->StopMusicNow();
	TestEqual(TEXT("Leaving saves the track"), Prefs.LastKey, FString(TEXT("Click 100bpm.wav")));

	// Resume: the audio and the beat grid both pick up at the saved point.
	Prefs.ResumeSec = 3.f;
	Road->StartMusicNow();
	TestTrue(TEXT("Resumed track plays"), Road->IsPlayingLocalTrack());
	TestEqual(TEXT("Audio resumes at 3 s"), Road->GetLocalTrackPosition(), 3.f, 0.25f);
	TestEqual(TEXT("Beat grid resumes at 3 s"), Beat->GetSongTimeSec(), 3.0 - UGFRhythmSettings::GetMutable()->SongsOffsetMs / 1000.0, 0.25);
	Road->StopMusicNow();
	TestEqual(TEXT("Leaving again saves about 3 s"), Prefs.ResumeSec, 3.f, 0.25f);

	// Everything excluded: back to the committed song.
	FGFHopPlaylistRules::SetIncluded(Prefs, TEXT("Click 100bpm.wav"), false);
	Road->StartMusicNow();
	TestFalse(TEXT("All excluded: no local track"), Road->IsPlayingLocalTrack());
	TestEqual(TEXT("All excluded: fallback tempo"), Beat->GetBPM(), 130.f, 0.01f);
	Road->StopMusicNow();

	// A broken .wav falls back instead of going silent.
	FGFHopPlaylistRules::SetIncluded(Prefs, TEXT("Click 100bpm.wav"), true);
	TArray<uint8> Broken = FGFHopMusicLibrary::MakeTestWav(8000, 8.f, 100.f);
	Broken.SetNum(40);
	FFileHelper::SaveArrayToFile(Broken, *FPaths::Combine(Folder.Path, TEXT("Click 100bpm.wav")));
	Road->StartMusicNow();
	TestFalse(TEXT("Broken file: no local track"), Road->IsPlayingLocalTrack());
	TestTrue(TEXT("Broken file: the built-in song"), Beat->GetBeatSource() == EGFBeatSource::Song && FMath::IsNearlyEqual(Beat->GetBPM(), 130.f, 0.01f));
	Road->StopMusicNow();
	Director.SetPrefsOverride(nullptr);
	Director.SetFolderOverride(FString());
	return true;
}

namespace GFHopMusicTestsPrivate
{
	/** What the client machine (a listen host here, so its clock is the server's) is playing while the server picks. */
	enum class ELitClientMusic : uint8
	{
		None,
		LocalTrack100,
		Clock175,
	};

	/** Everything the lit pick decided in one run, to compare across runs. */
	struct FLitPickRun
	{
		bool bOk = false;
		float PickBeatSeconds = 0.f;
		TArray<int64> Beats;
		TArray<int32> Indices;
		TArray<int32> Serials;
		bool bOffGridLandingPays = false;
		double ClientBpm = 0.0;
	};

	/**
	 * The same road, tiles and anchor steps as GFGame.AccelHop.Lit.Road, with the given music on this machine's
	 * clock. Records the pick beat counter and the pick at fixed server times.
	 */
	static FLitPickRun RunLitPicks(FAutomationTestBase& Test, ELitClientMusic Music, const TCHAR* WorldName)
	{
		FLitPickRun Run;
		FHopMusicTestWorld W(WorldName);
		if (!Test.TestNotNull(TEXT("World"), W.World))
		{
			return Run;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const EGFHopTileKind Kinds[] = { EGFHopTileKind::Standard, EGFHopTileKind::Boost, EGFHopTileKind::Standard, EGFHopTileKind::Downbeat, EGFHopTileKind::Standard, EGFHopTileKind::Landing };
		const float Xs[] = { 1400.f, 1200.f, 200.f, 1000.f, 600.f, 1800.f };
		for (int32 Index = 0; Index < 6; ++Index)
		{
			AGFAccelHopTile* Tile = W.World->SpawnActor<AGFAccelHopTile>(AGFAccelHopTile::StaticClass(), FTransform(FVector(Xs[Index], 0.0, 0.0)), Params);
			if (!Test.TestNotNull(TEXT("Tile"), Tile))
			{
				return Run;
			}
			Tile->SetKind(Kinds[Index]);
		}
		AGFAccelHopRoad* Road = W.World->SpawnActor<AGFAccelHopRoad>(AGFAccelHopRoad::StaticClass(), FTransform(FVector(1000.0, 0.0, 0.0)), Params);
		UGFBeatClockSubsystem* Beat = W.World->GetSubsystem<UGFBeatClockSubsystem>();
		if (!Test.TestNotNull(TEXT("Road"), Road) || !Test.TestNotNull(TEXT("Clock"), Beat))
		{
			return Run;
		}
		Road->SetAutoPick(false);
		Road->RefreshTiles();

		const FHopMusicTempFolder Folder(TEXT("LitPick"));
		FGFHopPlaylistPrefs Prefs;
		FGFHopMusicDirector& Director = Road->GetMusicDirector();
		Director.SetPrefsOverride(&Prefs);
		Director.SetFolderOverride(Folder.Path);
		if (Music == ELitClientMusic::LocalTrack100)
		{
			Test.TestTrue(TEXT("Write a 100 BPM click track"), Folder.AddWav(TEXT("Click 100bpm.wav"), 30.f, 100.f));
			Road->StartMusicNow();
			Test.TestTrue(TEXT("The local track plays"), Road->IsPlayingLocalTrack());
		}
		else if (Music == ELitClientMusic::Clock175)
		{
			FGFBeatmap Map;
			Map.SongId = FName(TEXT("LitPickTest175"));
			Map.Bpm = 175.f;
			Test.TestTrue(TEXT("A 175 BPM clock runs"), Beat->StartBeatmap(Map));
		}
		Run.ClientBpm = Beat->IsRunning() ? static_cast<double>(Beat->GetBPM()) : 0.0;

		Run.PickBeatSeconds = Road->GetLitPickBeatSeconds();
		const double Times[] = { 5.0, 5.3, 5.7, 6.2, 7.9, 12.34 };
		for (const double Now : Times)
		{
			Run.Beats.Add(Road->GetLitPickBeatIndex(Now));
		}

		// Anchor steps along the road, each at a server time off any beat grid.
		const float AnchorXs[] = { 200.f, 400.f, 700.f, 1000.f, 1300.f };
		double Now = 5.13;
		for (const float X : AnchorXs)
		{
			Road->ServerPickFrom(Road->AlongOf(FVector(X, 0.0, 0.0)), 1.f, Now);
			Run.Indices.Add(Road->GetLitIndex());
			Run.Serials.Add(Road->GetLitState().Serial);
			Now += 0.37;
		}

		// Land on the lit tile half a pick step off the server's grid: the server still calls it a lit landing.
		Road->ServerPickFrom(Road->AlongOf(FVector(200.0, 0.0, 0.0)), 1.f, Now);
		const AGFAccelHopTile* Lit = Road->GetLitTile();
		const double Step = static_cast<double>(Run.PickBeatSeconds);
		const double OffGrid = (FMath::FloorToDouble(Now / Step) + 0.5) * Step;
		Run.bOffGridLandingPays = Lit && Road->GetLitPickBeatIndex(OffGrid) == Road->GetLitPickBeatIndex(Now) && Road->ServerIsLitLanding(Lit, OffGrid);

		Road->StopMusicNow();
		Beat->StopClock();
		Director.SetPrefsOverride(nullptr);
		Director.SetFolderOverride(FString());
		Run.bOk = true;
		return Run;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFHopMusicLitPickIgnoresClientBpmTest, "GFGame.HopMusic.LitPickIgnoresClientBpm", GFHopMusicTestsPrivate::HopMusicFlags)
bool FGFHopMusicLitPickIgnoresClientBpmTest::RunTest(const FString&)
{
	using namespace GFHopMusicTestsPrivate;
	const FLitPickRun Quiet = RunLitPicks(*this, ELitClientMusic::None, TEXT("GFHopMusicLitPickQuiet"));
	const FLitPickRun Local = RunLitPicks(*this, ELitClientMusic::LocalTrack100, TEXT("GFHopMusicLitPickLocal"));
	const FLitPickRun Fast = RunLitPicks(*this, ELitClientMusic::Clock175, TEXT("GFHopMusicLitPickFast"));
	if (!Quiet.bOk || !Local.bOk || !Fast.bOk)
	{
		return false;
	}
	TestEqual(TEXT("No music: no client clock"), Quiet.ClientBpm, 0.0);
	TestEqual(TEXT("The local track drives this machine's clock at 100 BPM"), Local.ClientBpm, 100.0, 0.01);
	TestEqual(TEXT("The other clock runs at 175 BPM"), Fast.ClientBpm, 175.0, 0.01);

	// The pick step is the road song's committed beat (the fallback song's 130 BPM), whatever this machine plays.
	TestEqual(TEXT("Pick step: the committed 130 BPM"), Quiet.PickBeatSeconds, 60.f / 130.f, 0.001f);
	TestEqual(TEXT("Pick step ignores a 100 BPM local track"), Local.PickBeatSeconds, Quiet.PickBeatSeconds, 0.0001f);
	TestEqual(TEXT("Pick step ignores a 175 BPM clock"), Fast.PickBeatSeconds, Quiet.PickBeatSeconds, 0.0001f);
	bool bGrid = Quiet.Beats.Num() == 6;
	for (int32 Index = 0; bGrid && Index < Quiet.Beats.Num(); ++Index)
	{
		const double Times[] = { 5.0, 5.3, 5.7, 6.2, 7.9, 12.34 };
		bGrid &= Quiet.Beats[Index] == static_cast<int64>(FMath::FloorToDouble(Times[Index] / (60.0 / 130.0)));
	}
	TestTrue(TEXT("Pick beats are the server's own 130 BPM grid"), bGrid);
	TestTrue(TEXT("Same pick beats with a 100 BPM local track"), Local.Beats == Quiet.Beats);
	TestTrue(TEXT("Same pick beats with a 175 BPM clock"), Fast.Beats == Quiet.Beats);

	// The pick counts hops ahead of the anchor: the same tiles, in the same order, with the same serials.
	TestTrue(TEXT("Something was lit"), Quiet.Indices.ContainsByPredicate([](int32 I) { return I != INDEX_NONE; }));
	TestTrue(TEXT("Same lit tiles with a 100 BPM local track"), Local.Indices == Quiet.Indices);
	TestTrue(TEXT("Same lit tiles with a 175 BPM clock"), Fast.Indices == Quiet.Indices);
	TestTrue(TEXT("Same serials with a 100 BPM local track"), Local.Serials == Quiet.Serials);
	TestTrue(TEXT("Same serials with a 175 BPM clock"), Fast.Serials == Quiet.Serials);

	// Off the server's beat still pays the lit boost: the server checks the tile, not the beat.
	TestTrue(TEXT("Off-grid lit landing counts (no music)"), Quiet.bOffGridLandingPays);
	TestTrue(TEXT("Off-grid lit landing counts (100 BPM local track)"), Local.bOffGridLandingPays);
	TestTrue(TEXT("Off-grid lit landing counts (175 BPM clock)"), Fast.bOffGridLandingPays);
	return true;
}

#endif
