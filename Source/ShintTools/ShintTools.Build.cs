// Copyright ShintTools. All Rights Reserved.

using UnrealBuildTool;

public class ShintTools : ModuleRules
{
	public ShintTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(new string[]
		{
			"ShintTools/Core",
			"ShintTools/UI",
			"ShintTools/UI/Shared",
			"ShintTools/Utils",
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// UE Core
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",

			// Editor modules
			"EditorStyle",
			"EditorWidgets",
			"UnrealEd",
			"LevelEditor",
			"ToolMenus",
			"WorkspaceMenuStructure",

			// Blueprint reading (UK2Node, UEdGraph, FBPVariableDescription)
			"BlueprintGraph",
			"KismetCompiler",

			// Asset tools (IAssetTools::RenameAssets + AssetRegistry discovery)
			"AssetTools",
			"AssetRegistry",

			// Plugin manager (IPluginManager for config paths)
			"Projects",

			// HTTP & JSON
			"HTTP",
			"Json",
			"JsonUtilities",

			// Input
			"InputCore",

			// Blueprint graph introspection (K2Node types for variable/function analysis)
			"BlueprintGraph",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"MainFrame",
				"Kismet",      // Blueprint editor helpers (for reading BP data)
			});
		}
	}
}
