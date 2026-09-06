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
			// AIModule: REPlayerController 가 UAIBlueprintHelperLibrary::SimpleMoveToLocation 을 쓴다.
			// 이름만 보고 지우면 우클릭 이동이 죽는다 — Variant_* 와 함께 지울 항목이 아니다.
			"AIModule",
			"UMG",
			"Slate",
			"SlateCore",
			"MassEntity",
			"MassCore",
			"GameplayAbilities",
			"Niagara",
			"GameplayTags",
			"GameplayTasks",
			"NavigationSystem",
			"DeveloperSettings"
		});

		PublicIncludePaths.AddRange(new string[] {
			"Project_RE",
			"Project_RE/Abilities",
			"Project_RE/Baseline",
			"Project_RE/Core",
			"Project_RE/Mass",
			"Project_RE/UI"
		});
	}
}
