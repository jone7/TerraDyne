// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "Settings/TerraDyneSettings.h"
#include "World/TerraDyneChunk.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneGetChunksInRadiusTest,
    "TerraDyne.Undo.GetChunksInRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneGetChunksInRadiusTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World exists"), World);
    if (!World) return false;

    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    TestNotNull(TEXT("Manager spawned"), Manager);
    if (!Manager) return false;

    // Spawn a chunk and register it via RebuildChunkMap
    ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
    TestNotNull(TEXT("Chunk spawned"), Chunk);
    if (!Chunk) return false;

    Chunk->GridCoordinate = FIntPoint(0, 0);
    Chunk->WorldSize = 10000.f;
    Manager->RebuildChunkMap();

    // A radius of 1000 around origin should find the chunk at grid (0,0)
    TArray<ATerraDyneChunk*> Found = Manager->GetChunksInRadius(FVector::ZeroVector, 1000.f);
    TestTrue(TEXT("GetChunksInRadius finds chunk at origin"), Found.Contains(Chunk));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneUndoHistoryTrimTest,
    "TerraDyne.Undo.HistoryTrim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneUndoHistoryTrimTest::RunTest(const FString& Parameters)
{
    // Temporarily lower the undo history limit
    UTerraDyneSettings* Settings = GetMutableDefault<UTerraDyneSettings>();
    int32 OldMax = Settings->MaxUndoHistory;
    Settings->MaxUndoHistory = 3;

    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World exists"), World);
    if (!World)
    {
        Settings->MaxUndoHistory = OldMax;
        return false;
    }

    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    TestNotNull(TEXT("Manager spawned"), Manager);
    if (!Manager)
    {
        Settings->MaxUndoHistory = OldMax;
        return false;
    }

    APlayerController* PC = World->SpawnActor<APlayerController>();
    TestNotNull(TEXT("PlayerController spawned"), PC);
    if (!PC)
    {
        Settings->MaxUndoHistory = OldMax;
        return false;
    }

    // Commit 5 strokes with no actual chunks (radius 0 at origin, no chunks registered)
    for (int32 i = 0; i < 5; i++)
    {
        Manager->BeginStroke(FVector::ZeroVector, 0.f, PC);
        Manager->CommitStroke(PC);
    }

    TestEqual(TEXT("History trimmed to MaxUndoHistory=3"), Manager->GetUndoDepth(PC), 3);
    TestEqual(TEXT("Redo stack empty after new strokes"), Manager->GetRedoDepth(PC), 0);

    Settings->MaxUndoHistory = OldMax;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneUndoRoundTripTest,
    "TerraDyne.Undo.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneUndoRoundTripTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World exists"), World);
    if (!World) return false;

    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    TestNotNull(TEXT("Manager spawned"), Manager);
    if (!Manager) return false;

    APlayerController* PC = World->SpawnActor<APlayerController>();
    TestNotNull(TEXT("PlayerController spawned"), PC);
    if (!PC) return false;

    // Spawn chunk at origin and register it via RebuildChunkMap
    ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
    TestNotNull(TEXT("Chunk spawned"), Chunk);
    if (!Chunk) return false;

    Chunk->GridCoordinate = FIntPoint(0, 0);
    Chunk->WorldSize = 10000.f;

    // Initialize SculptBuffer to 10 zeros
    Chunk->SculptBuffer.Init(0.f, 10);
    Chunk->BaseBuffer.Init(0.f, 10);
    Chunk->DetailBuffer.Init(0.f, 10);
    Chunk->HeightBuffer.Init(0.f, 10);
    for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
        Chunk->WeightBuffers[L].Init(0.f, 10);

    // Register chunk in manager's ActiveChunkMap via RebuildChunkMap
    Manager->RebuildChunkMap();

    // Confirm the chunk is reachable through the manager before proceeding
    TestNotNull(TEXT("Chunk reachable via GetChunkAtCoord"), Manager->GetChunkAtCoord(FIntPoint(0, 0)));

    // BeginStroke snapshots current state (SculptBuffer[0] == 0.0f)
    Manager->BeginStroke(FVector::ZeroVector, 0.f, PC);
    TestTrue(TEXT("Stroke is pending after BeginStroke"), Manager->HasPendingStroke(PC));

    // Simulate a stroke — modify the chunk after the snapshot is taken
    Chunk->SculptBuffer[0] = 0.5f;

    // CommitStroke pushes the pre-stroke snapshot onto the undo stack
    Manager->CommitStroke(PC);

    TestEqual(TEXT("UndoDepth == 1 after CommitStroke"), Manager->GetUndoDepth(PC), 1);
    TestEqual(TEXT("RedoDepth == 0 before Undo"), Manager->GetRedoDepth(PC), 0);

    // Undo: should restore SculptBuffer[0] to 0.0f and push current state to redo
    Manager->Undo(PC);

    TestEqual(TEXT("SculptBuffer[0] restored to 0.0f after Undo"), Chunk->SculptBuffer[0], 0.f);
    TestEqual(TEXT("UndoDepth == 0 after Undo"), Manager->GetUndoDepth(PC), 0);
    TestEqual(TEXT("RedoDepth == 1 after Undo"), Manager->GetRedoDepth(PC), 1);

    // Redo: should restore SculptBuffer[0] to 0.5f and push current state back to undo
    Manager->Redo(PC);

    TestEqual(TEXT("SculptBuffer[0] restored to 0.5f after Redo"), Chunk->SculptBuffer[0], 0.5f);
    TestEqual(TEXT("UndoDepth == 1 after Redo"), Manager->GetUndoDepth(PC), 1);
    TestEqual(TEXT("RedoDepth == 0 after Redo"), Manager->GetRedoDepth(PC), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneBrushUsesExplicitTargetLayerTest,
    "TerraDyne.Undo.ExplicitTargetLayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneBrushUsesExplicitTargetLayerTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World exists"), World);
    if (!World) return false;

    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    TestNotNull(TEXT("Manager spawned"), Manager);
    if (!Manager) return false;

    ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
    TestNotNull(TEXT("Chunk spawned"), Chunk);
    if (!Chunk) return false;

    Chunk->GridCoordinate = FIntPoint(0, 0);
    Chunk->InitializeChunk(FIntPoint(0, 0), 1000.f, 32, nullptr, nullptr);
    Manager->GlobalChunkSize = 1000.f;
    Manager->ActiveLayer = ETerraDyneLayer::Sculpt;
    Manager->RebuildChunkMap();

    const int32 NumSamples = Chunk->Resolution * Chunk->Resolution;
    Chunk->BaseBuffer.Init(0.0f, NumSamples);
    Chunk->SculptBuffer.Init(0.0f, NumSamples);
    Chunk->DetailBuffer.Init(0.0f, NumSamples);
    Chunk->HeightBuffer.Init(0.0f, NumSamples);

    Manager->ApplyGlobalBrush(
        FVector::ZeroVector,
        250.0f,
        500.0f,
        ETerraDyneBrushMode::Raise,
        ETerraDyneLayer::Base);

    bool bBaseChanged = false;
    bool bSculptChanged = false;
    for (int32 Index = 0; Index < NumSamples; ++Index)
    {
        bBaseChanged |= !FMath::IsNearlyZero(Chunk->BaseBuffer[Index]);
        bSculptChanged |= !FMath::IsNearlyZero(Chunk->SculptBuffer[Index]);
    }

    TestTrue(TEXT("Explicit base-layer brush modifies the base buffer"), bBaseChanged);
    TestFalse(TEXT("Explicit base-layer brush does not leak into the sculpt buffer"), bSculptChanged);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneUndoRestoresAllHeightLayersTest,
    "TerraDyne.Undo.RestoresAllHeightLayers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneUndoRestoresAllHeightLayersTest::RunTest(const FString& Parameters)
{
    UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
    TestNotNull(TEXT("World exists"), World);
    if (!World) return false;

    ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
    TestNotNull(TEXT("Manager spawned"), Manager);
    if (!Manager) return false;

    APlayerController* PC = World->SpawnActor<APlayerController>();
    TestNotNull(TEXT("PlayerController spawned"), PC);
    if (!PC) return false;

    ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
    TestNotNull(TEXT("Chunk spawned"), Chunk);
    if (!Chunk) return false;

    Chunk->GridCoordinate = FIntPoint(0, 0);
    Chunk->InitializeChunk(FIntPoint(0, 0), 1000.f, 32, nullptr, nullptr);
    Manager->GlobalChunkSize = 1000.f;
    Manager->RebuildChunkMap();

    const int32 NumSamples = Chunk->Resolution * Chunk->Resolution;
    Chunk->BaseBuffer.Init(0.0f, NumSamples);
    Chunk->SculptBuffer.Init(0.0f, NumSamples);
    Chunk->DetailBuffer.Init(0.0f, NumSamples);
    Chunk->HeightBuffer.Init(0.0f, NumSamples);

    Manager->BeginStroke(FVector::ZeroVector, 0.f, PC);

    Chunk->BaseBuffer[0] = 0.25f;
    Chunk->SculptBuffer[0] = 0.5f;
    Chunk->DetailBuffer[0] = -0.1f;
    Chunk->HeightBuffer[0] = 0.65f;

    Manager->CommitStroke(PC);
    Manager->Undo(PC);

    TestEqual(TEXT("Undo restores base layer"), Chunk->BaseBuffer[0], 0.0f);
    TestEqual(TEXT("Undo restores sculpt layer"), Chunk->SculptBuffer[0], 0.0f);
    TestEqual(TEXT("Undo restores detail layer"), Chunk->DetailBuffer[0], 0.0f);

    Manager->Redo(PC);

    TestEqual(TEXT("Redo restores base layer"), Chunk->BaseBuffer[0], 0.25f);
    TestEqual(TEXT("Redo restores sculpt layer"), Chunk->SculptBuffer[0], 0.5f);
    TestEqual(TEXT("Redo restores detail layer"), Chunk->DetailBuffer[0], -0.1f);

    return true;
}

#endif // WITH_EDITOR
