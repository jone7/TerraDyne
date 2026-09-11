// Copyright (c) 2026 GregOrigin. All Rights Reserved.
using UnrealBuildTool;

public class TerraDyne : ModuleRules
{
	public TerraDyne(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;


		// Core dependencies
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Landscape",
			"RenderCore",
			"RHI",
			"GeometryCore",
			"UMG",
			"Projects"
		});

		// Plugin dependencies (Must be enabled in .uproject)
		PublicDependencyModuleNames.AddRange(new string[] {
			"GeometryFramework"
		});

		// Private dependencies
		PrivateDependencyModuleNames.AddRange(new string[] {
			"GeometryScriptingCore",
			"Slate",
            "SlateCore",
            "DeveloperSettings",
            "ApplicationCore",
            "NavigationSystem",
            "MeshDescription",
            "StaticMeshDescription"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
			PrivateDependencyModuleNames.Add("Foliage");
		}
	}
}
