using UnrealBuildTool;

public class GFMagic : ModuleRules
{
	public GFMagic(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "NetCore", "Niagara",
			"GameplayAbilities", "GameplayTags", "GameplayTasks",
			"GFCore", "GFVoxel", "GFNet"
		});
	}
}
