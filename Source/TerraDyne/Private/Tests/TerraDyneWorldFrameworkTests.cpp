// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/TerraDyneManager.h"
#include "Examples/TerraDyneIntegrationExamples.h"
#include "World/TerraDyneChunk.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"

#if WITH_EDITOR
#include "Tests/AutomationEditorCommon.h"

namespace TerraDyneWorldFrameworkTests
{
	class FWaitForSaveThenRestoreCommand final : public IAutomationLatentCommand
	{
	public:
		FWaitForSaveThenRestoreCommand(
			FAutomationTestBase* InTest,
			ATerraDyneManager* InManager,
			FString InSlotName)
			: Test(InTest)
			, Manager(InManager)
			, SlotName(MoveTemp(InSlotName))
			, DeadlineSeconds(FPlatformTime::Seconds() + 10.0)
		{
		}

		virtual bool Update() override
		{
			if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
			{
				if (FPlatformTime::Seconds() < DeadlineSeconds)
				{
					return false;
				}
				Test->AddError(FString::Printf(TEXT("Timed out waiting for async save slot '%s'."), *SlotName));
				return true;
			}

			ATerraDyneManager* ManagerPtr = Manager.Get();
			if (!ManagerPtr)
			{
				Test->AddError(TEXT("Manager was destroyed before async save validation."));
				UGameplayStatics::DeleteGameInSlot(SlotName, 0);
				return true;
			}

			ManagerPtr->LoadWorld(SlotName);
			Test->TestEqual(TEXT("Persistent population count restored"), ManagerPtr->GetPersistentPopulationCount(), 1);
			if (ManagerPtr->PersistentPopulationEntries.Num() == 1)
			{
				Test->TestEqual(
					TEXT("Population state restored"),
					ManagerPtr->PersistentPopulationEntries[0].State,
					ETerraDynePopulationState::Regrowing);
				Test->TestEqual(
					TEXT("Population type restored"),
					ManagerPtr->PersistentPopulationEntries[0].Descriptor.TypeId,
					FName(TEXT("BerryBush")));

				const FGuid RestoredPopulationId = ManagerPtr->PersistentPopulationEntries[0].PopulationId;
				Test->TestTrue(
					TEXT("Restored population can be reactivated"),
					ManagerPtr->SetPersistentPopulationDestroyed(RestoredPopulationId, false));
				Test->TestEqual(
					TEXT("Population state becomes active again"),
					ManagerPtr->PersistentPopulationEntries[0].State,
					ETerraDynePopulationState::Active);
			}

			const TArray<FTerraDynePCGPoint> PopulationPoints =
				ManagerPtr->GetPCGSeedPointsForChunk(FIntPoint::ZeroValue, true, false);
			Test->TestEqual(TEXT("PCG exports one active population point"), PopulationPoints.Num(), 1);
			UGameplayStatics::DeleteGameInSlot(SlotName, 0);
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		TWeakObjectPtr<ATerraDyneManager> Manager;
		FString SlotName;
		double DeadlineSeconds = 0.0;
	};

	class FWaitForSaveThenDeleteCommand final : public IAutomationLatentCommand
	{
	public:
		FWaitForSaveThenDeleteCommand(FAutomationTestBase* InTest, FString InSlotName)
			: Test(InTest)
			, SlotName(MoveTemp(InSlotName))
			, DeadlineSeconds(FPlatformTime::Seconds() + 10.0)
		{
		}

		virtual bool Update() override
		{
			if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
			{
				if (FPlatformTime::Seconds() < DeadlineSeconds)
				{
					return false;
				}
				Test->AddError(FString::Printf(TEXT("Timed out waiting for async save slot '%s'."), *SlotName));
				return true;
			}

			Test->TestTrue(TEXT("Save slot should exist after async save"), true);
			Test->TestTrue(TEXT("Save slot should delete cleanly"), UGameplayStatics::DeleteGameInSlot(SlotName, 0));
			Test->TestFalse(TEXT("Save slot should no longer exist after delete"),
				UGameplayStatics::DoesSaveGameExist(SlotName, 0));
			return true;
		}

	private:
		FAutomationTestBase* Test = nullptr;
		FString SlotName;
		double DeadlineSeconds = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDynePersistentPopulationSaveLoadTest,
	"TerraDyne.WorldFramework.PersistentPopulationSaveLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDynePersistentPopulationSaveLoadTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull("Manager", Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->GlobalChunkSize = 1000.0f;

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint::ZeroValue;
	Chunk->WorldSize = 1000.0f;
	Chunk->ChunkSizeWorldUnits = 1000.0f;
	Chunk->Initialize(16, 1000.0f);
	Manager->RebuildChunkMap();

	FTerraDynePopulationDescriptor Descriptor;
	Descriptor.Kind = ETerraDynePopulationKind::Harvestable;
	Descriptor.TypeId = TEXT("BerryBush");
	Descriptor.ActorClass = AStaticMeshActor::StaticClass();
	Descriptor.StaticMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Descriptor.bReplicates = true;
	Descriptor.bSnapToTerrain = true;
	Descriptor.bAllowRegrowth = true;
	Descriptor.RegrowthDelaySeconds = 5.0f;
	Descriptor.TerrainOffset = 25.0f;

	const FGuid PopulationId = Manager->PlacePersistentActorFromDescriptor(
		Descriptor,
		FTransform(FVector(0.0f, 0.0f, 250.0f)));
	TestTrue("Population id should be valid", PopulationId.IsValid());
	TestEqual("Persistent population count after placement", Manager->GetPersistentPopulationCount(), 1);

	TestTrue("Harvest should succeed", Manager->HarvestPersistentPopulation(PopulationId));
	TestEqual(
		"Population enters regrowing state",
		Manager->PersistentPopulationEntries[0].State,
		ETerraDynePopulationState::Regrowing);

	const FString SlotName = FString::Printf(
		TEXT("TerraDyneWorldFramework_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Manager->SaveWorld(SlotName);
	ADD_LATENT_AUTOMATION_COMMAND(
		TerraDyneWorldFrameworkTests::FWaitForSaveThenRestoreCommand(this, Manager, SlotName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneGameplayHooksAndProceduralMetadataTest,
	"TerraDyne.WorldFramework.GameplayHooksAndProceduralMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneGameplayHooksAndProceduralMetadataTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull("Manager", Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->GlobalChunkSize = 1000.0f;
	Manager->ProceduralWorldSettings.WorldSeed = 42;
	Manager->ProceduralWorldSettings.bEnableInfiniteEdgeGrowth = true;

	FTerraDyneBiomeOverlay Overlay;
	Overlay.BiomeTag = TEXT("Forest");
	Overlay.bApplyToProceduralChunks = true;
	Overlay.ProceduralNoiseMin = 0.0f;
	Overlay.ProceduralNoiseMax = 1.0f;
	Overlay.Priority = 5;
	Manager->BiomeOverlays.Add(Overlay);

	FTerraDyneAISpawnZone SpawnZone;
	SpawnZone.ZoneId = TEXT("ForestSpawn");
	SpawnZone.RequiredBiomeTag = TEXT("Forest");
	SpawnZone.LocalBounds = FBox(FVector(-250.0f, -250.0f, -200.0f), FVector(250.0f, 250.0f, 500.0f));
	Manager->AISpawnZones.Add(SpawnZone);

	FTerraDyneBuildPermissionZone BuildZone;
	BuildZone.ZoneId = TEXT("TownCore");
	BuildZone.Permission = ETerraDyneBuildPermission::Blocked;
	BuildZone.Reason = TEXT("Protected settlement footprint");
	BuildZone.LocalBounds = FBox(FVector(-300.0f, -300.0f, -500.0f), FVector(300.0f, 300.0f, 500.0f));
	Manager->BuildPermissionZones.Add(BuildZone);

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint::ZeroValue;
	Chunk->WorldSize = 1000.0f;
	Chunk->ChunkSizeWorldUnits = 1000.0f;
	Chunk->Initialize(16, 1000.0f);
	Manager->RebuildChunkMap();

	TestEqual("Chunk primary biome derived from overlay", Chunk->PrimaryBiomeTag, FName(TEXT("Forest")));

	FString BuildReason;
	TestFalse("Build permission should block location", Manager->CanBuildAtLocation(FVector::ZeroVector, BuildReason));
	TestTrue("Blocked build reason should be populated", !BuildReason.IsEmpty());

	const TArray<FTerraDyneAISpawnZone> MatchingZones = Manager->GetAISpawnZonesAtLocation(FVector::ZeroVector);
	TestEqual("AI spawn zone query should match one zone", MatchingZones.Num(), 1);
	TestEqual("AI spawn zone id preserved", MatchingZones[0].ZoneId, FName(TEXT("ForestSpawn")));

	const TArray<FTerraDynePCGPoint> PCGPoints =
		Manager->GetPCGSeedPointsForChunk(FIntPoint::ZeroValue, false, true);
	TestEqual("PCG exports one AI zone point", PCGPoints.Num(), 1);
	TestEqual("PCG biome tag preserved", PCGPoints[0].BiomeTag, FName(TEXT("Forest")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneIntegrationExamplesRuntimeHelpersTest,
	"TerraDyne.WorldFramework.IntegrationExamplesRuntimeHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneIntegrationExamplesRuntimeHelpersTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	TestNotNull("World", World);
	if (!World)
	{
		return false;
	}

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>();
	TestNotNull("Manager", Manager);
	if (!Manager)
	{
		return false;
	}

	Manager->GlobalChunkSize = 1000.0f;
	Manager->ProceduralWorldSettings.WorldSeed = 7;

	FTerraDyneBiomeOverlay Overlay;
	Overlay.BiomeTag = TEXT("Forest");
	Overlay.bApplyToProceduralChunks = true;
	Overlay.ProceduralNoiseMin = 0.0f;
	Overlay.ProceduralNoiseMax = 1.0f;
	Overlay.Priority = 5;
	Manager->BiomeOverlays.Add(Overlay);

	FTerraDyneAISpawnZone SpawnZone;
	SpawnZone.ZoneId = TEXT("ForestSpawn");
	SpawnZone.RequiredBiomeTag = TEXT("Forest");
	SpawnZone.LocalBounds = FBox(FVector(-250.0f, -250.0f, -200.0f), FVector(250.0f, 250.0f, 500.0f));
	Manager->AISpawnZones.Add(SpawnZone);

	FTerraDyneBuildPermissionZone BuildZone;
	BuildZone.ZoneId = TEXT("TownCore");
	BuildZone.Permission = ETerraDyneBuildPermission::Blocked;
	BuildZone.Reason = TEXT("Protected settlement footprint");
	BuildZone.LocalBounds = FBox(FVector(-300.0f, -300.0f, -500.0f), FVector(300.0f, 300.0f, 500.0f));
	Manager->BuildPermissionZones.Add(BuildZone);

	ATerraDyneChunk* Chunk = World->SpawnActor<ATerraDyneChunk>();
	TestNotNull("Chunk", Chunk);
	if (!Chunk)
	{
		return false;
	}

	Chunk->GridCoordinate = FIntPoint::ZeroValue;
	Chunk->WorldSize = 1000.0f;
	Chunk->ChunkSizeWorldUnits = 1000.0f;
	Chunk->Initialize(16, 1000.0f);
	Manager->RebuildChunkMap();

	ATerraDyneSaveLoadIntegrationExample* SaveLoadExample = World->SpawnActor<ATerraDyneSaveLoadIntegrationExample>();
	TestNotNull("Save/load example", SaveLoadExample);
	if (!SaveLoadExample)
	{
		return false;
	}

	SaveLoadExample->TargetManager = Manager;
	SaveLoadExample->SaveSlotName = FString::Printf(
		TEXT("TerraDyneExample_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));

	TestFalse("Save slot should not exist before first save", SaveLoadExample->DoesSaveExist());
	SaveLoadExample->SaveWorldToSlot();
	ADD_LATENT_AUTOMATION_COMMAND(
		TerraDyneWorldFrameworkTests::FWaitForSaveThenDeleteCommand(this, SaveLoadExample->SaveSlotName));

	ATerraDyneBiomeReactorIntegrationExample* BiomeReactor = World->SpawnActor<ATerraDyneBiomeReactorIntegrationExample>();
	TestNotNull("Biome reactor example", BiomeReactor);
	if (!BiomeReactor)
	{
		return false;
	}

	BiomeReactor->TargetManager = Manager;
	BiomeReactor->SetActorLocation(FVector::ZeroVector);

	TestTrue("Gameplay context refresh should succeed", BiomeReactor->RefreshGameplayContext());
	TestEqual("Biome reactor caches biome tag", BiomeReactor->LastBiomeTag, FName(TEXT("Forest")));
	TestFalse("Biome reactor caches blocked build state", BiomeReactor->bLastCanBuild);
	TestEqual("Biome reactor caches one AI spawn zone", BiomeReactor->LastAISpawnZones.Num(), 1);
	TestTrue("Biome reactor caches build reason", !BiomeReactor->LastBuildReason.IsEmpty());

	return true;
}
#endif
