using UnrealBuildTool;

public class GFCore : ModuleRules
{
	public GFCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "DeveloperSettings", "NetCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
