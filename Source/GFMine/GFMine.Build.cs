using UnrealBuildTool;

public class GFMine : ModuleRules
{
	public GFMine(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "GFCore", "GFNet" });
	}
}
