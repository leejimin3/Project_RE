// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Project_RE : ModuleRules
{
	public Project_RE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Project_RE",
			"Project_RE/Variant_Platforming",
			"Project_RE/Variant_Platforming/Animation",
			"Project_RE/Variant_Combat",
			"Project_RE/Variant_Combat/AI",
			"Project_RE/Variant_Combat/Animation",
			"Project_RE/Variant_Combat/Gameplay",
			"Project_RE/Variant_Combat/Interfaces",
			"Project_RE/Variant_Combat/UI",
			"Project_RE/Variant_SideScrolling",
			"Project_RE/Variant_SideScrolling/AI",
			"Project_RE/Variant_SideScrolling/Gameplay",
			"Project_RE/Variant_SideScrolling/Interfaces",
			"Project_RE/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
