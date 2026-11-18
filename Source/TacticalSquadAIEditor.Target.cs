using UnrealBuildTool;

public class TacticalSquadAIEditorTarget : TargetRules
{
	public TacticalSquadAIEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("TacticalSquadAI");
	}
}
