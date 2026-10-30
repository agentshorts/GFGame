#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "VoxelFinishContentBuilder.h"

DEFINE_LOG_CATEGORY_STATIC(LogVoxelFinishEditor, Log, All);

/** Imports the source PNGs and builds the lookup, atlases, and master materials. Writes .uasset files, so it is never run by a test. */
static FAutoConsoleCommand GVoxelFinishBuildContent(
	TEXT("VoxelFinish.BuildContent"),
	TEXT("Creates any missing VoxelFinish textures and master materials under /VoxelFinish and saves them. Pass 'rebuild' to regenerate the existing master materials."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bRebuild = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("rebuild"), ESearchCase::IgnoreCase); });
		FString Error;
		if (FVoxelFinishContentBuilder::EnsureAssets(Error, bRebuild))
		{
			UE_LOG(LogVoxelFinishEditor, Display, TEXT("VoxelFinish content is up to date."));
		}
		else
		{
			UE_LOG(LogVoxelFinishEditor, Error, TEXT("VoxelFinish.BuildContent failed: %s"), *Error);
		}
	}));

IMPLEMENT_MODULE(FDefaultModuleImpl, VoxelFinishEditor);
