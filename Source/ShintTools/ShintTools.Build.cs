// Copyright 2026 ShintTools. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ShintTools : ModuleRules
{
	public ShintTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Fab compliance: standard UE module layout — ALL headers live under
		// Public/, ALL .cpp under Private/. UBT auto-adds only the Public ROOT
		// (not its subdirectories) for non-legacy modules, so the bare-name
		// includes used across this module ("ShintCoreClient.h",
		// "CoreProcessManager.h", "SShintToolsPanel.h", ...) need every Public
		// subfolder on the include path. Paths are built from ModuleDirectory
		// (absolute) so resolution does not depend on the include-root base.
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

		// SHINT_FREE_TIER=1 builds the lockdown variant produced by
		// tools/minify_plugin.py for distribution to Free-tier users.
		// minify_plugin.py edits this same file to flip the define on,
		// commits the stripped tree under payload/ShintTools_UE5_Stripped,
		// and reverts it for the paid bundle. Default (here) is 0 so
		// developer / Indie / Studio builds keep the relaxed defaults.
		PublicDefinitions.Add("SHINT_FREE_TIER=0");

		// SHINT_MARKETPLACE_BUILD=1 enables the standalone Core install
		// wizard (Docker pull + container) at module startup. This is the
		// `develop-marketplace` branch — the Fab SOURCE submission, which
		// Fab compiles as-is, so the define is baked to 1 here (there is no
		// launcher to install the Core for marketplace customers). On the
		// paid `develop`/`main` branches this stays 0 (installer.py owns
		// Core install).
		PublicDefinitions.Add("SHINT_MARKETPLACE_BUILD=1");

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
