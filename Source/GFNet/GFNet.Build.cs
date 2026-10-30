using UnrealBuildTool;

public class GFNet : ModuleRules
{
	public GFNet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "GFCore", "GFVoxel", "GFCompanion" });
	}
}
