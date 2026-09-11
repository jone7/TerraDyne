// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneWorldPreset.h"
#include "Examples/TerraDyneIntegrationExamples.h"
#include "Grass/TerraDyneGrassTypes.h"
#include "World/TerraDyneSceneSetup.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneLoadTest, "TerraDyne.Basic.ModuleLoad", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneLoadTest::RunTest(const FString& Parameters)
{
    // Test 1: Check if Module is Loaded
    bool bIsLoaded = FModuleManager::Get().IsModuleLoaded("TerraDyne");
    TestTrue("TerraDyne module should be loaded", bIsLoaded);

    // Test 2: Required plugin shader payload should be staged with the plugin.
    const TSharedPtr<IPlugin> TerraDynePlugin = IPluginManager::Get().FindPlugin(TEXT("TerraDyne"));
    TestTrue("TerraDyne plugin descriptor should be discoverable", TerraDynePlugin.IsValid());
    if (TerraDynePlugin.IsValid())
    {
        const FString SimulationShaderPath = FPaths::Combine(
            TerraDynePlugin->GetBaseDir(),
            TEXT("Shaders"),
            TEXT("TerraDyneSimulation.usf"));
        TestTrue(
            FString::Printf(TEXT("Required shader file should exist: %s"), *SimulationShaderPath),
            FPaths::FileExists(SimulationShaderPath));
    }

    // Test 3: Check Class Validity
    UClass* ManagerClass = ATerraDyneManager::StaticClass();
    TestNotNull("ATerraDyneManager class should be valid", ManagerClass);

    if (ManagerClass)
    {
        ATerraDyneManager* DefaultObject = Cast<ATerraDyneManager>(ManagerClass->GetDefaultObject());
        TestNotNull("Default Object should exist", DefaultObject);
        
        // Test 4: Check Default Values
        TestTrue("Default GlobalChunkSize should be 0 or initialized", DefaultObject->GlobalChunkSize >= 0.0f);
        TestFalse("bAutoImportAtRuntime should be opt-in", DefaultObject->bAutoImportAtRuntime);
        TestFalse("Demo chunk bootstrap should be opt-in", DefaultObject->bSpawnDefaultChunksOnBeginPlay);
    }

    UClass* SceneSetupClass = ATerraDyneSceneSetup::StaticClass();
    TestNotNull("ATerraDyneSceneSetup class should be valid", SceneSetupClass);
    if (SceneSetupClass)
    {
        const ATerraDyneSceneSetup* DefaultSetup = Cast<ATerraDyneSceneSetup>(SceneSetupClass->GetDefaultObject());
        TestNotNull("Scene setup default object should exist", DefaultSetup);
        if (DefaultSetup)
        {
            TestEqual(
                "First-run scene setup should default to Survival Framework",
                DefaultSetup->DemoTemplate,
                ETerraDyneDemoTemplate::SurvivalFramework);
        }
    }

    TestNotNull(
        "Survival integration example class should be valid",
        ATerraDyneSurvivalIntegrationExample::StaticClass());
    TestNotNull(
        "PCG integration example class should be valid",
        ATerraDynePCGIntegrationExample::StaticClass());
    TestNotNull(
        "Replication integration example class should be valid",
        ATerraDyneReplicationIntegrationExample::StaticClass());
    TestNotNull(
        "Save/load integration example class should be valid",
        ATerraDyneSaveLoadIntegrationExample::StaticClass());
    TestNotNull(
        "Biome reactor integration example class should be valid",
        ATerraDyneBiomeReactorIntegrationExample::StaticClass());

    TestNotNull(
        "Packaged sample world preset should load",
        LoadObject<UTerraDyneWorldPreset>(
            nullptr,
            TEXT("/TerraDyne/Samples/Presets/DA_TerraDyne_ShowcaseWorld.DA_TerraDyne_ShowcaseWorld")));
    TestNotNull(
        "Packaged sample grass profile should load",
        LoadObject<UTerraDyneGrassProfile>(
            nullptr,
            TEXT("/TerraDyne/Samples/Profiles/DA_TerraDyne_ShowcaseGrass.DA_TerraDyne_ShowcaseGrass")));

    return true;
}
