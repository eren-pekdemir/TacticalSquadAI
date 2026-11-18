using UnrealBuildTool;

public class TacticalSquadAITarget : TargetRules
{
	public TacticalSquadAITarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("TacticalSquadAI");
	}
}
