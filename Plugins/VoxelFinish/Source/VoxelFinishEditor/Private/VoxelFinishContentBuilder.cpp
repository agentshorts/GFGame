#include "VoxelFinishContentBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFmod.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "VoxelFinish.h"
#include "VoxelFinishLookup.h"

namespace VoxelFinishContent
{
	struct FMat
	{
		UMaterial* Material = nullptr;
		int32 Slot = 0;

		template <typename T>
		T* Make()
		{
			T* Expr = NewObject<T>(Material);
			Expr->MaterialExpressionEditorX = (Slot % 8) * 160;
			Expr->MaterialExpressionEditorY = (Slot / 8) * 140;
			++Slot;
			Material->GetExpressionCollection().AddExpression(Expr);
			return Expr;
		}
	};

	static UMaterialExpression* Const(FMat& M, float Value)
	{
		UMaterialExpressionConstant* Expr = M.Make<UMaterialExpressionConstant>();
		Expr->R = Value;
		return Expr;
	}

	static UMaterialExpression* Mask(FMat& M, UMaterialExpression* Input, bool bR, bool bG, bool bB, bool bA)
	{
		UMaterialExpressionComponentMask* Expr = M.Make<UMaterialExpressionComponentMask>();
		Expr->Input.Connect(0, Input);
		Expr->R = bR ? 1 : 0;
		Expr->G = bG ? 1 : 0;
		Expr->B = bB ? 1 : 0;
		Expr->A = bA ? 1 : 0;
		return Expr;
	}

	static UMaterialExpression* AddC(FMat& M, UMaterialExpression* A, float B)
	{
		UMaterialExpressionAdd* Expr = M.Make<UMaterialExpressionAdd>();
		Expr->A.Connect(0, A);
		Expr->ConstB = B;
		return Expr;
	}

	static UMaterialExpression* Add(FMat& M, UMaterialExpression* A, UMaterialExpression* B)
	{
		UMaterialExpressionAdd* Expr = M.Make<UMaterialExpressionAdd>();
		Expr->A.Connect(0, A);
		Expr->B.Connect(0, B);
		return Expr;
	}

	static UMaterialExpression* MulC(FMat& M, UMaterialExpression* A, float B)
	{
		UMaterialExpressionMultiply* Expr = M.Make<UMaterialExpressionMultiply>();
		Expr->A.Connect(0, A);
		Expr->ConstB = B;
		return Expr;
	}

	static UMaterialExpression* Mul(FMat& M, UMaterialExpression* A, UMaterialExpression* B)
	{
		UMaterialExpressionMultiply* Expr = M.Make<UMaterialExpressionMultiply>();
		Expr->A.Connect(0, A);
		Expr->B.Connect(0, B);
		return Expr;
	}

	static UMaterialExpression* DivC(FMat& M, UMaterialExpression* A, float B)
	{
		UMaterialExpressionDivide* Expr = M.Make<UMaterialExpressionDivide>();
		Expr->A.Connect(0, A);
		Expr->ConstB = B;
		return Expr;
	}

	static UMaterialExpression* Div(FMat& M, UMaterialExpression* A, UMaterialExpression* B)
	{
		UMaterialExpressionDivide* Expr = M.Make<UMaterialExpressionDivide>();
		Expr->A.Connect(0, A);
		Expr->B.Connect(0, B);
		return Expr;
	}

	static UMaterialExpression* FloorOf(FMat& M, UMaterialExpression* A)
	{
		UMaterialExpressionFloor* Expr = M.Make<UMaterialExpressionFloor>();
		Expr->Input.Connect(0, A);
		return Expr;
	}

	static UMaterialExpression* Fmod(FMat& M, UMaterialExpression* A, float B)
	{
		UMaterialExpressionFmod* Expr = M.Make<UMaterialExpressionFmod>();
		Expr->A.Connect(0, A);
		Expr->B.Connect(0, Const(M, B));
		return Expr;
	}

	static UMaterialExpression* Append(FMat& M, UMaterialExpression* A, UMaterialExpression* B)
	{
		UMaterialExpressionAppendVector* Expr = M.Make<UMaterialExpressionAppendVector>();
		Expr->A.Connect(0, A);
		Expr->B.Connect(0, B);
		return Expr;
	}

	static UMaterialExpression* AppendC(FMat& M, UMaterialExpression* A, float B)
	{
		return Append(M, A, Const(M, B));
	}

	static UMaterialExpression* Sample(FMat& M, UTexture* Texture, UMaterialExpression* UV)
	{
		UMaterialExpressionTextureSample* Expr = M.Make<UMaterialExpressionTextureSample>();
		Expr->Texture = Texture;
		Expr->SamplerType = SAMPLERTYPE_LinearColor;
		Expr->SamplerSource = SSM_FromTextureAsset;
		Expr->Coordinates.Connect(0, UV);
		return Expr;
	}

	/** The lookup is a texture parameter so each world can bind its own transient lookup (AVoxelFinishVolume::ApplyLookupTo). */
	static UMaterialExpression* SampleLookup(FMat& M, UTexture* Texture, UMaterialExpression* UV)
	{
		UMaterialExpressionTextureSampleParameter2D* Expr = M.Make<UMaterialExpressionTextureSampleParameter2D>();
		Expr->ParameterName = FVoxelFinishLookup::ParameterName;
		Expr->Texture = Texture;
		Expr->SamplerType = SAMPLERTYPE_LinearColor;
		Expr->SamplerSource = SSM_FromTextureAsset;
		Expr->Coordinates.Connect(0, UV);
		return Expr;
	}

	static bool SaveAsset(UPackage* Package, UObject* Asset, FString& OutError)
	{
		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		// A package loaded from disk keeps its file open through its linker; release it so the save can replace the file.
		// Everything the package should contain was just built in memory, so it is complete.
		ResetLoaders(Package);
		Package->MarkAsFullyLoaded();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.bSlowTask = false;
		const FSavePackageResultStruct Result = UPackage::Save(Package, Asset, *Filename, Args);
		if (Result.Result != ESavePackageResult::Success)
		{
			OutError = FString::Printf(TEXT("Failed to save %s (save result %d)"), *Package->GetName(), static_cast<int32>(Result.Result));
			return false;
		}
		return true;
	}

	static UTexture2D* MakeTexture(const FString& PackageName, const TArray<FColor>& Pixels, int32 Width, int32 Height, TextureAddress Address, FString& OutError)
	{
		if (UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName))))
		{
			// Compare the source size, not GetSizeX/GetSizeY: those read platform data, which is empty under -nullrhi
			// and made every headless run re-save the existing textures.
			if (Existing->Source.GetSizeX() == Width && Existing->Source.GetSizeY() == Height && Existing->Filter == TF_Nearest)
			{
				return Existing;
			}
		}
		UPackage* Package = CreatePackage(*PackageName);
		UTexture2D* Texture = NewObject<UTexture2D>(Package, *FPackageName::GetLongPackageAssetName(PackageName), RF_Public | RF_Standalone);
		Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->Filter = TF_Nearest;
		Texture->AddressX = Address;
		Texture->AddressY = Address;
		Texture->LODGroup = TEXTUREGROUP_ColorLookupTable;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		Texture->PostEditChange();
		Texture->Filter = TF_Nearest;
		if (!SaveAsset(Package, Texture, OutError))
		{
			return nullptr;
		}
		return Texture;
	}

	static bool LoadPng(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, FString& OutError)
	{
		TArray<uint8> Compressed;
		if (!FFileHelper::LoadFileToArray(Compressed, *Path))
		{
			OutError = FString::Printf(TEXT("Missing source texture %s"), *Path);
			return false;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()))
		{
			OutError = FString::Printf(TEXT("Could not decode %s"), *Path);
			return false;
		}
		OutWidth = Wrapper->GetWidth();
		OutHeight = Wrapper->GetHeight();
		TArray<uint8> Raw;
		if (Wrapper->GetRaw(ERGBFormat::Gray, 8, Raw) && Raw.Num() == OutWidth * OutHeight)
		{
			OutPixels.SetNum(Raw.Num());
			for (int32 Index = 0; Index < Raw.Num(); ++Index)
			{
				OutPixels[Index] = FColor(Raw[Index], Raw[Index], Raw[Index], 255);
			}
			return true;
		}
		Raw.Reset();
		if (Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw) && Raw.Num() == OutWidth * OutHeight * 4)
		{
			OutPixels.SetNum(OutWidth * OutHeight);
			FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), Raw.Num());
			return true;
		}
		OutError = FString::Printf(TEXT("Unsupported pixel format in %s"), *Path);
		return false;
	}

	static UMaterialExpression* BuildPaint(FMat& M, UTexture2D* Lookup, UTexture2D* Pattern, UTexture2D* Sparkle, UMaterialExpression*& OutEmissive)
	{
		UMaterialExpressionTextureCoordinate* UV2 = M.Make<UMaterialExpressionTextureCoordinate>();
		UV2->CoordinateIndex = 2;
		UMaterialExpression* FinishId = Mask(M, UV2, true, false, false, false);
		UMaterialExpression* LookupU = DivC(M, AddC(M, FinishId, 0.5f), static_cast<float>(VoxelFinish::LookupTextureWidth));

		UMaterialExpression* ColorA = SampleLookup(M, Lookup, AppendC(M, LookupU, 0.125f));
		UMaterialExpression* ColorB = SampleLookup(M, Lookup, AppendC(M, LookupU, 0.375f));
		UMaterialExpression* Tint = SampleLookup(M, Lookup, AppendC(M, LookupU, 0.625f));
		UMaterialExpression* Meta = SampleLookup(M, Lookup, AppendC(M, LookupU, 0.875f));

		UMaterialExpression* Packed = Mask(M, UV2, false, true, false, false);
		UMaterialExpression* FaceU = DivC(M, AddC(M, Fmod(M, Packed, static_cast<float>(VoxelFinish::AtlasCellTexels)), 0.5f), static_cast<float>(VoxelFinish::AtlasCellTexels));
		UMaterialExpression* FaceY = FloorOf(M, DivC(M, Packed, static_cast<float>(VoxelFinish::AtlasCellTexels)));
		UMaterialExpression* FaceV = DivC(M, AddC(M, FaceY, 0.5f), static_cast<float>(VoxelFinish::AtlasCellTexels));

		UMaterialExpression* PatternId = FloorOf(M, AddC(M, MulC(M, Mask(M, Meta, false, true, false, false), 255.f), 0.5f));
		UMaterialExpression* CellX = Fmod(M, PatternId, static_cast<float>(VoxelFinish::PatternAtlasGrid));
		UMaterialExpression* CellY = FloorOf(M, DivC(M, PatternId, static_cast<float>(VoxelFinish::PatternAtlasGrid)));
		UMaterialExpression* AtlasU = DivC(M, Add(M, CellX, FaceU), static_cast<float>(VoxelFinish::PatternAtlasGrid));
		UMaterialExpression* AtlasV = DivC(M, Add(M, CellY, FaceV), static_cast<float>(VoxelFinish::PatternAtlasGrid));
		UMaterialExpression* PatternSample = Sample(M, Pattern, Append(M, AtlasU, AtlasV));
		UMaterialExpression* PatternMask = Mul(M, Mask(M, PatternSample, true, false, false, false), Mask(M, ColorA, false, false, false, true));

		UMaterialExpressionLinearInterpolate* Lerped = M.Make<UMaterialExpressionLinearInterpolate>();
		Lerped->A.Connect(0, Mask(M, ColorA, true, true, true, false));
		Lerped->B.Connect(0, Mask(M, ColorB, true, true, true, false));
		Lerped->Alpha.Connect(0, PatternMask);
		UMaterialExpression* Paint = Mul(M, Lerped, Mask(M, Tint, true, true, true, false));

		UMaterialExpression* Glow = Mul(M, Mul(M, Paint, Mask(M, ColorB, false, false, false, true)), MulC(M, Mask(M, Tint, false, false, false, true), 8.f));

		UMaterialExpressionTime* Time = M.Make<UMaterialExpressionTime>();
		UMaterialExpressionFrac* FracTime = M.Make<UMaterialExpressionFrac>();
		FracTime->Input.Connect(0, MulC(M, Time, VoxelFinish::SparkleFramesPerSecond / static_cast<float>(VoxelFinish::SparkleFrameCount)));
		UMaterialExpression* Frame = FloorOf(M, MulC(M, FracTime, static_cast<float>(VoxelFinish::SparkleFrameCount)));
		UMaterialExpression* SparkU = DivC(M, Add(M, Frame, FaceU), static_cast<float>(VoxelFinish::SparkleFrameCount));
		UMaterialExpression* Spark = Sample(M, Sparkle, Append(M, SparkU, FaceV));
		UMaterialExpression* SparkTerm = Mul(M, Mask(M, Spark, true, false, false, false), Mask(M, Meta, true, false, false, false));

		UMaterialExpressionWorldPosition* WorldPos = M.Make<UMaterialExpressionWorldPosition>();
		UMaterialExpressionCameraPositionWS* CameraPos = M.Make<UMaterialExpressionCameraPositionWS>();
		UMaterialExpressionDistance* Distance = M.Make<UMaterialExpressionDistance>();
		Distance->A.Connect(0, CameraPos);
		Distance->B.Connect(0, WorldPos);
		UMaterialExpressionIf* Gate = M.Make<UMaterialExpressionIf>();
		Gate->A.Connect(0, Distance);
		Gate->ConstB = VoxelFinish::EffectDistanceCutoffCm;
		Gate->AGreaterThanB.Connect(0, Const(M, 0.f));
		Gate->AEqualsB.Connect(0, Const(M, 1.f));
		Gate->ALessThanB.Connect(0, Const(M, 1.f));

		OutEmissive = Mul(M, Add(M, Glow, SparkTerm), Gate);
		return Paint;
	}

	static UMaterial* MakeMaster(const FString& PackageName, UTexture2D* Lookup, UTexture2D* Pattern, UTexture2D* Sparkle, bool bEmissive, bool bRebuild, FString& OutError)
	{
		const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
		UMaterial* Existing = bRebuild ? nullptr : LoadObject<UMaterial>(nullptr, *ObjectPath);
		if (Existing)
		{
			if (UMaterialEditorOnlyData* Editor = Existing->GetEditorOnlyData())
			{
				if (Editor->BaseColor.IsConnected() && (!bEmissive || Editor->EmissiveColor.IsConnected()))
				{
					return Existing;
				}
			}
		}

		UPackage* Package = CreatePackage(*PackageName);
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			// Load the saved master completely first; a partially loaded package cannot be saved over.
			Package->FullyLoad();
		}
		if (UMaterial* Previous = FindObject<UMaterial>(Package, *AssetName))
		{
			// Rebuilding a material that is already in memory: move the old object (and its expressions) out of the
			// package so the new graph is saved alone instead of being constructed over the loaded one.
			Previous->ClearFlags(RF_Public | RF_Standalone);
			Previous->Rename(*MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), FName(*(AssetName + TEXT("_Replaced")))).ToString(),
				GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
		UMaterial* Material = NewObject<UMaterial>(Package, *AssetName, RF_Public | RF_Standalone);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_DefaultLit);
		Material->TwoSided = false;
		Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);

		FMat Graph;
		Graph.Material = Material;
		UMaterialExpression* Emissive = nullptr;
		UMaterialExpression* Paint = BuildPaint(Graph, Lookup, Pattern, Sparkle, Emissive);
		if (!bEmissive)
		{
			Emissive = Const(Graph, 0.f);
		}
		if (UMaterialEditorOnlyData* Editor = Material->GetEditorOnlyData())
		{
			Editor->BaseColor.Connect(0, Paint);
			Editor->EmissiveColor.Connect(0, Emissive);
		}
		else
		{
			OutError = TEXT("Material editor data was not created");
			return nullptr;
		}
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		if (!SaveAsset(Package, Material, OutError))
		{
			return nullptr;
		}
		return Material;
	}
}

bool FVoxelFinishContentBuilder::EnsureAssets(FString& OutError, bool bRebuildMaterials)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VoxelFinish"));
	if (!Plugin.IsValid())
	{
		OutError = TEXT("VoxelFinish plugin is not mounted");
		return false;
	}
	const FString Content = Plugin->GetContentDir();

	TArray<FColor> PatternPixels;
	TArray<FColor> SparklePixels;
	TArray<FColor> StaticPixels;
	int32 PatternW = 0, PatternH = 0, SparkleW = 0, SparkleH = 0, StaticW = 0, StaticH = 0;
	if (!VoxelFinishContent::LoadPng(FPaths::Combine(Content, TEXT("Textures/Atlas/T_Paint_PatternAtlas_v1.png")), PatternPixels, PatternW, PatternH, OutError)
		|| !VoxelFinishContent::LoadPng(FPaths::Combine(Content, TEXT("Textures/Flipbooks/T_Paint_SparkleFlipbook_8f_v1.png")), SparklePixels, SparkleW, SparkleH, OutError)
		|| !VoxelFinishContent::LoadPng(FPaths::Combine(Content, TEXT("Textures/Atlas/T_FX_StaticAtlas_v1.png")), StaticPixels, StaticW, StaticH, OutError))
	{
		return false;
	}

	UTexture2D* Pattern = VoxelFinishContent::MakeTexture(TEXT("/VoxelFinish/Textures/Atlas/T_PatternAtlas"), PatternPixels, PatternW, PatternH, TA_Wrap, OutError);
	UTexture2D* Sparkle = VoxelFinishContent::MakeTexture(TEXT("/VoxelFinish/Textures/Flipbooks/T_SparkleFlipbook"), SparklePixels, SparkleW, SparkleH, TA_Clamp, OutError);
	UTexture2D* StaticAtlas = VoxelFinishContent::MakeTexture(TEXT("/VoxelFinish/Textures/Atlas/T_StaticAtlas"), StaticPixels, StaticW, StaticH, TA_Wrap, OutError);
	TArray<FColor> LookupPixels;
	LookupPixels.SetNumZeroed(VoxelFinish::LookupTextureWidth * VoxelFinish::LookupTextureHeight);
	UTexture2D* Lookup = VoxelFinishContent::MakeTexture(TEXT("/VoxelFinish/Textures/Atlas/T_FinishLookup"), LookupPixels, VoxelFinish::LookupTextureWidth, VoxelFinish::LookupTextureHeight, TA_Clamp, OutError);
	if (!Pattern || !Sparkle || !StaticAtlas || !Lookup)
	{
		return false;
	}

	UMaterial* Opaque = VoxelFinishContent::MakeMaster(TEXT("/VoxelFinish/Materials/M_FinishOpaque"), Lookup, Pattern, Sparkle, false, bRebuildMaterials, OutError);
	if (!Opaque)
	{
		return false;
	}
	UMaterial* Emissive = VoxelFinishContent::MakeMaster(TEXT("/VoxelFinish/Materials/M_FinishEmissive"), Lookup, Pattern, Sparkle, true, bRebuildMaterials, OutError);
	return Emissive != nullptr;
}
