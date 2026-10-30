using UnrealBuildTool;

public class GFGiant : ModuleRules
{
	public GFGiant(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "GFCore", "GFNet" });
	}
}
