using UnrealBuildTool;

public class VoxelFinishEditor : ModuleRules
{
	public VoxelFinishEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "VoxelFinish" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AssetRegistry", "ImageWrapper", "Projects" });
	}
}
