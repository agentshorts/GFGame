using UnrealBuildTool;

public class GFLab : ModuleRules
{
	public GFLab(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "GFCore", "GFNet", "GFBuild", "GameplayTags" });
	}
}
