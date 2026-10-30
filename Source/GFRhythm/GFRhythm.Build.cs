using UnrealBuildTool;

public class GFRhythm : ModuleRules
{
	public GFRhythm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new string[] {  });
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("ole32.lib");
		}
	}
}
