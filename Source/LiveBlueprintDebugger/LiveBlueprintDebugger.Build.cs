// Copyright (c) 2022-2023 Justin Nordin. All Rights Reserved.

using UnrealBuildTool;

public class LiveBlueprintDebugger : ModuleRules
{
	public LiveBlueprintDebugger(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
                "Editor/PropertyEditor",
			}
			);

		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Projects",
				"InputCore",
				"EditorFramework",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
                "DetailCustomizations",
				"DeveloperSettings",
				"EditorWidgets",
				"Kismet",
				"KismetWidgets",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
                "PropertyEditor"
			}
			);
	}
}
