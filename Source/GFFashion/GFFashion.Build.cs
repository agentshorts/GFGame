using UnrealBuildTool;

public class GFFashion : ModuleRules
{
	public GFFashion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "GFNet" });
		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}
