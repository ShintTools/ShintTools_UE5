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
			"ShintTools/Security",
			"ShintTools/Transport",
			"ShintTools/UI",
			"ShintTools/UI/Shared",
			"ShintTools/Utils",
		});

		// SHINT_FREE_TIER=1 builds the lockdown variant produced by
		// tools/minify_plugin.py for distribution to Free-tier users.
		// minify_plugin.py edits this same file to flip the define on,
		// commits the stripped tree under payload/ShintTools_UE5_Stripped,
		// and reverts it for the paid bundle. Default (here) is 0 so
		// developer / Indie / Studio builds keep the relaxed defaults.
		PublicDefinitions.Add("SHINT_FREE_TIER=0");

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
