#include "GFBeatHopMaps.h"
#include "GFBeatHopCourse.h"
#include "GFLog.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace GFBeatHopMapsPrivate
{
	static const TCHAR* FileNames[] = {
		TEXT("Data/BeatHop/Maps/WarmUpWobble.json"),
		TEXT("Data/BeatHop/Maps/WallRunWaltz.json"),
		TEXT("Data/BeatHop/Maps/RaptorRush.json"),
	};

	static bool ParseRoot(const FString& Json, TSharedPtr<FJsonObject>& Out, FString& OutError)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid())
		{
			OutError = TEXT("course JSON did not parse");
			return false;
		}
		return true;
	}

	static bool ReadDifficulty(const FJsonObject& Obj, const TCHAR* Field, EGFBeatDifficulty& Out)
	{
		FString Text;
		if (!Obj.TryGetStringField(Field, Text))
		{
			return false;
		}
		return FGFBeatmapIO::DifficultyFromString(Text, Out);
	}

	static bool ObstacleKind(const FString& Kind, EGFBeatHopObstacle& Out)
	{
		if (Kind == TEXT("dash")) { Out = EGFBeatHopObstacle::DashGate; return true; }
		if (Kind == TEXT("mantle") || Kind == TEXT("ledge")) { Out = EGFBeatHopObstacle::MantleLedge; return true; }
		if (Kind == TEXT("wall")) { Out = EGFBeatHopObstacle::WallRunPanel; return true; }
		if (Kind == TEXT("grapple")) { Out = EGFBeatHopObstacle::GrapplePost; return true; }
		if (Kind == TEXT("swivel")) { Out = EGFBeatHopObstacle::SwivelPole; return true; }
		if (Kind == TEXT("crumble")) { Out = EGFBeatHopObstacle::CrumbleBrick; return true; }
		if (Kind == TEXT("beatwall")) { Out = EGFBeatHopObstacle::BeatWall; return true; }
		return false;
	}

	static bool NoteKind(const FString& Kind, EGFBeatNoteType& Out)
	{
		if (Kind == TEXT("hop")) { Out = EGFBeatNoteType::Tap; return true; }
		if (Kind == TEXT("lane")) { Out = EGFBeatNoteType::Switch; return true; }
		if (Kind == TEXT("pad")) { Out = EGFBeatNoteType::Pad; return true; }
		if (Kind == TEXT("gap")) { Out = EGFBeatNoteType::Gap; return true; }
		return false;
	}

	static bool ParseFile(const FString& Path, FGFBeatHopMapDef& Out, FString& OutError)
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			OutError = FString::Printf(TEXT("missing course file %s"), *Path);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!ParseRoot(Json, Root, OutError))
		{
			return false;
		}
		FString Id;
		if (!Root->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s has no id"), *Path);
			return false;
		}
		Out = FGFBeatHopMapDef();
		Out.Id = FName(*Id);
		double Number = 0.0;
		if (Root->TryGetNumberField(TEXT("number"), Number)) { Out.Number = static_cast<int32>(Number); }
		Root->TryGetStringField(TEXT("title"), Out.Title);
		if (Out.Title.IsEmpty()) { Out.Title = Id; }
		if (!ReadDifficulty(*Root, TEXT("difficulty"), Out.DisplayDifficulty))
		{
			OutError = FString::Printf(TEXT("%s has no difficulty"), *Id);
			return false;
		}
		if (!ReadDifficulty(*Root, TEXT("judge_difficulty"), Out.JudgeDifficulty))
		{
			Out.JudgeDifficulty = Out.DisplayDifficulty;
		}
		FString Song;
		if (Root->TryGetStringField(TEXT("default_song"), Song)) { Out.DefaultSong = FName(*Song); }
		double Value = 0.0;
		if (Root->TryGetNumberField(TEXT("bpm_min"), Value)) { Out.BpmMin = static_cast<float>(Value); }
		if (Root->TryGetNumberField(TEXT("bpm_max"), Value)) { Out.BpmMax = static_cast<float>(Value); }
		if (Root->TryGetNumberField(TEXT("reference_bpm"), Value)) { Out.ReferenceBpm = static_cast<float>(Value); }
		if (Root->TryGetNumberField(TEXT("par_score"), Value)) { Out.ParScore = static_cast<int32>(Value); }
		if (Root->TryGetNumberField(TEXT("brick_width_scale"), Value)) { Out.BrickWidthScale = static_cast<float>(Value); }
		if (Root->TryGetNumberField(TEXT("perfect_window_ms"), Value)) { Out.PerfectWindowMs = static_cast<float>(Value); }
		if (Root->TryGetNumberField(TEXT("good_window_ms"), Value)) { Out.GoodWindowMs = static_cast<float>(Value); }
		if (Root->TryGetNumberField(TEXT("checkpoint_every_beats"), Value)) { Out.CheckpointEveryBeats = static_cast<int32>(Value); }
		bool bClimb = false;
		if (Root->TryGetBoolField(TEXT("mantle_climb"), bClimb)) { Out.bMantleClimb = bClimb; }
		if (Root->TryGetNumberField(TEXT("lanes"), Value)) { Out.Lanes = FMath::Max(1, static_cast<int32>(Value)); }
		if (Root->TryGetNumberField(TEXT("ref_speed_cms"), Value)) { Out.RefSpeedCms = static_cast<float>(Value); }
		const TArray<TSharedPtr<FJsonValue>>* Tint = nullptr;
		if (Root->TryGetArrayField(TEXT("tint"), Tint) && Tint && Tint->Num() >= 3)
		{
			Out.Tint = FLinearColor(
				static_cast<float>((*Tint)[0]->AsNumber()),
				static_cast<float>((*Tint)[1]->AsNumber()),
				static_cast<float>((*Tint)[2]->AsNumber()));
		}

		const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
		if (Root->TryGetArrayField(TEXT("sections"), Sections) && Sections)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Sections)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (!Entry.IsValid() || !Entry->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
				{
					continue;
				}
				FGFBeatHopMapSection Section;
				(*Obj)->TryGetStringField(TEXT("id"), Section.Id);
				(*Obj)->TryGetStringField(TEXT("teach"), Section.Teach);
				double Beat = 0.0;
				if ((*Obj)->TryGetNumberField(TEXT("start_beat"), Beat)) { Section.StartBeat = Beat; }
				Out.Sections.Add(Section);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Pieces = nullptr;
		if (!Root->TryGetArrayField(TEXT("pieces"), Pieces) || !Pieces || Pieces->Num() == 0)
		{
			OutError = FString::Printf(TEXT("%s has no pieces"), *Id);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Pieces)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				continue;
			}
			FGFBeatHopMapPiece Piece;
			if (!(*Obj)->TryGetStringField(TEXT("kind"), Piece.Kind))
			{
				continue;
			}
			double Beat = 0.0;
			double Lane = 0.0;
			double Height = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("beat"), Beat)) { Piece.Beat = Beat; }
			if ((*Obj)->TryGetNumberField(TEXT("lane"), Lane)) { Piece.Lane = static_cast<int32>(Lane); }
			if ((*Obj)->TryGetNumberField(TEXT("height_cm"), Height)) { Piece.HeightCm = static_cast<float>(Height); }
			EGFBeatNoteType Note = EGFBeatNoteType::Tap;
			EGFBeatHopObstacle Obstacle = EGFBeatHopObstacle::DashGate;
			if (!NoteKind(Piece.Kind, Note) && !ObstacleKind(Piece.Kind, Obstacle))
			{
				OutError = FString::Printf(TEXT("%s has unknown piece %s"), *Id, *Piece.Kind);
				return false;
			}
			Out.Pieces.Add(Piece);
		}
		if (Out.Pieces.Num() == 0)
		{
			OutError = FString::Printf(TEXT("%s pieces were empty"), *Id);
			return false;
		}
		return true;
	}

	static TArray<FGFBeatHopMapDef>& Cache()
	{
		static TArray<FGFBeatHopMapDef> Maps;
		static bool bLoaded = false;
		if (bLoaded)
		{
			return Maps;
		}
		bLoaded = true;
		for (const TCHAR* Name : FileNames)
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / Name);
			FGFBeatHopMapDef Map;
			FString Error;
			if (!ParseFile(Path, Map, Error))
			{
				UE_LOG(LogGF, Error, TEXT("Beat Hop maps: %s"), *Error);
				continue;
			}
			Maps.Add(MoveTemp(Map));
		}
		Maps.Sort([](const FGFBeatHopMapDef& A, const FGFBeatHopMapDef& B) { return A.Number < B.Number; });
		return Maps;
	}

	struct FBestBook
	{
		TMap<FString, int32> Scores;
		bool bLoaded = false;
	};

	static FString BestKey(FName MapId, const FString& PlayerKey)
	{
		return MapId.ToString() + TEXT("|") + PlayerKey;
	}

	static FString BestsPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BeatHop/LocalBests.json"));
	}

	static FBestBook& Bests()
	{
		static FBestBook Book;
		if (Book.bLoaded)
		{
			return Book;
		}
		Book.bLoaded = true;
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *BestsPath()))
		{
			return Book;
		}
		TSharedPtr<FJsonObject> Root;
		FString Error;
		if (!ParseRoot(Json, Root, Error))
		{
			return Book;
		}
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Root->TryGetArrayField(TEXT("bests"), Rows) || !Rows)
		{
			return Book;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Rows)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				continue;
			}
			FString MapId;
			FString Player;
			double Score = 0.0;
			(*Obj)->TryGetStringField(TEXT("map"), MapId);
			(*Obj)->TryGetStringField(TEXT("player"), Player);
			(*Obj)->TryGetNumberField(TEXT("score"), Score);
			if (!MapId.IsEmpty() && !Player.IsEmpty())
			{
				Book.Scores.Add(BestKey(FName(*MapId), Player), static_cast<int32>(Score));
			}
		}
		return Book;
	}

	static void SaveBests()
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const TPair<FString, int32>& Pair : Bests().Scores)
		{
			FString MapId;
			FString Player;
			if (!Pair.Key.Split(TEXT("|"), &MapId, &Player))
			{
				continue;
			}
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("map"), MapId);
			Obj->SetStringField(TEXT("player"), Player);
			Obj->SetNumberField(TEXT("score"), Pair.Value);
			Rows.Add(MakeShared<FJsonValueObject>(Obj));
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetArrayField(TEXT("bests"), Rows);
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		if (!Root.IsValid() || !FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			return;
		}
		const FString Path = BestsPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(Json, *Path);
	}

	static FLayoutHook& HookSlot()
	{
		static FLayoutHook Hook = nullptr;
		return Hook;
	}
}

FName GFBeatHopMaps::FreeHopId()
{
	static const FName Id(TEXT("FreeHop"));
	return Id;
}

const TArray<FGFBeatHopMapDef>& GFBeatHopMaps::All()
{
	return GFBeatHopMapsPrivate::Cache();
}

const FGFBeatHopMapDef* GFBeatHopMaps::Find(FName Id)
{
	if (Id.IsNone())
	{
		return nullptr;
	}
	for (const FGFBeatHopMapDef& Map : All())
	{
		if (Map.Id == Id)
		{
			return &Map;
		}
	}
	return nullptr;
}

const FGFBeatHopMapDef* GFBeatHopMaps::ByNumber(int32 Number)
{
	for (const FGFBeatHopMapDef& Map : All())
	{
		if (Map.Number == Number)
		{
			return &Map;
		}
	}
	return nullptr;
}

bool GFBeatHopMaps::IsHandShaped(FName Id)
{
	return Find(Id) != nullptr;
}

FString GFBeatHopMaps::VarietyKey(const FString& Kind)
{
	if (Kind == TEXT("gap"))
	{
		return FString();
	}
	if (Kind == TEXT("pad") || Kind == TEXT("hop"))
	{
		return TEXT("hop");
	}
	return Kind;
}

int32 GFBeatHopMaps::ObstacleCount(const FGFBeatHopMapDef& Map)
{
	int32 Count = 0;
	for (const FGFBeatHopMapPiece& Piece : Map.Pieces)
	{
		if (Piece.Kind != TEXT("gap"))
		{
			++Count;
		}
	}
	return Count;
}

int32 GFBeatHopMaps::MoveVariety(const FGFBeatHopMapDef& Map)
{
	TArray<FString> Keys;
	for (const FGFBeatHopMapPiece& Piece : Map.Pieces)
	{
		const FString Key = VarietyKey(Piece.Kind);
		if (!Key.IsEmpty() && !Keys.Contains(Key))
		{
			Keys.Add(Key);
		}
	}
	return Keys.Num();
}

float GFBeatHopMaps::AverageWindowMs(const FGFBeatHopMapDef& Map)
{
	return (Map.PerfectWindowMs + Map.GoodWindowMs) * 0.5f;
}

float GFBeatHopMaps::TimingDemand(const FGFBeatHopMapDef& Map)
{
	const float Window = AverageWindowMs(Map);
	return Window > 0.f ? 1000.f / Window : 0.f;
}

FString GFBeatHopMaps::Describe(const FGFBeatHopMapDef& Map)
{
	return FString::Printf(TEXT("%s obstacles=%d moves=%d window_ms=%.1f demand=%.2f"),
		*Map.Title, ObstacleCount(Map), MoveVariety(Map), AverageWindowMs(Map), TimingDemand(Map));
}

bool GFBeatHopMaps::Fits(const FGFBeatHopMapDef& Map, float Bpm, FString& OutMessage)
{
	OutMessage.Reset();
	const int32 Low = FMath::RoundToInt(Map.BpmMin);
	const int32 High = FMath::RoundToInt(Map.BpmMax);
	if (Bpm <= 0.f)
	{
		OutMessage = FString::Printf(TEXT("%s needs a steady tempo between %d and %d BPM."), *Map.Title, Low, High);
		return false;
	}
	if (Bpm + KINDA_SMALL_NUMBER < Map.BpmMin || Bpm - KINDA_SMALL_NUMBER > Map.BpmMax)
	{
		OutMessage = FString::Printf(TEXT("%s needs a tempo between %d and %d BPM. This one is %d."),
			*Map.Title, Low, High, FMath::RoundToInt(Bpm));
		return false;
	}
	return true;
}

bool GFBeatHopMaps::Build(const FGFBeatHopMapDef& Map, float Bpm, FGFBeatHopMapBuilt& Out, FString& OutError)
{
	Out = FGFBeatHopMapBuilt();
	if (Bpm <= 0.f)
	{
		OutError = TEXT("course BPM must be above zero");
		return false;
	}
	Out.Beatmap.SongId = Map.Id;
	Out.Beatmap.Title = Map.Title;
	Out.Beatmap.Difficulty = Map.DisplayDifficulty;
	Out.Beatmap.Bpm = Bpm;
	Out.Beatmap.Lanes = FMath::Max(1, Map.Lanes);
	Out.Beatmap.RefSpeedCms = Map.RefSpeedCms > 0.f ? Map.RefSpeedCms : 600.f;
	Out.Beatmap.OffsetMs = 0.f;

	TArray<int32> Order;
	Order.Reserve(Map.Pieces.Num());
	for (int32 i = 0; i < Map.Pieces.Num(); ++i)
	{
		Order.Add(i);
	}
	Order.Sort([&Map](int32 A, int32 B)
	{
		if (Map.Pieces[A].Beat != Map.Pieces[B].Beat)
		{
			return Map.Pieces[A].Beat < Map.Pieces[B].Beat;
		}
		return A < B;
	});

	for (int32 Index : Order)
	{
		const FGFBeatHopMapPiece& Piece = Map.Pieces[Index];
		const int32 Lane = FMath::Clamp(Piece.Lane, 0, Out.Beatmap.Lanes - 1);
		EGFBeatNoteType Note = EGFBeatNoteType::Tap;
		if (GFBeatHopMapsPrivate::NoteKind(Piece.Kind, Note))
		{
			FGFBeatNote Row;
			Row.Beat = Piece.Beat;
			Row.Lane = Lane;
			Row.Type = Note;
			Out.Beatmap.Notes.Add(Row);
			continue;
		}
		EGFBeatHopObstacle Kind = EGFBeatHopObstacle::DashGate;
		if (!GFBeatHopMapsPrivate::ObstacleKind(Piece.Kind, Kind))
		{
			OutError = FString::Printf(TEXT("unknown piece %s"), *Piece.Kind);
			return false;
		}
		FGFBeatHopObstacleInstance Obstacle;
		Obstacle.Kind = Kind;
		Obstacle.BeatTime = Out.Beatmap.BeatToSeconds(Piece.Beat);
		Obstacle.Lane = Lane;
		Obstacle.HeightCm = Piece.HeightCm;
		if (Obstacle.HeightCm <= 0.f && Kind == EGFBeatHopObstacle::MantleLedge)
		{
			Obstacle.HeightCm = Piece.Kind == TEXT("ledge") ? 200.f : 120.f;
		}
		Out.Obstacles.Add(Obstacle);
	}
	return true;
}

int32 GFBeatHopMaps::BestScore(FName MapId, const FString& PlayerKey)
{
	if (MapId.IsNone() || PlayerKey.IsEmpty())
	{
		return 0;
	}
	const int32* Found = GFBeatHopMapsPrivate::Bests().Scores.Find(GFBeatHopMapsPrivate::BestKey(MapId, PlayerKey));
	return Found ? *Found : 0;
}

void GFBeatHopMaps::NoteBest(FName MapId, const FString& PlayerKey, int32 Score)
{
	if (MapId.IsNone() || PlayerKey.IsEmpty() || Score <= 0)
	{
		return;
	}
	TMap<FString, int32>& Scores = GFBeatHopMapsPrivate::Bests().Scores;
	const FString Key = GFBeatHopMapsPrivate::BestKey(MapId, PlayerKey);
	const int32* Found = Scores.Find(Key);
	if (Found && *Found >= Score)
	{
		return;
	}
	Scores.Add(Key, Score);
	GFBeatHopMapsPrivate::SaveBests();
}

void GFBeatHopMaps::SetLayoutHook(FLayoutHook Hook)
{
	GFBeatHopMapsPrivate::HookSlot() = Hook;
}

void GFBeatHopMaps::NotifyLayout(AGFBeatHopCourse* Course)
{
	if (const FLayoutHook Hook = GFBeatHopMapsPrivate::HookSlot())
	{
		Hook(Course);
	}
}

FString GFBeatHopMaps::LoadCourse(UWorld* World, int32 Number)
{
#if UE_BUILD_SHIPPING
	return TEXT("Dev builds only.");
#else
	if (!World)
	{
		return TEXT("No world.");
	}
	const FGFBeatHopMapDef* Map = ByNumber(Number);
	if (!Map)
	{
		return FString::Printf(TEXT("No Beat Hop course %d."), Number);
	}
	for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
	{
		if (It->HasAuthority() && It->SelectMap(Map->Id))
		{
			UE_LOG(LogGF, Log, TEXT("Beat Hop: gf.BeatHop.Course %d (%s)"), Number, *Map->Title);
			return FString();
		}
	}
	return TEXT("No Beat Hop course actor in this world.");
#endif
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldAndArgs GFBeatHopCourseCommand(
	TEXT("gf.BeatHop.Course"),
	TEXT("Load Beat Hop course 1, 2 or 3. Dev builds only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 Number = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const FString Why = GFBeatHopMaps::LoadCourse(World, Number);
		if (!Why.IsEmpty())
		{
			UE_LOG(LogGF, Warning, TEXT("Beat Hop: %s"), *Why);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFBeatHopSectionCommand(
	TEXT("gf.BeatHop.Section"),
	TEXT("Jump to a 1-based section of the selected Beat Hop course. Dev builds only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 Section = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		if (!World)
		{
			return;
		}
		for (TActorIterator<AGFBeatHopCourse> It(World); It; ++It)
		{
			if (!It->HasAuthority())
			{
				continue;
			}
			const FString Why = It->DevJumpToSection(Section);
			if (!Why.IsEmpty())
			{
				UE_LOG(LogGF, Warning, TEXT("Beat Hop: %s"), *Why);
			}
			return;
		}
		UE_LOG(LogGF, Warning, TEXT("Beat Hop: No Beat Hop course actor in this world."));
	}));
#endif
