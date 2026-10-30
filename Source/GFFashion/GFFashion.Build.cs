using UnrealBuildTool;

public class GFFashion : ModuleRules
{
	public GFFashion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });

	}
}
