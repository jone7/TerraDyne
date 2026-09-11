// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneTileData.h"
#include "Core/TerraDyneSaveGame.h"
#include "Grass/TerraDyneGrassTypes.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

// -----------------------------------------------------------------------
// Paint: applying paint brush modifies the weight buffer
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDynePaintWeightTest,
	"TerraDyne.Paint.WeightBufferModified",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDynePaintWeightTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk) return false;

	Chunk->InitializeChunk(FIntPoint(0, 0), 1000.f, 32, nullptr, nullptr);

	// Verify all weight buffers start at zero
	for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
	{
		float Sum = 0.f;
		for (float V : Chunk->WeightBuffers[L]) Sum += V;
		TestEqual(FString::Printf(TEXT("Layer %d weight should start at zero"), L), Sum, 0.f);
	}

	// Apply paint brush at chunk centre to layer 1
	const int32 TargetLayer = 1;
	Chunk->ApplyLocalIdempotentEdit(FVector::ZeroVector, 300.f, 5000.f,
		ETerraDyneBrushMode::Paint, ETerraDyneLayer::Sculpt, TargetLayer);

	// Layer 1 must have some non-zero values; other layers must remain zero
	float SumLayer1 = 0.f;
	for (float V : Chunk->WeightBuffers[TargetLayer]) SumLayer1 += V;
	TestTrue("Paint layer 1 should have weight after brush", SumLayer1 > 0.f);

	for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
	{
		if (L == TargetLayer) continue;
		float S = 0.f;
		for (float V : Chunk->WeightBuffers[L]) S += V;
		TestEqual(FString::Printf(TEXT("Layer %d should be untouched"), L), S, 0.f);
	}

	return true;
}

// -----------------------------------------------------------------------
// Save / Load: weight data survives a round-trip through FTerraDyneChunkData
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneWeightRoundTripTest,
	"TerraDyne.Paint.WeightSaveLoadRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneWeightRoundTripTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);

	ATerraDyneChunk* Source = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Source chunk", Source);
	if (!Source) return false;

	Source->InitializeChunk(FIntPoint(0, 0), 1000.f, 32, nullptr, nullptr);

	// Paint layer 2 at the centre
	Source->ApplyLocalIdempotentEdit(FVector::ZeroVector, 400.f, 5000.f,
		ETerraDyneBrushMode::Paint, ETerraDyneLayer::Sculpt, 2);

	// Serialize
	FTerraDyneChunkData Data = Source->GetSerializedData();
	TestEqual("WeightData byte count should equal 4 * Resolution^2",
		Data.WeightData.Num(), 4 * Source->Resolution * Source->Resolution);

	// Deserialize into a fresh chunk
	ATerraDyneChunk* Target = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Target chunk", Target);
	if (!Target) return false;

	Target->InitializeChunk(FIntPoint(1, 0), 1000.f, 32, nullptr, nullptr);
	Target->LoadFromData(Data);

	// Compare layer 2: each value should be within 1/255 quantisation error
	const TArray<float>& SrcW = Source->WeightBuffers[2];
	const TArray<float>& DstW = Target->WeightBuffers[2];
	TestEqual("Weight buffer sizes match", SrcW.Num(), DstW.Num());

	bool bMatch = true;
	for (int32 i = 0; i < FMath::Min(SrcW.Num(), DstW.Num()); i++)
	{
		if (FMath::Abs(SrcW[i] - DstW[i]) > (1.f / 255.f) + KINDA_SMALL_NUMBER)
		{
			bMatch = false;
			break;
		}
	}
	TestTrue("Weight layer 2 survives save/load within uint8 quantisation tolerance", bMatch);

	return true;
}

// -----------------------------------------------------------------------
// Grass: RequestGrassRegen with null profile must not crash
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneGrassNullProfileTest,
	"TerraDyne.Grass.NullProfileSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneGrassNullProfileTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk) return false;

	Chunk->InitializeChunk(FIntPoint(0, 0), 1000.f, 32, nullptr, nullptr);

	// GrassProfile is null by default — this must not crash or assert
	Chunk->RequestGrassRegen();
	TestTrue("RequestGrassRegen with null profile survives", true);

	return true;
}

// -----------------------------------------------------------------------
// Grass: ApplyGrassResult with out-of-range variety index must not crash
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneGrassApplyResultSafeTest,
	"TerraDyne.Grass.ApplyResultInvalidIndexSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneGrassApplyResultSafeTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk) return false;

	Chunk->InitializeChunk(FIntPoint(0, 0), 1000.f, 32, nullptr, nullptr);

	TArray<FTransform> Empty;
	// Invalid index with no profile — must not crash
	Chunk->ApplyGrassResult(0, MoveTemp(Empty));
	TestTrue("ApplyGrassResult with null profile survives", true);

	return true;
}

#endif // WITH_EDITOR
