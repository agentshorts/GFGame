// Beat Hop live mode tests (PLAN.md M5): the beat tracker and live chart generator on synthetic audio, the shared
// clock and ring buffer, StartLive end to end on the real clocks (a fake capture source that stamps packets with the
// real performance counter, like WASAPI), device loss and recovery, and one real loopback open (skipped cleanly when
// the machine has no output device).
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "GFBeatTracker.h"
#include "GFLiveChartGenerator.h"
#include "GFWasapiLoopback.h"
#include "GFLiveAudio.h"
#include "GFBeatClockSubsystem.h"
#include "GFBeatHopCourse.h"
#include "GFBuiltInSongs.h"
#include "GFBeatmap.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS

namespace GFLiveTests
{
	static constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	static constexpr int32 SampleRate = 48000;

	/** Adds one drum hit (kick body + click) at sample I. */
	static void AddHit(TArray<float>& X, int32 I, FRandomStream& Rng, float Gain, bool bKick)
	{
		// 0.4 s tail with a 20 ms fade-out, so the synth never ends a hit with a click of its own.
		const int32 Full = SampleRate * 2 / 5;
		const int32 Fade = SampleRate / 50;
		const int32 L = FMath::Min(X.Num() - I, Full);
		for (int32 k = 0; k < L; ++k)
		{
			const double T = static_cast<double>(k) / SampleRate;
			const double Tail = k > Full - Fade ? static_cast<double>(Full - k) / Fade : 1.0;
			double V = Rng.FRandRange(-1.f, 1.f) * 0.35 * FMath::Exp(-T / 0.005);
			if (bKick)
			{
				V += FMath::Sin(2.0 * PI * 55.0 * T) * FMath::Exp(-T / 0.06) * 0.8;
			}
			X[I + k] += static_cast<float>(V * Gain * Tail);
		}
	}

	/** Beats at a steady tempo (optionally changing at ChangeAt), with quiet off-beat hats and a little noise. */
	static TArray<float> Synth(double Seconds, double Bpm, bool bHats, TArray<double>* OutBeats = nullptr, double ChangeAt = -1.0, double Bpm2 = 0.0, int32 Seed = 1)
	{
		FRandomStream Rng(Seed);
		TArray<float> X;
		X.SetNumZeroed(static_cast<int32>(Seconds * SampleRate));
		for (float& S : X)
		{
			S = Rng.FRandRange(-0.01f, 0.01f);
		}
		double T = 0.25;
		while (T < Seconds)
		{
			const double P = 60.0 / (ChangeAt > 0.0 && T >= ChangeAt ? Bpm2 : Bpm);
			AddHit(X, static_cast<int32>(T * SampleRate), Rng, 1.f, true);
			if (OutBeats)
			{
				OutBeats->Add(T);
			}
			if (bHats && T + P * 0.5 < Seconds)
			{
				AddHit(X, static_cast<int32>((T + P * 0.5) * SampleRate), Rng, 0.25f, false);
			}
			T += P;
		}
		return X;
	}

	/** Feeds the clip in 10 ms blocks; calls OnBlock(Now) after each. */
	template <typename F>
	static void Feed(FGFBeatTracker& Tracker, const TArray<float>& X, F&& OnBlock, double StartTime = 0.0)
	{
		const int32 Block = SampleRate / 100;
		for (int32 i = 0; i < X.Num(); i += Block)
		{
			const int32 N = FMath::Min(Block, X.Num() - i);
			Tracker.ProcessSamples(X.GetData() + i, N, i == 0 ? StartTime : -1.0);
			OnBlock(StartTime + static_cast<double>(i + N) / SampleRate);
		}
	}

	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GFLiveTestWorld"));
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			if (!World->HasBegunPlay() && World->GetWorldSettings())
			{
				World->GetWorldSettings()->NotifyBeginPlay();
			}
		}
		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** What the test controls across every fake source the clock creates. */
	struct FFakeAudio
	{
		TArray<float> Clip;
		int32 Rate = SampleRate;
		/** The device "opens" on another thread after this long (the sample-rate race). */
		float ReadyDelay = 0.15f;
		/** BeginCapture fails this many more times. */
		std::atomic<int32> FailStarts{0};
		/** The source with this index reports DeviceLost. */
		std::atomic<int32> LoseIndex{-1};
		std::atomic<int32> Made{0};
		/** Real-clock time the first source started playing the clip. */
		double FirstStart = -1.0;
	};

	/**
	 * Plays Clip in real time, like a loopback of the speakers. Each read is stamped the way WASAPI does it: the
	 * performance counter (100 ns units) at the first sample, converted through FGFAudioClock.
	 */
	class FFakeLiveSource : public IGFLiveAudioSource
	{
	public:
		explicit FFakeLiveSource(TSharedRef<FFakeAudio> InAudio) : Audio(InAudio), Index(InAudio->Made++)
		{
			Ready = FPlatformProcess::GetSynchEventFromPool(true);
		}
		virtual ~FFakeLiveSource() override
		{
			StopCapture();
			FPlatformProcess::ReturnSynchEventToPool(Ready);
		}
		virtual bool BeginCapture(const FString&) override
		{
			if (Audio->FailStarts.load() > 0)
			{
				--Audio->FailStarts;
				Status = static_cast<uint8>(EGFCaptureStatus::Failed);
				return false;
			}
			Status = static_cast<uint8>(EGFCaptureStatus::Starting);
			Ready->Reset();
			const float Delay = Audio->ReadyDelay;
			const int32 R = Audio->Rate;
			Worker = Async(EAsyncExecution::Thread, [this, Delay, R]()
			{
				FPlatformProcess::Sleep(Delay);
				Started = FGFAudioClock::Now();
				Rate = R;
				Status = static_cast<uint8>(EGFCaptureStatus::Capturing);
				Ready->Trigger();
			});
			return true;
		}
		virtual void StopCapture() override
		{
			if (Worker.IsValid())
			{
				Worker.Wait();
				Worker = TFuture<void>();
			}
			if (Status.load() == static_cast<uint8>(EGFCaptureStatus::Capturing))
			{
				Status = static_cast<uint8>(EGFCaptureStatus::Idle);
			}
		}
		virtual EGFCaptureStatus GetStatus() const override
		{
			const EGFCaptureStatus S = static_cast<EGFCaptureStatus>(Status.load());
			return S == EGFCaptureStatus::Capturing && Audio->LoseIndex.load() == Index ? EGFCaptureStatus::DeviceLost : S;
		}
		virtual int32 GetSampleRate() const override { return Rate.load(); }
		virtual int32 ReadSamples(TArray<float>& Out, double& OutFirst) override
		{
			if (GetStatus() != EGFCaptureStatus::Capturing || Audio->Clip.Num() == 0)
			{
				Out.Reset();
				return 0;
			}
			if (Audio->FirstStart < 0.0)
			{
				Audio->FirstStart = Started;
			}
			const double Now = FGFAudioClock::Now();
			const int64 Due = static_cast<int64>((Now - Started) * Rate);
			const int32 N = static_cast<int32>(FMath::Clamp<int64>(Due - Played, 0, Rate));
			Out.SetNumUninitialized(N);
			for (int32 i = 0; i < N; ++i)
			{
				Out[i] = Audio->Clip[(Played + i) % Audio->Clip.Num()];
			}
			const double TrueFirst = Started + static_cast<double>(Played) / Rate;
			const uint64 Qpc = FGFAudioClock::QpcNow100ns() - static_cast<uint64>(FMath::Max(0.0, Now - TrueFirst) * 1e7);
			OutFirst = FGFAudioClock::PacketStartTime(Qpc, true, FGFAudioClock::MeasureQpcOffset(), Now, static_cast<double>(N) / Rate);
			Played += N;
			return N;
		}

	protected:
		virtual bool WaitUntilReady(float TimeoutSeconds) override
		{
			if (Status.load() == static_cast<uint8>(EGFCaptureStatus::Starting))
			{
				Ready->Wait(FTimespan::FromSeconds(TimeoutSeconds));
			}
			return Status.load() == static_cast<uint8>(EGFCaptureStatus::Capturing);
		}

	private:
		TSharedRef<FFakeAudio> Audio;
		const int32 Index;
		FEvent* Ready = nullptr;
		TFuture<void> Worker;
		std::atomic<uint8> Status{static_cast<uint8>(EGFCaptureStatus::Idle)};
		std::atomic<int32> Rate{0};
		double Started = 0.0;
		int64 Played = 0;
	};

	static void UseFake(UGFBeatClockSubsystem* Clock, const TSharedRef<FFakeAudio>& Audio)
	{
		Clock->SetLiveSourceFactory([Audio]() -> TUniquePtr<IGFLiveAudioSource> { return MakeUnique<FFakeLiveSource>(Audio); });
	}

	/** Ticks the clock (and course) in real time until Pred or the timeout. */
	template <typename F>
	static bool RunFor(double Seconds, UGFBeatClockSubsystem* Clock, AGFBeatHopCourse* Course, F&& Pred)
	{
		const double End = FGFAudioClock::Now() + Seconds;
		double Last = FGFAudioClock::Now();
		while (FGFAudioClock::Now() < End)
		{
			FPlatformProcess::Sleep(0.01f);
			const double Now = FGFAudioClock::Now();
			Clock->Tick(static_cast<float>(Now - Last));
			if (Course)
			{
				Course->UpdateRun();
			}
			Last = Now;
			if (Pred())
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveBeatTrackTest, "GFGame.Live.BeatTrack", GFLiveTests::Flags)
bool FGFLiveBeatTrackTest::RunTest(const FString&)
{
	for (const double Bpm : { 80.0, 96.0, 110.0, 128.0, 140.0, 170.0 })
	{
		for (const bool bHats : { false, true })
		{
			TArray<double> Beats;
			const TArray<float> X = GFLiveTests::Synth(12.0, Bpm, bHats, &Beats, -1.0, 0.0, static_cast<int32>(Bpm));
			FGFBeatTracker Tracker(GFLiveTests::SampleRate);
			double LockedAt = -1.0;
			GFLiveTests::Feed(Tracker, X, [&](double Now)
			{
				if (LockedAt < 0.0 && Tracker.IsLocked())
				{
					LockedAt = Now;
				}
			});
			const FString What = FString::Printf(TEXT("%.0f BPM%s"), Bpm, bHats ? TEXT(" + hats") : TEXT(""));
			TestTrue(*(What + TEXT(": locks within 6 s")), LockedAt >= 0.0 && LockedAt <= 6.0);
			const double Got = Tracker.GetBPM();
			TestTrue(*FString::Printf(TEXT("%s: tempo within 2%% (got %.2f)"), *What, Got), FMath::Abs(Got - Bpm) <= Bpm * 0.02);
			TestFalse(*(What + TEXT(": no octave error")), FMath::Abs(Got - 2.0 * Bpm) < Bpm * 0.1 || FMath::Abs(Got - 0.5 * Bpm) < Bpm * 0.05);

			// Beat grid vs the real beats over the last 4 s.
			int32 Checked = 0, Close = 0;
			double SumErr = 0.0;
			for (const double B : Beats)
			{
				if (B < 8.0 || B > 11.5)
				{
					continue;
				}
				const double N = FMath::RoundHalfFromZero(Tracker.BeatNumberAt(B));
				const double Err = Tracker.TimeOfBeatNumber(N) - B;
				++Checked;
				SumErr += Err;
				Close += FMath::Abs(Err) <= 0.040 ? 1 : 0;
			}
			TestTrue(*FString::Printf(TEXT("%s: >= 90%% of beats within 40 ms (%d/%d, mean error %.1f ms)"), *What, Close, Checked, Checked ? 1000.0 * SumErr / Checked : 0.0), Checked > 0 && Close >= FMath::CeilToInt(Checked * 0.9));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveOnsetsTest, "GFGame.Live.Onsets", GFLiveTests::Flags)
bool FGFLiveOnsetsTest::RunTest(const FString&)
{
	// Irregular hits: onsets must land near each one and nowhere else.
	const TArray<double> Hits = { 0.50, 0.93, 1.21, 1.80, 2.05, 2.70, 3.33, 3.50, 4.20, 4.90 };
	FRandomStream Rng(3);
	TArray<float> X;
	X.SetNumZeroed(GFLiveTests::SampleRate * 6);
	for (const double H : Hits)
	{
		GFLiveTests::AddHit(X, static_cast<int32>(H * GFLiveTests::SampleRate), Rng, 1.f, true);
	}
	FGFBeatTracker Tracker(GFLiveTests::SampleRate);
	TArray<FGFOnset> Onsets;
	GFLiveTests::Feed(Tracker, X, [&](double) { Onsets.Append(Tracker.ConsumeOnsets()); }, 100.0);
	TestEqual(TEXT("One onset per hit"), Onsets.Num(), Hits.Num());
	for (const double H : Hits)
	{
		const FGFOnset* Near = Onsets.FindByPredicate([H](const FGFOnset& O) { return FMath::Abs(O.Time - (100.0 + H)) <= 0.015; });
		TestNotNull(*FString::Printf(TEXT("Onset within 15 ms of %.2f s (stream clock respected)"), H), Near);
		if (Near)
		{
			TestTrue(TEXT("Kick is low-band heavy"), Near->Low > Near->High);
		}
	}

	// The game's own sounds are ignored (loopback hears the game too).
	FGFBeatTracker Tracker2(GFLiveTests::SampleRate);
	for (const double H : Hits)
	{
		Tracker2.NoteGameSound(H + 0.005);
	}
	TArray<FGFOnset> Onsets2;
	GFLiveTests::Feed(Tracker2, X, [&](double) { Onsets2.Append(Tracker2.ConsumeOnsets()); });
	TestEqual(TEXT("Game sounds don't count as music"), Onsets2.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveSilenceTest, "GFGame.Live.Silence", GFLiveTests::Flags)
bool FGFLiveSilenceTest::RunTest(const FString&)
{
	TArray<float> Quiet;
	Quiet.SetNumZeroed(GFLiveTests::SampleRate * 5);
	FGFBeatTracker Tracker(GFLiveTests::SampleRate);
	FGFLiveChartGenerator Gen;
	int32 Bricks = 0, OnsetCount = 0;
	GFLiveTests::Feed(Tracker, Quiet, [&](double Now)
	{
		const TArray<FGFOnset> O = Tracker.ConsumeOnsets();
		OnsetCount += O.Num();
		Bricks += Gen.Update(Now, Tracker, O).Num();
	});
	TestTrue(TEXT("Silence is detected"), Tracker.IsSilent());
	TestFalse(TEXT("No lock on silence"), Tracker.IsLocked());
	TestEqual(TEXT("No onsets"), OnsetCount, 0);
	TestEqual(TEXT("No bricks without a beat"), Bricks, 0);

	// Music, then the music stops: the lock goes away and bricks stop coming.
	TArray<float> X = GFLiveTests::Synth(10.0, 120.0, false);
	X.AddZeroed(GFLiveTests::SampleRate * 6);
	FGFBeatTracker Tracker2(GFLiveTests::SampleRate);
	FGFLiveChartGenerator Gen2;
	bool bWasLocked = false;
	double LastBrickCreated = -1.0;
	GFLiveTests::Feed(Tracker2, X, [&](double Now)
	{
		bWasLocked |= Tracker2.IsLocked();
		for (const FGFLiveBrick& B : Gen2.Update(Now, Tracker2, Tracker2.ConsumeOnsets()))
		{
			LastBrickCreated = B.CreatedAt;
		}
	});
	TestTrue(TEXT("Locked while the music played"), bWasLocked);
	TestFalse(TEXT("Lock lost after the music stops"), Tracker2.IsLocked());
	TestTrue(TEXT("Bricks stop soon after the music"), LastBrickCreated > 0.0 && LastBrickCreated < 13.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveLookaheadTest, "GFGame.Live.LookaheadFreeze", GFLiveTests::Flags)
bool FGFLiveLookaheadTest::RunTest(const FString&)
{
	// 120 BPM, then a jump to 128 BPM at 12 s.
	const TArray<float> X = GFLiveTests::Synth(26.0, 120.0, true, nullptr, 12.0, 128.0);
	FGFBeatTracker Tracker(GFLiveTests::SampleRate);
	FGFLiveChartGenerator Gen;
	Gen.Difficulty = EGFBeatDifficulty::Easy; // on-beat only, so spacing = the beat period
	TMap<int32, double> FrozenTimes;
	int32 Created = 0, ShortLead = 0, Moved = 0;
	GFLiveTests::Feed(Tracker, X, [&](double Now)
	{
		for (const FGFLiveBrick& B : Gen.Update(Now, Tracker, Tracker.ConsumeOnsets()))
		{
			++Created;
			ShortLead += (B.Time - B.CreatedAt) < Gen.MinLeadSeconds - 1e-6 ? 1 : 0;
		}
		for (const FGFLiveBrick& B : Gen.GetBricks())
		{
			if (B.bFrozen)
			{
				if (const double* T = FrozenTimes.Find(B.Id))
				{
					Moved += FMath::IsNearlyEqual(*T, B.Time, 1e-9) ? 0 : 1;
				}
				else
				{
					FrozenTimes.Add(B.Id, B.Time);
					TestTrue(TEXT("Frozen only inside the freeze window"), B.Time - Now <= Gen.FreezeSeconds + 0.02);
				}
			}
		}
	});
	TestTrue(TEXT("Bricks were generated"), Created > 20);
	TestEqual(TEXT("Every brick appears at least 1.5 s ahead"), ShortLead, 0);
	TestEqual(TEXT("Frozen bricks never move"), Moved, 0);

	// After the jump the spacing follows the new tempo.
	TArray<double> Late;
	for (const FGFLiveBrick& B : Gen.GetBricks())
	{
		if (B.Time > 20.0)
		{
			Late.Add(B.Time);
		}
	}
	Late.Sort();
	if (TestTrue(TEXT("Bricks after the tempo jump"), Late.Num() >= 4))
	{
		const double Spacing = (Late.Last() - Late[0]) / (Late.Num() - 1);
		TestTrue(*FString::Printf(TEXT("Spacing follows 128 BPM (%.3f s)"), Spacing), FMath::Abs(Spacing - 60.0 / 128.0) <= 0.02 * 60.0 / 128.0);
	}
	TestTrue(TEXT("Tracker followed the jump"), FMath::Abs(Tracker.GetBPM() - 128.f) <= 128.f * 0.02f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveDevicesTest, "GFGame.Live.Devices", GFLiveTests::Flags)
bool FGFLiveDevicesTest::RunTest(const FString&)
{
	// "Listen to": system default first; output devices only (the list never offers a microphone).
	const TArray<FGFAudioOutputDevice> Options = FGFWasapiLoopback::EnumerateOutputDevices();
	if (TestTrue(TEXT("At least the default entry"), Options.Num() >= 1))
	{
		TestTrue(TEXT("First entry is the system default"), Options[0].Id.IsEmpty());
	}

	TArray<FGFAudioOutputDevice> Active;
	FGFAudioOutputDevice Speakers;
	Speakers.Id = TEXT("{test-speakers}");
	Speakers.FriendlyName = TEXT("Test Speakers");
	Active.Add(Speakers);

	bool bFellBack = true;
	TestTrue(TEXT("Empty saved id means the default"), FGFWasapiLoopback::ResolveListenDevice(FString(), Active, bFellBack).IsEmpty());
	TestFalse(TEXT("Default isn't a fallback"), bFellBack);
	TestEqual(TEXT("Saved active output is used"), FGFWasapiLoopback::ResolveListenDevice(Speakers.Id, Active, bFellBack), Speakers.Id);
	TestFalse(TEXT("No fallback when present"), bFellBack);
	TestTrue(TEXT("Unplugged device falls back to default"), FGFWasapiLoopback::ResolveListenDevice(TEXT("{gone}"), Active, bFellBack).IsEmpty());
	TestTrue(TEXT("Fallback is reported"), bFellBack);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveClockTest, "GFGame.Live.Clock", GFLiveTests::Flags)
bool FGFLiveClockTest::RunTest(const FString&)
{
	// A WASAPI-style stamp taken "now" must convert to FGFAudioClock::Now(), not to a time ~194 days away.
	const double Offset = FGFAudioClock::MeasureQpcOffset();
	const double Before = FGFAudioClock::Now();
	const uint64 Qpc = FGFAudioClock::QpcNow100ns();
	const double After = FGFAudioClock::Now();
	const double Converted = FGFAudioClock::FromQpc100ns(Qpc, Offset);
	TestTrue(*FString::Printf(TEXT("QPC stamp lands on the engine clock (%.3f ms off)"), 1000.0 * (Converted - Before)), Converted >= Before - 0.002 && Converted <= After + 0.002);
#if PLATFORM_WINDOWS
	TestTrue(*FString::Printf(TEXT("Offset is FPlatformTime's 16777216 s (%.3f)"), Offset), FMath::Abs(Offset - 16777216.0) < 0.01);
	TestTrue(TEXT("Raw QPC seconds are ~194 days off the engine clock (the bug this fixes)"), FMath::Abs(static_cast<double>(Qpc) * 1e-7 - Before) > 1.6e7);
#endif

	// Packet stamps: good ones are kept; missing, flagged, future or stale ones fall back to now - packet length.
	const double Now = FGFAudioClock::Now();
	const uint64 TenMsAgo = FGFAudioClock::QpcNow100ns() - 100000;
	const double Good = FGFAudioClock::PacketStartTime(TenMsAgo, true, Offset, Now, 0.01);
	TestTrue(TEXT("Good stamp kept"), FMath::Abs(Good - (Now - 0.01)) < 0.003);
	TestEqual(TEXT("Flagged stamp falls back"), FGFAudioClock::PacketStartTime(TenMsAgo, false, Offset, Now, 0.02), Now - 0.02);
	TestEqual(TEXT("Zero stamp falls back"), FGFAudioClock::PacketStartTime(0, true, Offset, Now, 0.02), Now - 0.02);
	TestEqual(TEXT("Raw (unconverted) stamp falls back"), FGFAudioClock::PacketStartTime(TenMsAgo, true, 0.0, Now, 0.02), Now - 0.02);
	TestEqual(TEXT("Future stamp falls back"), FGFAudioClock::PacketStartTime(TenMsAgo + 10000000, true, Offset, Now, 0.02), Now - 0.02);
	TestTrue(TEXT("Good stamp is usable"), FGFAudioClock::IsStampUsable(TenMsAgo, true, Offset, Now));
	TestFalse(TEXT("Timestamp-error flag makes a stamp unusable"), FGFAudioClock::IsStampUsable(TenMsAgo, false, Offset, Now));
	TestFalse(TEXT("Zero stamp is unusable"), FGFAudioClock::IsStampUsable(0, true, Offset, Now));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveRingTest, "GFGame.Live.Ring", GFLiveTests::Flags)
bool FGFLiveRingTest::RunTest(const FString&)
{
	FGFAudioRing Ring(8, 4); // 8 samples at 4 Hz: 2 s
	TArray<float> Out;
	double First = 0.0;
	const float A[] = { 1, 2, 3 };
	Ring.Write(A, 3, 100.0);
	TestEqual(TEXT("Read count"), Ring.Read(Out, First), 3);
	TestEqual(TEXT("Read start time"), First, 100.0);
	TestTrue(TEXT("Read data"), Out.Num() == 3 && Out[0] == 1.f && Out[2] == 3.f);
	TestEqual(TEXT("Empty after read"), Ring.Read(Out, First), 0);

	// Wrap around the end without loss.
	const float B[] = { 4, 5, 6, 7, 8, 9 };
	Ring.Write(B, 6, 200.0);
	Ring.Write(B, 2, 201.5);
	TestEqual(TEXT("Wrapped count"), Ring.Read(Out, First), 8);
	TestEqual(TEXT("Wrapped start keeps the first write's time"), First, 200.0);
	TestTrue(TEXT("Wrapped order"), Out[5] == 9.f && Out[6] == 4.f && Out[7] == 5.f);
	TestEqual(TEXT("Nothing dropped yet"), Ring.GetDropped(), int64(0));

	// Overflow: the oldest are overwritten and the start time moves on with them.
	const float C[] = { 10, 11, 12, 13, 14, 15 };
	Ring.Write(C, 6, 300.0);
	Ring.Write(C, 6, 301.5);
	TestEqual(TEXT("Full ring"), Ring.Read(Out, First), 8);
	TestEqual(TEXT("Start moved by the 4 dropped samples (1 s)"), First, 301.0);
	TestTrue(TEXT("Oldest dropped"), Out[0] == 14.f && Out[7] == 15.f);
	TestEqual(TEXT("Dropped count"), Ring.GetDropped(), int64(4));

	// A write bigger than the ring keeps only the newest samples.
	TArray<float> Big;
	for (int32 i = 0; i < 12; ++i) { Big.Add(static_cast<float>(i)); }
	Ring.Write(Big.GetData(), Big.Num(), 400.0);
	TestEqual(TEXT("Big write keeps capacity"), Ring.Read(Out, First), 8);
	TestEqual(TEXT("Big write start"), First, 401.0);
	TestTrue(TEXT("Big write newest"), Out[0] == 4.f && Out[7] == 11.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveRingGapsTest, "GFGame.Live.RingGaps", GFLiveTests::Flags)
bool FGFLiveRingGapsTest::RunTest(const FString&)
{
	// Every packet's own timestamp is honoured, and device-flagged discontinuities are never stitched on.
	FGFAudioRing Ring(100, 100); // 1 s at 100 Hz
	TArray<float> Out;
	double First = 0.0;
	TArray<float> Ten;
	Ten.Init(1.f, 10);
	const auto Near = [](double A, double B) { return FMath::Abs(A - B) < 1e-6; };

	Ring.Write(Ten.GetData(), 10, 10.0);
	Ring.Write(Ten.GetData(), 10, 10.1);
	TestEqual(TEXT("Contiguous packets read as one"), Ring.Read(Out, First), 20);
	TestTrue(TEXT("Contiguous start"), Near(First, 10.0));

	Ring.Write(Ten.GetData(), 10, 110.0);
	Ring.Write(Ten.GetData(), 10, 110.1015);
	TestEqual(TEXT("Jitter under the tolerance still continues"), Ring.Read(Out, First), 20);

	Ring.Write(Ten.GetData(), 10, 20.0);
	Ring.Write(Ten.GetData(), 10, 20.15);
	TestEqual(TEXT("A 50 ms gap is filled with 5 silent samples"), Ring.Read(Out, First), 25);
	TestTrue(TEXT("Filled start"), Near(First, 20.0));
	TestTrue(TEXT("Fill is silence, real samples after it"), Out.Num() == 25 && Out[9] == 1.f && Out[10] == 0.f && Out[14] == 0.f && Out[15] == 1.f);
	TestEqual(TEXT("Fill counted"), Ring.GetFilled(), int64(5));

	Ring.Write(Ten.GetData(), 10, 30.0);
	Ring.Write(Ten.GetData(), 10, 30.1, /*bDiscontinuity*/ true);
	TestEqual(TEXT("Flagged discontinuity is a new segment"), Ring.Read(Out, First), 10);
	TestTrue(TEXT("First segment start"), Near(First, 30.0));
	TestEqual(TEXT("Second segment"), Ring.Read(Out, First), 10);
	TestTrue(TEXT("Second segment keeps its own stamp"), Near(First, 30.1));
	TestEqual(TEXT("Then empty"), Ring.Read(Out, First), 0);

	Ring.Write(Ten.GetData(), 10, 40.0);
	Ring.Write(Ten.GetData(), 10, 41.0);
	Ring.Read(Out, First);
	TestEqual(TEXT("A 1 s jump is a new segment"), Ring.Read(Out, First), 10);
	TestTrue(TEXT("Jumped segment time"), Near(First, 41.0));

	Ring.Write(Ten.GetData(), 10, 50.0);
	Ring.Write(Ten.GetData(), 10, 49.9);
	Ring.Read(Out, First);
	TestEqual(TEXT("A backwards stamp is a new segment"), Ring.Read(Out, First), 10);
	TestTrue(TEXT("Backwards segment time"), Near(First, 49.9));
	TestEqual(TEXT("Segments counted"), Ring.GetBreaks(), 3);

	// Overflow across a segment boundary keeps each segment's time right.
	TArray<float> Sixty;
	Sixty.Init(2.f, 60);
	const int64 DroppedBefore = Ring.GetDropped();
	Ring.Write(Sixty.GetData(), 60, 60.0);
	Ring.Write(Sixty.GetData(), 60, 70.0);
	TestEqual(TEXT("Older segment lost its 20 oldest"), Ring.Read(Out, First), 40);
	TestTrue(TEXT("Older segment start moved on 0.2 s"), Near(First, 60.2));
	TestEqual(TEXT("Newer segment intact"), Ring.Read(Out, First), 60);
	TestTrue(TEXT("Newer segment start"), Near(First, 70.0));
	TestEqual(TEXT("Overflow dropped 20"), Ring.GetDropped() - DroppedBefore, int64(20));

	// Overflow that swallows a whole segment and part of the next.
	TArray<float> Thirty, Eighty;
	Thirty.Init(3.f, 30);
	Eighty.Init(4.f, 80);
	Ring.Write(Thirty.GetData(), 30, 80.0);
	Ring.Write(Thirty.GetData(), 30, 90.0);
	Ring.Write(Eighty.GetData(), 80, 100.0);
	TestEqual(TEXT("What's left of the middle segment"), Ring.Read(Out, First), 20);
	TestTrue(TEXT("Middle segment start moved on 0.1 s"), Near(First, 90.1));
	TestEqual(TEXT("Newest segment"), Ring.Read(Out, First), 80);
	TestTrue(TEXT("Newest segment start"), Near(First, 100.0));
	TestEqual(TEXT("Ring empty"), Ring.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveStartRealClockTest, "GFGame.Live.StartLiveRealClock", GFLiveTests::Flags)
bool FGFLiveStartRealClockTest::RunTest(const FString&)
{
	GFLiveTests::FTestWorld TW;
	UGFBeatClockSubsystem* Clock = TW.World ? TW.World->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
	AGFBeatHopCourse* Course = TW.World ? TW.World->SpawnActor<AGFBeatHopCourse>() : nullptr;
	if (!TestNotNull(TEXT("Beat clock"), Clock) || !TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	TArray<double> Beats;
	TSharedRef<GFLiveTests::FFakeAudio> Audio = MakeShared<GFLiveTests::FFakeAudio>();
	Audio->Clip = GFLiveTests::Synth(20.0, 120.0, true, &Beats, -1.0, 0.0, 7);
	GFLiveTests::UseFake(Clock, Audio);

	// Live run through the course, as the game starts it. The device opens 150 ms later on another thread.
	FString Why;
	TestTrue(TEXT("Solo live run starts"), Course->StartRunForPlayers(NAME_None, EGFBeatDifficulty::Normal, EGFBeatHopMode::Live, false, 1, Why));
	TestTrue(TEXT("Clock is live"), Clock->GetBeatSource() == EGFBeatSource::Live && Clock->GetLiveStatus() == EGFLiveStatus::Listening);
	TestEqual(TEXT("Tracker uses the device's real sample rate (waited for it)"), Clock->GetTracker().GetSampleRate(), GFLiveTests::SampleRate);

	GFLiveTests::RunFor(8.0, Clock, Course, []() { return false; });

	const double Now = FGFAudioClock::Now();
	const FGFBeatTracker& T = Clock->GetTracker();
	TestTrue(TEXT("Locked on the real clock"), T.IsLocked());
	TestTrue(*FString::Printf(TEXT("120 BPM (got %.2f)"), T.GetBPM()), FMath::Abs(T.GetBPM() - 120.f) <= 2.4f);
	TestTrue(*FString::Printf(TEXT("Analysis is at 'now' on the engine clock (%.3f s behind)"), Now - T.GetAnalysedUntil()), FMath::Abs(Now - T.GetAnalysedUntil()) < 0.25);
	TestTrue(TEXT("Corrected now is on the same clock"), FMath::Abs(Clock->GetCorrectedNow() - Now) < 1.0);
	TestTrue(TEXT("Current beat is a real beat count"), Clock->GetCurrentBeat() >= 0.0 && Clock->GetCurrentBeat() < 100.0);

	// The tracker's grid against when the beats really played (real clock), over the last few seconds.
	int32 Checked = 0, Close = 0;
	double SumErr = 0.0;
	for (const double B : Beats)
	{
		const double At = Audio->FirstStart + B;
		if (At < Now - 3.5 || At > Now - 0.5)
		{
			continue;
		}
		const double N = FMath::RoundHalfFromZero(T.BeatNumberAt(At));
		const double Err = T.TimeOfBeatNumber(N) - At;
		++Checked;
		SumErr += Err;
		Close += FMath::Abs(Err) <= 0.040 ? 1 : 0;
	}
	TestTrue(*FString::Printf(TEXT("Beats line up with real time (%d/%d within 40 ms, mean %.1f ms)"), Close, Checked, Checked ? 1000.0 * SumErr / Checked : 0.0), Checked >= 4 && Close >= FMath::CeilToInt(Checked * 0.9));

	// Bricks are timed on the run clock, just ahead of now (not ~194 days away).
	const double RunNow = Course->GetRunTime();
	TestTrue(TEXT("Run time is the corrected engine clock"), FMath::Abs(RunNow - Clock->GetCorrectedNow()) < 0.05);
	TestTrue(TEXT("Live bricks were made"), Course->NumBricks() > 0);
	int32 Sane = 0;
	for (int32 i = Course->GetFirstBrickIndex(); i < Course->NumBricks(); ++i)
	{
		const double BT = Course->GetBrick(i).Time;
		Sane += (BT > RunNow - 8.0 && BT < RunNow + 6.0) ? 1 : 0;
	}
	TestEqual(TEXT("Every brick is near the run clock"), Sane, Course->NumKeptBricks());

	// Old bricks are pruned (the run is endless); indices stay stable and pruned bricks can't be landed on.
	TestTrue(*FString::Printf(TEXT("Old bricks pruned (%d of %d)"), Course->GetFirstBrickIndex(), Course->NumBricks()), Course->GetFirstBrickIndex() > 0);
	TestEqual(TEXT("Kept + pruned = made"), Course->GetFirstBrickIndex() + Course->NumKeptBricks(), Course->NumBricks());
	double Oldest = TNumericLimits<double>::Max();
	for (int32 i = Course->GetFirstBrickIndex(); i < Course->NumBricks(); ++i)
	{
		Oldest = FMath::Min(Oldest, Course->GetBrick(i).Time);
	}
	TestTrue(*FString::Printf(TEXT("Nothing kept far behind the player (oldest %.2f s ago)"), RunNow - Oldest), Course->NumKeptBricks() == 0 || RunNow - Oldest < Course->LiveBrickKeepSeconds + 1.0);
	TestFalse(TEXT("Pruned brick is gone"), Course->IsValidBrick(0));
	TestTrue(TEXT("Landing on a pruned brick does nothing"), Course->HandleLanding(TEXT("Late"), 0, RunNow) == EGFJudgement::None);
	TestTrue(TEXT("No landing without a player key"), Course->HandleLanding(FString(), Course->GetFirstBrickIndex(), RunNow) == EGFJudgement::None);
	if (Course->NumKeptBricks() > 0)
	{
		Course->HandleLanding(TEXT("Late"), Course->GetFirstBrickIndex(), RunNow);
	}

	// A second player joins mid-run: live mode is solo, so the run ends (and the clock stops).
	Course->SetDebugPlayerCount(2);
	Course->UpdateRun();
	TestFalse(TEXT("Run ended when a second player joined"), Course->IsRunning());
	TestTrue(*FString::Printf(TEXT("Reason says why (%s)"), *Course->GetEndReason()), Course->GetEndReason().Contains(TEXT("solo")));
	TestTrue(TEXT("Live clock stopped"), Clock->GetBeatSource() == EGFBeatSource::None);
	const FGFRhythmScore* Late = Course->GetScore(TEXT("Late"));
	TestTrue(TEXT("A late player's score counts the pruned bricks as misses"), Late && Late->Judged() == Course->NumBricks() && Late->Misses >= Course->GetFirstBrickIndex());
	TestFalse(TEXT("Co-op live can't restart"), Course->StartRunForPlayers(NAME_None, EGFBeatDifficulty::Normal, EGFBeatHopMode::Live, false, 2, Why));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveDeviceLossTest, "GFGame.Live.DeviceLoss", GFLiveTests::Flags)
bool FGFLiveDeviceLossTest::RunTest(const FString&)
{
	GFLiveTests::FTestWorld TW;
	UGFBeatClockSubsystem* Clock = TW.World ? TW.World->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
	AGFBeatHopCourse* Course = TW.World ? TW.World->SpawnActor<AGFBeatHopCourse>() : nullptr;
	if (!TestNotNull(TEXT("Beat clock"), Clock) || !TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	TSharedRef<GFLiveTests::FFakeAudio> Audio = MakeShared<GFLiveTests::FFakeAudio>();
	Audio->Clip = GFLiveTests::Synth(4.0, 100.0, false);
	Audio->ReadyDelay = 0.05f;
	GFLiveTests::UseFake(Clock, Audio);

	// The device never opens: StartLive says so (and doesn't hang), and with the fallback song off the course refuses
	// with a reason (GFGame.Live.FallbackSong covers the default: play the fallback song instead).
	Audio->FailStarts = 1;
	TestFalse(TEXT("StartLive reports a device that won't open"), Clock->StartLive());
	TestTrue(TEXT("Status is Failed"), Clock->GetLiveStatus() == EGFLiveStatus::Failed);
	Audio->FailStarts = 1;
	FString Why;
	Course->bFallbackWhenNoMusic = false;
	TestFalse(TEXT("Live run refused without a device"), Course->StartRunForPlayers(NAME_None, EGFBeatDifficulty::Normal, EGFBeatHopMode::Live, false, 1, Why));
	TestFalse(TEXT("Refusal has a reason"), Why.IsEmpty());
	Course->bFallbackWhenNoMusic = true;

	// The device is pulled mid-run: the clock reopens the default and keeps listening.
	TestTrue(TEXT("StartLive"), Clock->StartLive());
	GFLiveTests::RunFor(0.3, Clock, nullptr, []() { return false; });
	Audio->LoseIndex = Audio->Made.load() - 1;
	Clock->Tick(0.01f);
	TestTrue(TEXT("Recovering after the device went away"), Clock->GetLiveStatus() == EGFLiveStatus::Recovering);
	TestTrue(TEXT("Back to listening on a new device"), GFLiveTests::RunFor(4.0, Clock, nullptr, [Clock]() { return Clock->GetLiveStatus() == EGFLiveStatus::Listening; }));
	TestEqual(TEXT("One recovery"), Clock->GetLiveRecoveries(), 1);
	TestTrue(TEXT("Still the live clock"), Clock->GetBeatSource() == EGFBeatSource::Live);
	int32 Got = 0;
	GFLiveTests::RunFor(0.5, Clock, nullptr, [Clock, &Got]() { Got += Clock->GetTracker().GetAnalysedUntil() > 0.0 ? 1 : 0; return Got > 0; });
	TestTrue(TEXT("Audio flows again"), Got > 0);

	// Pulled again during a live run and nothing reopens: live mode gives up, and the run ends with a reason.
	TestTrue(TEXT("Solo live run starts"), Course->StartRunForPlayers(NAME_None, EGFBeatDifficulty::Normal, EGFBeatHopMode::Live, false, 1, Why));
	Clock->MaxLiveRecoveryAttempts = 2;
	Audio->FailStarts = 100;
	Audio->LoseIndex = Audio->Made.load() - 1;
	TestTrue(TEXT("Live mode gives up after its retries"), GFLiveTests::RunFor(5.0, Clock, Course, [Clock]() { return Clock->GetLiveStatus() == EGFLiveStatus::Failed; }));
	TestTrue(TEXT("Clock stopped"), Clock->GetBeatSource() == EGFBeatSource::None);
	Course->UpdateRun();
	TestFalse(TEXT("Run ended"), Course->IsRunning());
	TestTrue(*FString::Printf(TEXT("Reason says audio stopped (%s)"), *Course->GetEndReason()), Course->GetEndReason().Contains(TEXT("audio")));
	Audio->FailStarts = 0;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveFallbackSongTest, "GFGame.Live.FallbackSong", GFLiveTests::Flags)
bool FGFLiveFallbackSongTest::RunTest(const FString&)
{
	// The start-flow rule: no music source, or only silence, plays the fallback song; demos open on it.
	const FName Fallback = FGFBeatHopMusic::FallbackSongId();
	const TArray<FName> Ids = FGFBuiltInSongs::SongIds();
	if (!TestTrue(TEXT("Fallback is a built-in song"), Ids.Contains(Fallback)) || !TestTrue(TEXT("Other songs"), Ids.Num() > 1))
	{
		return false;
	}
	const FName Other = Ids[0];
	TestTrue(TEXT("No music source: fallback"), FGFBeatHopMusic::PickSong(EGFBeatHopMode::Live, NAME_None, false, false) == Fallback);
	TestTrue(TEXT("Silent music: fallback"), FGFBeatHopMusic::PickSong(EGFBeatHopMode::Live, NAME_None, true, true) == Fallback);
	TestTrue(TEXT("Music playing: stays live"), FGFBeatHopMusic::PickSong(EGFBeatHopMode::Live, NAME_None, true, false).IsNone());
	TestTrue(TEXT("Songs mode with nothing chosen: fallback"), FGFBeatHopMusic::PickSong(EGFBeatHopMode::Songs, NAME_None, false, false) == Fallback);
	TestTrue(TEXT("Songs mode keeps the chosen song"), FGFBeatHopMusic::PickSong(EGFBeatHopMode::Songs, Other, false, false) == Other);
	TestTrue(TEXT("Demos open on the fallback"), FGFBeatHopMusic::DefaultSong(true) == Fallback);
	TestTrue(TEXT("Not a demo: the first song"), FGFBeatHopMusic::DefaultSong(false) == Other);
	TestTrue(TEXT("Greybox songs have no audio"), FGFBeatHopMusic::SoundPathFor(Other).IsEmpty());

	// The song's audio is imported, and its beat map loads: the files in Content/Charts match the embedded charts.
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FGFBeatHopMusic::SoundPathFor(Fallback), nullptr, LOAD_NoWarn);
	if (TestNotNull(TEXT("Fallback SoundWave"), Sound))
	{
		TestTrue(*FString::Printf(TEXT("Fallback is 31.3 s (got %.2f)"), Sound->GetDuration()), FMath::Abs(Sound->GetDuration() - 31.29f) < 0.1f);
	}
	for (EGFBeatDifficulty D : { EGFBeatDifficulty::Easy, EGFBeatDifficulty::Normal, EGFBeatDifficulty::Hard })
	{
		const FString Name = FString::Printf(TEXT("BeatHopFallback_%s.json"), FGFBeatmapIO::DifficultyToString(D));
		FGFBeatmap FromFile;
		FString Error;
		if (!TestTrue(*(Name + TEXT(" loads ") + Error), FGFBeatmapIO::LoadFile(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Charts"), Name), FromFile, Error)))
		{
			continue;
		}
		for (const FString& P : FGFBeatmapIO::Validate(FromFile))
		{
			AddError(Name + TEXT(": ") + P);
		}
		TestTrue(*(Name + TEXT(" song id")), FromFile.SongId == Fallback);
		TestEqual(*(Name + TEXT(" BPM")), FromFile.Bpm, 130.f);
		TestEqual(*(Name + TEXT(" offset")), FromFile.OffsetMs, 15.f);
		TestTrue(*(Name + TEXT(" has notes")), FromFile.CountJudgedNotes() >= 16);
		const FGFBeatmap* Embedded = FGFBuiltInSongs::Find(Fallback, D);
		if (TestNotNull(*(Name + TEXT(" is embedded")), Embedded))
		{
			TestEqual(*(Name + TEXT(" same notes as embedded")), Embedded->Notes.Num(), FromFile.Notes.Num());
			bool bSame = Embedded->Bpm == FromFile.Bpm && Embedded->OffsetMs == FromFile.OffsetMs && Embedded->Lanes == FromFile.Lanes;
			for (int32 i = 0; bSame && i < FMath::Min(Embedded->Notes.Num(), FromFile.Notes.Num()); ++i)
			{
				bSame = Embedded->Notes[i].Beat == FromFile.Notes[i].Beat && Embedded->Notes[i].Lane == FromFile.Notes[i].Lane && Embedded->Notes[i].Type == FromFile.Notes[i].Type;
			}
			TestTrue(*(Name + TEXT(" matches the embedded chart")), bSame);
		}
		if (Sound && FromFile.Notes.Num() > 0)
		{
			TestTrue(*(Name + TEXT(" ends inside the song")), FromFile.BeatToSeconds(FromFile.Notes.Last().Beat) < Sound->GetDuration());
		}
	}

	GFLiveTests::FTestWorld TW;
	UGFBeatClockSubsystem* Clock = TW.World ? TW.World->GetSubsystem<UGFBeatClockSubsystem>() : nullptr;
	AGFBeatHopCourse* Course = TW.World ? TW.World->SpawnActor<AGFBeatHopCourse>() : nullptr;
	if (!TestNotNull(TEXT("Beat clock"), Clock) || !TestNotNull(TEXT("Course"), Course))
	{
		return false;
	}
	TSharedRef<GFLiveTests::FFakeAudio> Audio = MakeShared<GFLiveTests::FFakeAudio>();
	Audio->ReadyDelay = 0.05f;
	GFLiveTests::UseFake(Clock, Audio);

	// No music source (the output device won't open): the live start plays the fallback song instead of refusing.
	Audio->FailStarts = 1;
	FString Why;
	TestTrue(TEXT("Live start with no device still starts"), Course->StartRunForPlayers(NAME_None, EGFBeatDifficulty::Normal, EGFBeatHopMode::Live, false, 1, Why));
	TestTrue(TEXT("Running"), Course->IsRunning());
	TestTrue(TEXT("Plays the song, not live"), Course->GetRunMode() == EGFBeatHopMode::Songs);
	TestTrue(TEXT("The fallback song was picked"), Course->GetRunSongId() == Fallback);
	TestFalse(TEXT("Says why"), Course->GetFallbackReason().IsEmpty());
	TestEqual(TEXT("Its beat map is loaded"), Course->GetBeatmap().Bpm, 130.f);
	TestTrue(TEXT("Bricks from the beat map"), Course->NumBricks() > 0);
	TestTrue(TEXT("Clock isn't live"), Clock->GetBeatSource() != EGFBeatSource::Live);
	Course->SetDebugRunTime(1.0);
	Course->Tick(0.01f);
	TestTrue(TEXT("Song audio starts with the run"), Course->GetMusicSongId() == Fallback);
	Course->SetDebugRunTime(-1.0);
	Course->EndRun(TEXT("test"));
	Course->Tick(0.01f);
	TestTrue(TEXT("Song audio stops with the run"), Course->GetMusicSongId().IsNone());

	// The device opens but only silence plays: after LiveSilenceFallbackSeconds the live run becomes the fallback song.
	Audio->Clip.SetNumZeroed(GFLiveTests::SampleRate);
	Course->LiveSilenceFallbackSeconds = 2.f;
	TestTrue(TEXT("Live run starts"), Course->StartRunForPlayers(NAME_None, EGFBeatDifficulty::Normal, EGFBeatHopMode::Live, false, 1, Why));
	TestTrue(TEXT("Live while listening"), Course->GetRunMode() == EGFBeatHopMode::Live && Course->GetFallbackReason().IsEmpty());
	TestTrue(TEXT("Silence switches to the fallback song"), GFLiveTests::RunFor(6.0, Clock, Course, [Course]() { return Course->GetRunMode() == EGFBeatHopMode::Songs; }));
	TestTrue(TEXT("Still running"), Course->IsRunning());
	TestTrue(TEXT("Fallback song"), Course->GetRunSongId() == Fallback);
	TestFalse(*FString::Printf(TEXT("Says why (%s)"), *Course->GetFallbackReason()), Course->GetFallbackReason().IsEmpty());
	TestTrue(TEXT("Live clock stopped"), Clock->GetBeatSource() != EGFBeatSource::Live);
	Course->EndRun(TEXT("test"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGFLiveWasapiTest, "GFGame.Live.WasapiOpen", GFLiveTests::Flags)
bool FGFLiveWasapiTest::RunTest(const FString&)
{
	// The real loopback: StartCapture returns promptly either way, never hangs, and stamps on the engine clock.
	FGFWasapiLoopback Loopback;
	const double T0 = FGFAudioClock::Now();
	const bool bOpen = Loopback.StartCapture(FString(), 2.f);
	const double Took = FGFAudioClock::Now() - T0;
	TestTrue(*FString::Printf(TEXT("StartCapture returns within 3 s (%.2f s)"), Took), Took < 3.0);
	if (!bOpen)
	{
		TestTrue(TEXT("A failed open says Failed"), Loopback.GetStatus() == EGFCaptureStatus::Failed);
		AddInfo(TEXT("No output device to loop back on this machine; real-device checks skipped."));
		return true;
	}
	TestTrue(TEXT("Capturing"), Loopback.GetStatus() == EGFCaptureStatus::Capturing);
	TestTrue(*FString::Printf(TEXT("Sample rate known on return (%d)"), Loopback.GetSampleRate()), Loopback.GetSampleRate() >= 8000);
	FPlatformProcess::Sleep(0.3f);
	TArray<float> Samples;
	double First = 0.0;
	const int32 N = Loopback.ReadSamples(Samples, First);
	if (N > 0)
	{
		const double Age = FGFAudioClock::Now() - First;
		TestTrue(*FString::Printf(TEXT("Captured audio is stamped on the engine clock (%.3f s old)"), Age), Age >= -0.05 && Age < 5.0);
	}
	else
	{
		AddInfo(TEXT("Nothing is playing, so the loopback delivered no packets; stamp check skipped."));
	}
	Loopback.StopCapture();
	TestTrue(TEXT("Stopped"), Loopback.GetStatus() == EGFCaptureStatus::Idle);
	return true;
}

#endif
