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

			// Asset management — required for IAssetTools::RenameAssets
			"AssetTools",
			"AssetRegistry",

			// Plugin info — required for IPluginManager (banner image loading)
			"Projects",

			// HTTP & JSON
			"HTTP",
			"Json",
			"JsonUtilities",

			// Input/Output utilities
			"InputCore",

			// Blueprint graph introspection (K2Node types for variable/function analysis)
			"BlueprintGraph",
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"MainFrame",
			});
		}
	}
}
