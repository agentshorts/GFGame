using UnrealBuildTool;

public class GFArena : ModuleRules
{
	public GFArena(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "GFCore", "GFCompanion", "GFNet" });
	}
}
