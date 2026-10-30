using UnrealBuildTool;

public class GFCompanion : ModuleRules
{
	public GFCompanion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "GFCore", "GameplayTags" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
