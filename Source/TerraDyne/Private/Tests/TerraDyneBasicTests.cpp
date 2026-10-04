// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneWorldPreset.h"
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

    // 开源 CPU 路径仍要求真实插件描述可发现。
    TestTrue("TerraDyne plugin descriptor should be discoverable", IPluginManager::Get().FindPlugin(TEXT("TerraDyne")).IsValid());

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

    // 上游未提供商业 Examples、Samples 和 GPU Shader，不能作为本开源构建的验收条件。
    return true;
}
