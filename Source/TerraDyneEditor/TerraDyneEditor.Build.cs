// Copyright (c) 2026 GregOrigin. All Rights Reserved.
using UnrealBuildTool;

public class TerraDyneEditor : ModuleRules
{
	public TerraDyneEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore",
			"TerraDyne",
			"GeometryCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"UnrealEd",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"EditorFramework",
			"AssetTools",
			"AssetRegistry",
			"PropertyEditor",
			"LevelEditor",
			"ToolMenus",
			"Projects", // Required for IPluginManager
			"Landscape", // Required to read source Landscape data
			"Foliage", // Required by LandscapeEdit.h (InstancedFoliageActor)
			"RenderCore", // Required for Texture locking/baking
			"RHI"
		});
	}
}
