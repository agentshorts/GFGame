#include "GFMiningRangeLayout.h"

#include "GFDataTables.h"
#include "GFLog.h"
#include "GFVoxelTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace GFMiningMaps
{
	bool IsNamedMap(const FString& MapOrPackage, const TCHAR* ShortName)
	{
		const FString Short(ShortName);
		return MapOrPackage.Equals(Short, ESearchCase::IgnoreCase)
			|| MapOrPackage.EndsWith(TEXT("/") + Short, ESearchCase::IgnoreCase)
			|| MapOrPackage.EndsWith(TEXT("_") + Short, ESearchCase::IgnoreCase);
	}

	static bool WorldIs(const UWorld* World, const TCHAR* ShortName)
	{
		if (!World)
		{
			return false;
		}
		if (IsNamedMap(World->GetMapName(), ShortName))
		{
			return true;
		}
		const UPackage* Package = World->GetOutermost();
		return Package && IsNamedMap(Package->GetName(), ShortName);
	}

	bool IsMiningRange(const UWorld* World)
	{
		return WorldIs(World, TEXT("L_MiningRange"));
	}

	bool IsHub(const UWorld* World)
	{
		return WorldIs(World, TEXT("L_Hub"));
	}
}

namespace
{
	static const FVector DefaultBadgeOffset(4.f, -44.f, 10.f);

	static bool ReadVec3(const FJsonObject& Obj, const TCHAR* Key, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj.TryGetArrayField(Key, Arr) || !Arr || Arr->Num() < 3)
		{
			return false;
		}
		Out.X = (*Arr)[0].IsValid() ? (*Arr)[0]->AsNumber() : 0.0;
		Out.Y = (*Arr)[1].IsValid() ? (*Arr)[1]->AsNumber() : 0.0;
		Out.Z = (*Arr)[2].IsValid() ? (*Arr)[2]->AsNumber() : 0.0;
		return true;
	}

	static bool ReadIVec(const FJsonObject& Obj, const TCHAR* Key, FIntVector& Out)
	{
		FVector V;
		if (!ReadVec3(Obj, Key, V))
		{
			return false;
		}
		Out = FIntVector(FMath::RoundToInt(V.X), FMath::RoundToInt(V.Y), FMath::RoundToInt(V.Z));
		return true;
	}

	static FString PropName(const FJsonObject& Obj, const TCHAR* Key, const TCHAR* Fallback)
	{
		FString Name;
		if (Obj.TryGetStringField(Key, Name) && !Name.IsEmpty())
		{
			return Name;
		}
		return Fallback;
	}

	/** Viewer's right on a sign whose front is +X is local -Y. */
	static FVector BadgeOffsetOf(const FJsonObject& Obj)
	{
		FVector Ue;
		if (ReadVec3(Obj, TEXT("sign_local_offset_ue_cm"), Ue))
		{
			return Ue;
		}
		const TSharedPtr<FJsonObject>* Sem = nullptr;
		if (Obj.TryGetObjectField(TEXT("sign_local_offset_cm"), Sem) && Sem && (*Sem).IsValid())
		{
			double Forward = 4.0;
			double Right = 44.0;
			double Up = 10.0;
			(*Sem)->TryGetNumberField(TEXT("forward"), Forward);
			(*Sem)->TryGetNumberField(TEXT("right"), Right);
			(*Sem)->TryGetNumberField(TEXT("up"), Up);
			return FVector(Forward, -Right, Up);
		}
		return DefaultBadgeOffset;
	}

	static void ReadSignLines(const FJsonObject& Sign, FGFMiningRow& Row)
	{
		const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
		if (Sign.TryGetArrayField(TEXT("text"), Lines) && Lines)
		{
			if (Lines->IsValidIndex(0) && (*Lines)[0].IsValid()) { Row.Line1 = (*Lines)[0]->AsString(); }
			if (Lines->IsValidIndex(1) && (*Lines)[1].IsValid()) { Row.Line2 = (*Lines)[1]->AsString(); }
			if (Lines->IsValidIndex(2) && (*Lines)[2].IsValid()) { Row.Line3 = (*Lines)[2]->AsString(); }
		}
		FString L1, L2, L3;
		if (Sign.TryGetStringField(TEXT("line1"), L1)) { Row.Line1 = L1; }
		if (Sign.TryGetStringField(TEXT("line2"), L2)) { Row.Line2 = L2; }
		if (Sign.TryGetStringField(TEXT("line3"), L3)) { Row.Line3 = L3; }
		double Yaw = Row.SignYaw;
		if (Sign.TryGetNumberField(TEXT("yaw_deg"), Yaw)) { Row.SignYaw = static_cast<float>(Yaw); }
		FVector At;
		if (ReadVec3(Sign, TEXT("location_cm"), At)) { Row.SignCm = At; }
	}

	static bool ParseRoot(const FString& Json, TSharedPtr<FJsonObject>& Out, FString& OutError)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Out) || !Out.IsValid())
		{
			OutError = TEXT("not valid JSON");
			return false;
		}
		return true;
	}

	static void AddCam(TArray<FGFMiningCam>& Cams, const FString& Name, const FJsonObject& Obj)
	{
		FGFMiningCam Cam;
		Cam.Name = Name;
		ReadVec3(Obj, TEXT("location_cm"), Cam.LocationCm);
		ReadVec3(Obj, TEXT("look_at_cm"), Cam.LookAtCm);
		Cams.Add(Cam);
	}
}

const FGFDressingSign* FGFMiningDressing::FindSign(const FString& RowId) const
{
	for (const FGFDressingSign& Sign : Signs)
	{
		if (Sign.RowId == RowId)
		{
			return &Sign;
		}
	}
	return nullptr;
}

const FGFDressingBadge* FGFMiningDressing::FindBadge(const FString& RowId) const
{
	for (const FGFDressingBadge& Badge : Badges)
	{
		if (Badge.RowId == RowId)
		{
			return &Badge;
		}
	}
	return nullptr;
}

FString FGFMiningRangeLayout::RowsPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/MiningRange/MiningRange_Rows_v1.json"));
}

FString FGFMiningRangeLayout::TreePath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Data/MiningRange/MiningRange_Tree_Verdant_v1.json"));
}

FString FGFMiningRangeLayout::DressingPath()
{
	const FString Flat = FPaths::ProjectDir() / TEXT("Data/MiningRange/MiningRange_Dressing_v2.json");
	if (FPaths::FileExists(Flat))
	{
		return FPaths::ConvertRelativePathToFull(Flat);
	}
	const FString Nested = FPaths::ProjectDir() / TEXT("Data/MiningRange/MiningRange_Dressing_v2/MiningRange_Dressing_v2.json");
	if (FPaths::FileExists(Nested))
	{
		return FPaths::ConvertRelativePathToFull(Nested);
	}
	return FPaths::ConvertRelativePathToFull(Flat);
}

bool FGFMiningRangeLayout::ParseDressingString(const FString& Json, FGFMiningDressing& Out, FString& OutError)
{
	Out = FGFMiningDressing();
	TSharedPtr<FJsonObject> Root;
	if (!ParseRoot(Json, Root, OutError))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Signs = nullptr;
	if (!Root->TryGetArrayField(TEXT("signs"), Signs) || !Signs || Signs->Num() == 0)
	{
		OutError = TEXT("dressing has no signs");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Signs)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
		{
			continue;
		}
		FGFDressingSign Sign;
		(*Obj)->TryGetStringField(TEXT("row_id"), Sign.RowId);
		Sign.Mesh = PropName(**Obj, TEXT("mesh"), TEXT("SM_Prop_Range_Sign"));
		ReadVec3(**Obj, TEXT("location_cm"), Sign.LocationCm);
		double Yaw = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("yaw_deg"), Yaw)) { Sign.YawDeg = static_cast<float>(Yaw); }
		Out.Signs.Add(Sign);
	}
	if (Out.Signs.Num() == 0)
	{
		OutError = TEXT("dressing signs were empty");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Badges = nullptr;
	if (Root->TryGetArrayField(TEXT("badges"), Badges) && Badges)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Badges)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				continue;
			}
			FGFDressingBadge Badge;
			(*Obj)->TryGetStringField(TEXT("row_id"), Badge.RowId);
			Badge.Mesh = PropName(**Obj, TEXT("mesh"), TEXT("SM_Prop_Range_Badge_SpellProof"));
			Badge.OffsetCm = BadgeOffsetOf(**Obj);
			double Yaw = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("yaw_deg"), Yaw)) { Badge.YawDeg = static_cast<float>(Yaw); }
			Out.Badges.Add(Badge);
		}
	}

	const TSharedPtr<FJsonObject>* LeverObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("reset_lever"), LeverObj) || !LeverObj)
	{
		Root->TryGetObjectField(TEXT("lever"), LeverObj);
	}
	if (LeverObj && (*LeverObj).IsValid())
	{
		const TSharedPtr<FJsonObject>* Base = nullptr;
		const TSharedPtr<FJsonObject>* Handle = nullptr;
		(*LeverObj)->TryGetObjectField(TEXT("base"), Base);
		(*LeverObj)->TryGetObjectField(TEXT("handle"), Handle);
		const FJsonObject& BaseObj = (Base && (*Base).IsValid()) ? **Base : **LeverObj;
		Out.Lever.BaseMesh = PropName(BaseObj, TEXT("mesh"), TEXT("SM_Prop_Range_ResetLever_Base"));
		ReadVec3(BaseObj, TEXT("location_cm"), Out.Lever.LocationCm);
		double Yaw = Out.Lever.YawDeg;
		if (BaseObj.TryGetNumberField(TEXT("yaw_deg"), Yaw)) { Out.Lever.YawDeg = static_cast<float>(Yaw); }
		if (Handle && (*Handle).IsValid())
		{
			Out.Lever.HandleMesh = PropName(**Handle, TEXT("mesh"), TEXT("SM_Prop_Range_ResetLever_Handle"));
			FVector Offset;
			if (ReadVec3(**Handle, TEXT("local_offset_cm"), Offset) || ReadVec3(**Handle, TEXT("offset_cm"), Offset))
			{
				Out.Lever.HandleOffsetCm = Offset;
			}
			double Pitch = 0.0;
			if ((*Handle)->TryGetNumberField(TEXT("rest_pitch_deg"), Pitch)) { Out.Lever.RestPitch = static_cast<float>(Pitch); }
			if ((*Handle)->TryGetNumberField(TEXT("pulled_pitch_deg"), Pitch)) { Out.Lever.PulledPitch = static_cast<float>(Pitch); }
		}
		else
		{
			Out.Lever.HandleMesh = TEXT("SM_Prop_Range_ResetLever_Handle");
		}
		Out.Lever.bPresent = true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Trims = nullptr;
	if (!Root->TryGetArrayField(TEXT("lava_edge_trim"), Trims) || !Trims)
	{
		if (!Root->TryGetArrayField(TEXT("trims"), Trims) || !Trims)
		{
			Root->TryGetArrayField(TEXT("trim"), Trims);
		}
	}
	if (Trims)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Trims)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				continue;
			}
			FGFDressingProp Prop;
			Prop.Mesh = PropName(**Obj, TEXT("mesh"), TEXT("SM_Prop_Range_LavaEdgeTrim"));
			ReadVec3(**Obj, TEXT("location_cm"), Prop.LocationCm);
			double Yaw = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("yaw_deg"), Yaw)) { Prop.YawDeg = static_cast<float>(Yaw); }
			Out.Trims.Add(Prop);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Tiles = nullptr;
	if (!Root->TryGetArrayField(TEXT("floor_tints"), Tiles) || !Tiles)
	{
		if (!Root->TryGetArrayField(TEXT("tiles"), Tiles) || !Tiles)
		{
			if (!Root->TryGetArrayField(TEXT("floor_tiles"), Tiles) || !Tiles)
			{
				Root->TryGetArrayField(TEXT("floors"), Tiles);
			}
		}
	}
	if (Tiles)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Tiles)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				continue;
			}
			FGFDressingProp Prop;
			Prop.Mesh = PropName(**Obj, TEXT("mesh"), TEXT("SM_Prop_Range_FloorTint"));
			(*Obj)->TryGetStringField(TEXT("material"), Prop.Material);
			(*Obj)->TryGetStringField(TEXT("zone"), Prop.Zone);
			ReadVec3(**Obj, TEXT("location_cm"), Prop.LocationCm);
			double Yaw = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("yaw_deg"), Yaw)) { Prop.YawDeg = static_cast<float>(Yaw); }
			Out.Tiles.Add(Prop);
		}
	}

	Out.bLoaded = true;
	return true;
}

bool FGFMiningRangeLayout::LoadDressingFile(const FString& Path, FGFMiningDressing& Out, FString& OutError)
{
	Out = FGFMiningDressing();
	if (Path.IsEmpty() || !FPaths::FileExists(Path))
	{
		OutError = TEXT("dressing file is missing");
		return false;
	}
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = TEXT("dressing file could not be read");
		return false;
	}
	return ParseDressingString(Json, Out, OutError);
}

bool FGFMiningRangeLayout::LoadProject()
{
	*this = FGFMiningRangeLayout();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *RowsPath()))
	{
		Error = TEXT("rows file is missing");
		UE_LOG(LogGF, Warning, TEXT("Mining range: %s"), *Error);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!ParseRoot(Json, Root, Error))
	{
		UE_LOG(LogGF, Warning, TEXT("Mining range: rows %s"), *Error);
		return false;
	}

	const TSharedPtr<FJsonObject>* Spawn = nullptr;
	if (Root->TryGetObjectField(TEXT("spawn"), Spawn) && Spawn && (*Spawn).IsValid())
	{
		ReadVec3(**Spawn, TEXT("location_cm"), SpawnCm);
		double Yaw = SpawnYaw;
		if ((*Spawn)->TryGetNumberField(TEXT("yaw_deg"), Yaw)) { SpawnYaw = static_cast<float>(Yaw); }
	}
	const TSharedPtr<FJsonObject>* Reset = nullptr;
	if (Root->TryGetObjectField(TEXT("reset_lever"), Reset) && Reset && (*Reset).IsValid())
	{
		ReadVec3(**Reset, TEXT("location_cm"), ResetCm);
		double Yaw = ResetYaw;
		if ((*Reset)->TryGetNumberField(TEXT("yaw_deg"), Yaw)) { ResetYaw = static_cast<float>(Yaw); }
	}
	const TSharedPtr<FJsonObject>* Gate = nullptr;
	if (Root->TryGetObjectField(TEXT("return_gate"), Gate) && Gate && (*Gate).IsValid())
	{
		ReadVec3(**Gate, TEXT("location_cm"), ReturnCm);
		double Yaw = ReturnYaw;
		if ((*Gate)->TryGetNumberField(TEXT("yaw_deg"), Yaw)) { ReturnYaw = static_cast<float>(Yaw); }
	}

	const TSharedPtr<FJsonObject>* CamsObj = nullptr;
	if (Root->TryGetObjectField(TEXT("hud_capture_cams"), CamsObj) && CamsObj && (*CamsObj).IsValid())
	{
		const TSharedPtr<FJsonObject>* Wide = nullptr;
		if ((*CamsObj)->TryGetObjectField(TEXT("wide"), Wide) && Wide && (*Wide).IsValid())
		{
			AddCam(Cams, TEXT("Wide"), **Wide);
		}
		const TSharedPtr<FJsonObject>* Per = nullptr;
		if ((*CamsObj)->TryGetObjectField(TEXT("per_zone"), Per) && Per && (*Per).IsValid())
		{
			const TCHAR* ZoneNames[] = { TEXT("Common"), TEXT("Verdant"), TEXT("Rime"), TEXT("Cinderwrights"), TEXT("Ore") };
			for (const TCHAR* ZoneName : ZoneNames)
			{
				const TSharedPtr<FJsonObject>* ZoneCam = nullptr;
				if ((*Per)->TryGetObjectField(ZoneName, ZoneCam) && ZoneCam && (*ZoneCam).IsValid())
				{
					AddCam(Cams, ZoneName, **ZoneCam);
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ZoneArr = nullptr;
	if (Root->TryGetArrayField(TEXT("zones"), ZoneArr) && ZoneArr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ZoneArr)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			FString Name;
			if (Value.IsValid() && Value->TryGetObject(Obj) && Obj && (*Obj).IsValid() && (*Obj)->TryGetStringField(TEXT("zone"), Name))
			{
				Zones.Add(Name);
			}
		}
	}

	float LaneFrom = -1000.f;
	float LaneTo = 12000.f;
	float LaneWidth = 1200.f;
	float LaneCentre = 0.f;
	const TSharedPtr<FJsonObject>* Lane = nullptr;
	if (Root->TryGetObjectField(TEXT("lane"), Lane) && Lane && (*Lane).IsValid())
	{
		double N = 0.0;
		if ((*Lane)->TryGetNumberField(TEXT("from_x_cm"), N)) { LaneFrom = static_cast<float>(N); }
		if ((*Lane)->TryGetNumberField(TEXT("to_x_cm"), N)) { LaneTo = static_cast<float>(N); }
		if ((*Lane)->TryGetNumberField(TEXT("width_cm"), N)) { LaneWidth = static_cast<float>(N); }
		if ((*Lane)->TryGetNumberField(TEXT("centre_y_cm"), N)) { LaneCentre = static_cast<float>(N); }
	}

	TMap<FIntVector, FGFMiningCell> Unique;
	auto Put = [&Unique](const FGFMiningCell& Cell)
	{
		Unique.Add(Cell.Range, Cell);
	};

	const int32 X0 = FMath::FloorToInt(LaneFrom / 100.f);
	const int32 X1 = FMath::FloorToInt((LaneTo - 1.f) / 100.f);
	const int32 Y0 = FMath::FloorToInt((LaneCentre - LaneWidth * 0.5f) / 100.f);
	const int32 Y1 = FMath::FloorToInt((LaneCentre + LaneWidth * 0.5f - 1.f) / 100.f);
	for (int32 X = X0; X <= X1; ++X)
	{
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			FGFMiningCell Cell;
			Cell.Range = FIntVector(X, Y, -1);
			Cell.Authored = TEXT("Stone");
			Cell.bProtected = true;
			Put(Cell);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* RowArr = nullptr;
	if (!Root->TryGetArrayField(TEXT("rows"), RowArr) || !RowArr)
	{
		Error = TEXT("rows array is missing");
		return false;
	}

	int32 TreeRow = INDEX_NONE;
	FIntVector TreeMin = FIntVector::ZeroValue;
	FIntVector TreeSize = FIntVector::ZeroValue;
	for (const TSharedPtr<FJsonValue>& Value : *RowArr)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
		{
			continue;
		}
		const FJsonObject& RowObj = **Obj;
		FGFMiningRow Row;
		RowObj.TryGetStringField(TEXT("row_id"), Row.Id);
		RowObj.TryGetStringField(TEXT("zone"), Row.Zone);
		RowObj.TryGetStringField(TEXT("kind"), Row.Kind);
		FString Block;
		if (RowObj.TryGetStringField(TEXT("block"), Block))
		{
			Row.Block = FName(*Block);
		}
		RowObj.TryGetStringField(TEXT("mesh"), Row.StandInMesh);
		RowObj.TryGetBoolField(TEXT("spell_proof"), Row.bSpellProof);
		RowObj.TryGetBoolField(TEXT("protected"), Row.bProtected);
		RowObj.TryGetBoolField(TEXT("placed"), Row.bPlaced);
		const TSharedPtr<FJsonObject>* Sign = nullptr;
		if (RowObj.TryGetObjectField(TEXT("sign"), Sign) && Sign && (*Sign).IsValid())
		{
			ReadSignLines(**Sign, Row);
		}
		const int32 Index = Rows.Num();
		FIntVector Min = FIntVector::ZeroValue;
		FIntVector Size = FIntVector::ZeroValue;
		ReadIVec(RowObj, TEXT("cell_min"), Min);
		ReadIVec(RowObj, TEXT("size_cells"), Size);
		const bool bTree = Row.Kind == TEXT("tree_plot");
		if (bTree)
		{
			TreeRow = Index;
			TreeMin = Min;
			TreeSize = Size;
			for (int32 X = 0; X < Size.X; ++X)
			{
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					FGFMiningCell Grass;
					Grass.Range = FIntVector(Min.X + X, Min.Y + Y, -1);
					Grass.Authored = TEXT("Grass");
					Grass.bProtected = true;
					Put(Grass);
				}
			}
		}
		else if (!Row.Block.IsNone() && Size.X > 0 && Size.Y > 0 && Size.Z > 0)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					for (int32 Z = 0; Z < Size.Z; ++Z)
					{
						FGFMiningCell Cell;
						Cell.Range = Min + FIntVector(X, Y, Z);
						Cell.Authored = Row.Block;
						Cell.RowIndex = Index;
						Cell.bProtected = Row.bProtected;
						Cell.bSpellProof = Row.bSpellProof;
						Put(Cell);
					}
				}
			}
		}
		const TSharedPtr<FJsonObject>* Lip = nullptr;
		if (RowObj.TryGetObjectField(TEXT("lip"), Lip) && Lip && (*Lip).IsValid())
		{
			FIntVector LipMin = FIntVector::ZeroValue;
			FIntVector LipSize = FIntVector::ZeroValue;
			ReadIVec(**Lip, TEXT("cell_min"), LipMin);
			ReadIVec(**Lip, TEXT("size_cells"), LipSize);
			FString LipBlock;
			(*Lip)->TryGetStringField(TEXT("block"), LipBlock);
			const FName LipId = LipBlock.IsEmpty() ? FName(TEXT("Basalt")) : FName(*LipBlock);
			auto PutLip = [&](const FIntVector& At)
			{
				FGFMiningCell Cell;
				Cell.Range = At;
				Cell.Authored = LipId;
				Cell.RowIndex = Index;
				Cell.bProtected = true;
				Put(Cell);
			};
			for (int32 X = 0; X < LipSize.X; ++X)
			{
				for (int32 Y = 0; Y < LipSize.Y; ++Y)
				{
					for (int32 Z = 0; Z < LipSize.Z; ++Z)
					{
						PutLip(LipMin + FIntVector(X, Y, Z));
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Caps = nullptr;
			if ((*Lip)->TryGetArrayField(TEXT("also_end_caps"), Caps) && Caps)
			{
				for (const TSharedPtr<FJsonValue>& Cap : *Caps)
				{
					const TArray<TSharedPtr<FJsonValue>>* Triple = nullptr;
					if (Cap.IsValid() && Cap->TryGetArray(Triple) && Triple && Triple->Num() >= 3)
					{
						PutLip(FIntVector(
							FMath::RoundToInt((*Triple)[0]->AsNumber()),
							FMath::RoundToInt((*Triple)[1]->AsNumber()),
							FMath::RoundToInt((*Triple)[2]->AsNumber())));
					}
				}
			}
		}
		Rows.Add(Row);
	}

	FString TreeJson;
	if (TreeRow != INDEX_NONE && FFileHelper::LoadFileToString(TreeJson, *TreePath()))
	{
		TSharedPtr<FJsonObject> TreeRoot;
		FString TreeError;
		if (ParseRoot(TreeJson, TreeRoot, TreeError))
		{
			const TArray<TSharedPtr<FJsonValue>>* TreeCells = nullptr;
			if (TreeRoot->TryGetArrayField(TEXT("cells"), TreeCells) && TreeCells)
			{
				for (const TSharedPtr<FJsonValue>& Value : *TreeCells)
				{
					const TSharedPtr<FJsonObject>* Obj = nullptr;
					if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
					{
						continue;
					}
					double I = 0, J = 0, K = 0;
					(*Obj)->TryGetNumberField(TEXT("i"), I);
					(*Obj)->TryGetNumberField(TEXT("j"), J);
					(*Obj)->TryGetNumberField(TEXT("k"), K);
					FString Block;
					(*Obj)->TryGetStringField(TEXT("block"), Block);
					FGFMiningCell Cell;
					Cell.Range = TreeMin + FIntVector(FMath::RoundToInt(I), FMath::RoundToInt(J), FMath::RoundToInt(K));
					Cell.Authored = FName(*Block);
					Cell.RowIndex = TreeRow;
					Put(Cell);
					TreeLogs += Cell.Authored == TEXT("Log") ? 1 : 0;
					TreeLeaves += Cell.Authored == TEXT("Leaves") ? 1 : 0;
				}
			}
		}
		else
		{
			UE_LOG(LogGF, Warning, TEXT("Mining range: tree %s"), *TreeError);
		}
	}
	else if (TreeRow != INDEX_NONE)
	{
		UE_LOG(LogGF, Warning, TEXT("Mining range: tree file is missing, the plot stays grass"));
	}
	(void)TreeSize;

	FIntVector Min(MAX_int32, MAX_int32, MAX_int32);
	FIntVector Max(MIN_int32, MIN_int32, MIN_int32);
	for (const TPair<FIntVector, FGFMiningCell>& Pair : Unique)
	{
		Min.X = FMath::Min(Min.X, Pair.Key.X);
		Min.Y = FMath::Min(Min.Y, Pair.Key.Y);
		Min.Z = FMath::Min(Min.Z, Pair.Key.Z);
		Max.X = FMath::Max(Max.X, Pair.Key.X);
		Max.Y = FMath::Max(Max.Y, Pair.Key.Y);
		Max.Z = FMath::Max(Max.Z, Pair.Key.Z);
		Cells.Add(Pair.Value);
	}
	if (Cells.Num() == 0)
	{
		Error = TEXT("no cells");
		return false;
	}
	Min -= FIntVector(1, 1, 1);
	Max += FIntVector(1, 1, 1);
	Origin = Min;
	GridSize = Max - Min + FIntVector(1, 1, 1);

	FString DressError;
	if (!LoadDressingFile(DressingPath(), Dressing, DressError))
	{
		UE_LOG(LogGF, Log, TEXT("Mining range: dressing fallback (%s)"), *DressError);
		Dressing = FGFMiningDressing();
	}
	bLoaded = true;
	return true;
}

void FGFMiningRangeLayout::ApplyBlockTable(const UDataTable* Table)
{
	auto Known = [Table](FName Id)
	{
		return Table && !Id.IsNone() && Table->FindRow<FGFBlockRow>(Id, TEXT("MineRange"), false) != nullptr;
	};
	for (FGFMiningCell& Cell : Cells)
	{
		Cell.Stamp = (!Cell.Authored.IsNone() && Known(Cell.Authored)) ? Cell.Authored : FName(TEXT("Stone"));
		if (Cell.Authored.IsNone())
		{
			Cell.Stamp = NAME_None;
		}
		Cell.bBreakable = !Cell.bProtected && !Cell.bSpellProof && !Cell.Stamp.IsNone();
	}
	for (FGFMiningRow& Row : Rows)
	{
		const bool bStation = Row.Kind == TEXT("tree_plot") || Row.Kind == TEXT("station");
		const FGFBlockRow* Def = (!bStation && Table) ? Table->FindRow<FGFBlockRow>(Row.Block, TEXT("MineRange"), false) : nullptr;
		if (!Def)
		{
			Row.Sign1 = Row.Line1;
			Row.Sign2 = Row.Line2;
			Row.Sign3 = Row.Line3;
			continue;
		}
		FString Name = Def->DisplayName.ToString();
		if (Name.IsEmpty() || Name.Contains(TEXT("NSLOCTEXT")))
		{
			Name = Row.Block.ToString();
		}
		const FString Drop = Def->DropItem.IsNone() ? Row.Block.ToString() : Def->DropItem.ToString();
		Row.Sign1 = Name;
		Row.Sign2 = FString::Printf(TEXT("Hardness %s"), *FString::SanitizeFloat(Def->Hardness));
		Row.Sign3 = FString::Printf(TEXT("drops %s"), *Drop);
	}
}

int32 FGFMiningRangeLayout::BreakableCount(int32 RowIndex) const
{
	int32 N = 0;
	for (const FGFMiningCell& Cell : Cells)
	{
		N += (Cell.RowIndex == RowIndex && Cell.bBreakable) ? 1 : 0;
	}
	return N;
}

int32 FGFMiningRangeLayout::RowIndexOf(const FString& RowId) const
{
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		if (Rows[i].Id.Equals(RowId, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}
	return INDEX_NONE;
}
