using UnrealBuildTool;

public class VoxelFinish : ModuleRules
{
	public VoxelFinish(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "Projects", "RHI" });
	}
}
