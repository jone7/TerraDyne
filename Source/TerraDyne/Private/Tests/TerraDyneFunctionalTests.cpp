// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "World/TerraDyneSceneSetup.h"
#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshQueryFunctions.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

namespace TerraDyneFunctionalTests
{
    class FWaitForTerrainMeshCommand final : public IAutomationLatentCommand
    {
    public:
        FWaitForTerrainMeshCommand(FAutomationTestBase* InTest, ATerraDyneChunk* InChunk)
            : Test(InTest)
            , Chunk(InChunk)
            , DeadlineSeconds(FPlatformTime::Seconds() + 10.0)
        {
        }

        virtual bool Update() override
        {
            ATerraDyneChunk* ChunkPtr = Chunk.Get();
            if (!ChunkPtr || !ChunkPtr->DynamicMeshComp)
            {
                Test->AddError(TEXT("Chunk was destroyed before its asynchronous terrain mesh completed."));
                return true;
            }

            // Editor automation worlds are not guaranteed to tick actors. Drive the production
            // async-result application path explicitly while the worker finishes.
            static_cast<AActor*>(ChunkPtr)->Tick(0.05f);
            if (ChunkPtr->DynamicMeshComp->GetDynamicMesh()->GetTriangleCount() > 0)
            {
                Test->TestTrue(
                    TEXT("Collision should remain enabled after the terrain mesh is applied"),
                    ChunkPtr->DynamicMeshComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
                return true;
            }

            if (FPlatformTime::Seconds() < DeadlineSeconds)
            {
                return false;
            }

            Test->AddError(TEXT("Timed out waiting for the asynchronous terrain mesh build."));
            return true;
        }

    private:
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<ATerraDyneChunk> Chunk;
        double DeadlineSeconds = 0.0;
    };
}

// USP 1 & 3: Deformation & Physics Update
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneDeformationTest, "TerraDyne.Functional.Deformation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneDeformationTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull("World should exist", World);

    ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
    TestNotNull("Chunk should spawn", Chunk);

    if (Chunk)
    {
        // 1. Initialize
        Chunk->InitializeChunk(FIntPoint(0,0), 1000.0f, 32, nullptr, nullptr);
        Chunk->RebuildPhysicsMesh(); // Ensure mesh exists

        TestNotNull("DynamicMeshComp should exist", Chunk->DynamicMeshComp.Get());
        
        FVector EditPos(500.0f, 500.0f, 0.0f);
        const float InitialHeight = Chunk->GetHeightAtLocation(EditPos);

        // 2. Deform
        // Apply a strong brush to ensure change
        Chunk->ApplyLocalIdempotentEdit(EditPos, 200.0f, 500.0f, ETerraDyneBrushMode::Raise);

        const float UpdatedHeight = Chunk->GetHeightAtLocation(EditPos);

        // 3. Verify Change (USP 1)
        TestTrue("Height query should change after deformation", !FMath::IsNearlyEqual(InitialHeight, UpdatedHeight));
        ADD_LATENT_AUTOMATION_COMMAND(TerraDyneFunctionalTests::FWaitForTerrainMeshCommand(this, Chunk));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneCPUBrushStateSyncTest, "TerraDyne.Functional.CPUBrushStateSync", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneCPUBrushStateSyncTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull("World should exist", World);
    if (!World) return false;

    ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
    TestNotNull("Chunk should spawn", Chunk);
    if (!Chunk) return false;

    Chunk->GridCoordinate = FIntPoint::ZeroValue;
    Chunk->ChunkSizeWorldUnits = 2048.0f;
    Chunk->WorldSize = 2048.0f;
    Chunk->ZScale = 768.0f;
    Chunk->InitializeChunk(FIntPoint::ZeroValue, 2048.0f, 32, nullptr);

    const bool bRenderResourcesExpected = FApp::CanEverRender() && !IsRunningDedicatedServer();
    if (bRenderResourcesExpected)
    {
        TestNotNull("Height RT should be created on a render-capable runtime", Chunk->HeightRT.Get());
    }
    else
    {
        TestNull("Headless runtime should not allocate a height RT", Chunk->HeightRT.Get());
    }

    UTextureRenderTarget2D* PreviousHeightRT = Chunk->HeightRT.Get();
    const FVector EditPos(0.0f, 0.0f, 0.0f);
    const float InitialHeight = Chunk->GetHeightAtLocation(EditPos);
    const int32 CenterIndex = (Chunk->Resolution / 2) * Chunk->Resolution + (Chunk->Resolution / 2);
    const float InitialSample = Chunk->HeightBuffer.IsValidIndex(CenterIndex) ? Chunk->HeightBuffer[CenterIndex] : 0.0f;

    Chunk->ApplyLocalIdempotentEdit(EditPos, 300.0f, 500.0f, ETerraDyneBrushMode::Raise);

    const float UpdatedHeight = Chunk->GetHeightAtLocation(EditPos);
    TestTrue("CPU brush should update CPU height queries immediately", !FMath::IsNearlyEqual(InitialHeight, UpdatedHeight));
    TestTrue("CPU brush should update the CPU height buffer immediately",
        Chunk->HeightBuffer.IsValidIndex(CenterIndex) && !FMath::IsNearlyEqual(InitialSample, Chunk->HeightBuffer[CenterIndex]));
    // CPU 路径必须被显式选中，显示资源不能伪装成 GPU 计算能力。
    TestFalse("CPU-only adaptation must not advertise GPU brushes", Chunk->IsUsingGPU());
    TestEqual("CPU edit retains its display target", Chunk->HeightRT.Get(), PreviousHeightRT);

    const FTerraDyneChunkData SerializedData = Chunk->GetSerializedData();
    TestTrue("Serialized height data should match the CPU height buffer after a CPU brush",
        SerializedData.HeightData.IsValidIndex(CenterIndex) &&
        Chunk->HeightBuffer.IsValidIndex(CenterIndex) &&
        FMath::IsNearlyEqual(SerializedData.HeightData[CenterIndex], Chunk->HeightBuffer[CenterIndex], KINDA_SMALL_NUMBER));

    return true;
}

// USP 4: Infinite World / Grid Logic
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneGridTest, "TerraDyne.Functional.InfiniteGrid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneGridTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    Manager->GlobalChunkSize = 1000.0f; // Set size

    // Manually spawn chunks to simulate the grid
    ATerraDyneChunk* ChunkA = World->SpawnActor<ATerraDyneChunk>();
    ChunkA->GridCoordinate = FIntPoint(0, 0);
    
    ATerraDyneChunk* ChunkB = World->SpawnActor<ATerraDyneChunk>();
    ChunkB->GridCoordinate = FIntPoint(-1, -1); // Negative Quadrant

    Manager->RebuildChunkMap(); // This is the function we are testing

    // Test Positive Lookup
    ATerraDyneChunk* FoundA = Manager->GetChunkAtLocation(FVector(100, 100, 0));
    TestEqual("Should find Chunk A at (0,0)", FoundA, ChunkA);

    // Test Negative Lookup (USP 4)
    // Centered chunk math means chunk [0,0] spans [-500, 500] for a 1000-unit chunk.
    ATerraDyneChunk* FoundB = Manager->GetChunkAtLocation(FVector(-600, -600, 0));
    TestEqual("Should find Chunk B at (-1,-1)", FoundB, ChunkB);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneGlobalNoiseTest, "TerraDyne.Functional.GlobalNoise", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneGlobalNoiseTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull("World should exist", World);
    if (!World) return false;

    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    TestNotNull("Manager should spawn", Manager);
    if (!Manager) return false;

    Manager->GlobalChunkSize = 1000.0f;
    Manager->ActiveLayer = ETerraDyneLayer::Detail;

    ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
    TestNotNull("Chunk should spawn", Chunk);
    if (!Chunk) return false;

    Chunk->GridCoordinate = FIntPoint(0, 0);
    Chunk->InitializeChunk(FIntPoint(0, 0), 1000.0f, 32, nullptr, nullptr);

    const int32 NumSamples = Chunk->Resolution * Chunk->Resolution;
    Chunk->BaseBuffer.Init(0.5f, NumSamples);
    Chunk->SculptBuffer.Init(0.0f, NumSamples);
    Chunk->DetailBuffer.Init(0.0f, NumSamples);
    Chunk->HeightBuffer.Init(0.5f, NumSamples);
    Chunk->RebuildPhysicsMesh();

    Manager->RebuildChunkMap();
    Manager->ApplyGlobalNoise(120.0f, 0.003f, 42.0f);

    bool bDetailChanged = false;
    bool bHeightChanged = false;
    for (int32 Index = 0; Index < NumSamples; Index++)
    {
        bDetailChanged |= !FMath::IsNearlyZero(Chunk->DetailBuffer[Index]);
        bHeightChanged |= !FMath::IsNearlyEqual(Chunk->HeightBuffer[Index], 0.5f);
    }

    TestTrue("ApplyGlobalNoise modifies the active detail layer", bDetailChanged);
    TestTrue("ApplyGlobalNoise recomputes the combined height buffer", bHeightChanged);

    return true;
}

// USP 5: Zero Config
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneSetupTest, "TerraDyne.Functional.ZeroConfig", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneSetupTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    Manager->bSpawnDefaultChunksOnBeginPlay = true;
    
    // Trigger the explicit sandbox bootstrap path.
    Manager->DispatchBeginPlay();

    // Verify Chunks Spawned
    TArray<AActor*> Chunks;
    UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneChunk::StaticClass(), Chunks);
    
    // Should spawn 3x3 grid = 9 chunks
    TestTrue("Should spawn default sandbox chunks", Chunks.Num() >= 9);

    // Verify Stats (USP 5 part 2)
    FTerraDyneGPUStats Stats = Manager->GetGPUStats();
    TestTrue("Should detect GPU or Software", !Stats.ComputeBackend.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneSceneSetupRuntimeInitTest, "TerraDyne.Functional.SceneSetupRuntimeInit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneSceneSetupRuntimeInitTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull("World should exist", World);
    if (!World) return false;

    ATerraDyneSceneSetup* SceneSetup = World->SpawnActor<ATerraDyneSceneSetup>();
    TestNotNull("Scene setup should spawn", SceneSetup);
    if (!SceneSetup) return false;

    SceneSetup->DemoTemplate = ETerraDyneDemoTemplate::SurvivalFramework;
    SceneSetup->InitializeWorld();

    TArray<AActor*> Managers;
    UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneManager::StaticClass(), Managers);
    TestEqual("Scene setup should spawn one manager", Managers.Num(), 1);

    TArray<AActor*> Orchestrators;
    UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneOrchestrator::StaticClass(), Orchestrators);
    TestEqual("Scene setup should spawn one orchestrator", Orchestrators.Num(), 1);

    TArray<AActor*> DirectionalLights;
    UGameplayStatics::GetAllActorsOfClass(World, ADirectionalLight::StaticClass(), DirectionalLights);
    TestTrue("Scene setup should ensure at least one directional light", DirectionalLights.Num() >= 1);

    TArray<AActor*> SkyLights;
    UGameplayStatics::GetAllActorsOfClass(World, ASkyLight::StaticClass(), SkyLights);
    TestTrue("Scene setup should ensure at least one skylight", SkyLights.Num() >= 1);

    if (Managers.Num() == 1)
    {
        ATerraDyneManager* Manager = Cast<ATerraDyneManager>(Managers[0]);
        TestNotNull("Spawned manager should be valid", Manager);
        if (Manager)
        {
            TestTrue("Survival template should enable default chunk spawn on begin play", Manager->bSpawnDefaultChunksOnBeginPlay);
            TestTrue("Survival template should enable default lighting on begin play", Manager->bSetupDefaultLightingOnBeginPlay);
            TestFalse("Survival template should not auto-spawn showcase", Manager->bSpawnShowcaseOnBeginPlay);
        }
    }

    return true;
}
#endif
