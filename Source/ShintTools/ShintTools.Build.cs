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

		// 0 = full build (developer / Indie / Studio). A separate build
		// sets this to 1 to compile the reduced Free-tier variant.
		PublicDefinitions.Add("SHINT_FREE_TIER=0");

		// 0 = full paid/launcher build: the launcher's installer owns Core, and
		// the generic tier welcome is used. The Fab source pack and the
		// launcher's marketplace binary build flip this to 1 to compile the
		// standalone variant (in-editor Core install wizard + "get the launcher"
		// promo). This MUST stay 0 in committed source: paid installs pull @main
		// and compile it directly, so a committed 1 makes every paid install run
		// the marketplace path (Fab welcome + Core wizard). Regressed to 1 in
		// 9ae64c5 while chasing a green UE 5.7 build — that is the paid
		// welcome-on-launcher-install bug; restored here.
		PublicDefinitions.Add("SHINT_MARKETPLACE_BUILD=0");

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

			// LOD Auditor Contract v2 collection (Studio tier):
			//   RenderCore            — FStaticMeshLODResources (per-LOD counts)
			//   PhysicsCore           — UBodySetup / FKAggregateGeom (LD011)
			//   MaterialEditor        — UMaterialEditingLibrary::GetStatistics
			//   MeshDescription +     — Deep Scan geometry integrity + normal
			//   StaticMeshDescription   stats (FMeshDescription attribute walks)
			"RenderCore",
			"PhysicsCore",
			"MaterialEditor",
			"MeshDescription",
			"StaticMeshDescription",
			// UStaticMeshEditorSubsystem — in-place LOD auto-fix structural ops
			// (generate/reduce LOD chain, screen sizes, simple collision).
			"StaticMeshEditor",

			// Editor modules. NOT "EditorStyle" — the whole UI already uses
			// FAppStyle (its replacement); EditorStyle is soft-deprecated
			// upstream (FEditorStyle::* calls warn) and an unused dependency
			// on it is pure risk for the 5.2-5.8 compatibility range this
			// plugin targets, with zero benefit since nothing here calls it.
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
			// Asset Naming Bot row click — IContentBrowserSingleton::SyncBrowserToAssets
			// (reveal/select, not open the asset editor).
			"ContentBrowser",

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
