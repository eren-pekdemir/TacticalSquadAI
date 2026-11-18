using UnrealBuildTool;

public class TacticalSquadAI : ModuleRules
{
	public TacticalSquadAI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",
			"SquadAI"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Niagara"
		});

		PublicIncludePaths.Add(ModuleDirectory + "/Public");
	}
}
