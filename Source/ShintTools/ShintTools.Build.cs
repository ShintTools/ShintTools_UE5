// Copyright 2026 ShintTools. All Rights Reserved.

using UnrealBuildTool;

public class ShintTools : ModuleRules
{
	public ShintTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Fab compliance: sources follow the standard UE module layout —
		// Public/ holds the exposed module header, Private/ holds all
		// internal implementation. The folder-qualified sibling includes
		// (e.g. "Transport/FShintHttpClient.h", "Security/ShintSecurity.h")
		// are preserved by re-rooting every include path under Private/.
		// "Public" is auto-added by UBT so the module header resolves as
		// "ShintTools.h". Without the Private subfolder roots, UAT
		// BuildPlugin fails with `C1083: Cannot open include file: 'Transport/…'`.
		PrivateIncludePaths.AddRange(new string[]
		{
			"ShintTools/Private",
			"ShintTools/Private/Api",
			"ShintTools/Private/Core",
			"ShintTools/Private/Marketplace",
			"ShintTools/Private/Security",
			"ShintTools/Private/Transport",
			"ShintTools/Private/UI",
			"ShintTools/Private/UI/Shared",
			"ShintTools/Private/Utils",
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
