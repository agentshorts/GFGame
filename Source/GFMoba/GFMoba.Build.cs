using UnrealBuildTool;

public class GFMoba : ModuleRules
{
	public GFMoba(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
