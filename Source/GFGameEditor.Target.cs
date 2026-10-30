using UnrealBuildTool;

public class GFGameEditorTarget : TargetRules
{
	public GFGameEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "GFGame", "GFCore", "GFVoxel", "GFBuild", "GFGarden", "GFCompanion", "GFFashion", "GFMoba", "GFRhythm", "GFSave", "GFArena", "GFLab", "GFMine", "GFDepths", "GFGiant", "GFSocial", "GFTests" });
	}
}
