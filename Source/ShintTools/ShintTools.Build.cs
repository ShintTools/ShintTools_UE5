// Copyright 2026 ShintTools. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ShintTools : ModuleRules
{
	public ShintTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Public"),
			Path.Combine(ModuleDirectory, "Public", "Api"),
			Path.Combine(ModuleDirectory, "Public", "Core"),
			Path.Combine(ModuleDirectory, "Public", "Marketplace"),
			Path.Combine(ModuleDirectory, "Public", "Security"),
			Path.Combine(ModuleDirectory, "Public", "Transport"),
			Path.Combine(ModuleDirectory, "Public", "UI"),
			Path.Combine(ModuleDirectory, "Public", "UI", "Shared"),
			Path.Combine(ModuleDirectory, "Public", "Utils"),
		});

		PublicDefinitions.Add("SHINT_FREE_TIER=0");

		PublicDefinitions.Add("SHINT_MARKETPLACE_BUILD=0");

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{

			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",

			"RenderCore",
			"PhysicsCore",
			"MaterialEditor",
			"MeshDescription",
			"StaticMeshDescription",

			"StaticMeshEditor",

			"EditorWidgets",
			"UnrealEd",
			"LevelEditor",
			"ToolMenus",
			"WorkspaceMenuStructure",

			"BlueprintGraph",
			"KismetCompiler",

			"AssetTools",
			"AssetRegistry",

			"ContentBrowser",

			"Projects",

			"HTTP",
			"Json",
			"JsonUtilities",

			"InputCore",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"MainFrame",
				"Kismet",
			});
		}
	}
}
