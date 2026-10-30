using UnrealBuildTool;

public class GFSocial : ModuleRules
{
	public GFSocial(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "NetCore", "GFCore", "GFNet" });
	}
}
