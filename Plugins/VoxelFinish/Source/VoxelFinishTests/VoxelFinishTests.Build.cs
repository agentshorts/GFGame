using UnrealBuildTool;

public class VoxelFinishTests : ModuleRules
{
	public VoxelFinishTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "VoxelFinish" });
		PrivateDependencyModuleNames.AddRange(new string[] { "VoxelFinishEditor", "UnrealEd" });
	}
}
