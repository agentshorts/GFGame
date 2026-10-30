using UnrealBuildTool;

public class GFVoxel : ModuleRules
{
	public GFVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "NetCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
