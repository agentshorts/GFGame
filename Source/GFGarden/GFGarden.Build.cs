using UnrealBuildTool;

public class GFGarden : ModuleRules
{
	public GFGarden(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
