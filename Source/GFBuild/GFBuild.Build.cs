using UnrealBuildTool;

public class GFBuild : ModuleRules
{
	public GFBuild(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "GameplayTags" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
