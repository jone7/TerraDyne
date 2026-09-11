// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "World/TerraDyneOrchestrator.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerraDyneDemoSimulationTest, "TerraDyne.Demo.Simulation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneDemoSimulationTest::RunTest(const FString& Parameters)
{
	// Create a temporary world
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!World)
	{
		AddError("Failed to create World");
		return false;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	// Spawn Orchestrator
	ATerraDyneOrchestrator* Orchestrator = World->SpawnActor<ATerraDyneOrchestrator>();

	if (Orchestrator)
	{
		Orchestrator->DispatchBeginPlay();

		TestTrue("Phase is Warmup", Orchestrator->CurrentPhase == EShowcasePhase::Warmup);

		struct FExpectedTransition
		{
			float Duration;
			EShowcasePhase Phase;
			const TCHAR* Label;
		};

		const TArray<FExpectedTransition> ExpectedTransitions = {
			{4.1f, EShowcasePhase::AuthoredWorldDemo, TEXT("AuthoredWorldDemo")},
			{8.1f, EShowcasePhase::SculptingDemo, TEXT("SculptingDemo")},
			{10.1f, EShowcasePhase::LayerDemo, TEXT("LayerDemo")},
			{6.1f, EShowcasePhase::PaintDemo, TEXT("PaintDemo")},
			{6.1f, EShowcasePhase::UndoRedoDemo, TEXT("UndoRedoDemo")},
			{8.1f, EShowcasePhase::PopulationDemo, TEXT("PopulationDemo")},
			{10.1f, EShowcasePhase::ProceduralWorldDemo, TEXT("ProceduralWorldDemo")},
			{10.1f, EShowcasePhase::GameplayHooksDemo, TEXT("GameplayHooksDemo")},
			{8.1f, EShowcasePhase::DesignerWorkflowDemo, TEXT("DesignerWorkflowDemo")},
			{6.1f, EShowcasePhase::PersistenceDemo, TEXT("PersistenceDemo")},
			{8.1f, EShowcasePhase::ReplicationDemo, TEXT("ReplicationDemo")},
			{5.1f, EShowcasePhase::LODDemo, TEXT("LODDemo")},
			{6.1f, EShowcasePhase::Interactive, TEXT("Interactive")},
		};

		for (const FExpectedTransition& Transition : ExpectedTransitions)
		{
			// NOTE: Direct Tick() bypasses FTimerManager. If Orchestrator uses engine timers,
			// this test may not accurately simulate phase transitions.
			Orchestrator->Tick(Transition.Duration);
			TestTrue(
				FString::Printf(TEXT("Phase advanced to %s"), Transition.Label),
				Orchestrator->CurrentPhase == Transition.Phase);
		}

		// Interactive should stay indefinitely
		// NOTE: Direct Tick() bypasses FTimerManager. If Orchestrator uses engine timers,
		// this test may not accurately simulate phase transitions.
		Orchestrator->Tick(100.0f);
		TestTrue("Phase stays Interactive", Orchestrator->CurrentPhase == EShowcasePhase::Interactive);
	}
	else
	{
		AddError("Orchestrator failed to spawn");
	}

	// Cleanup: destroy world content first, then remove the engine's world context
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);

	return true;
}
