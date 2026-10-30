using UnrealBuildTool;

public class GFRhythm : ModuleRules
{
	public GFRhythm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GFCore", "GFNet", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("ole32.lib");
		}
	}
}
