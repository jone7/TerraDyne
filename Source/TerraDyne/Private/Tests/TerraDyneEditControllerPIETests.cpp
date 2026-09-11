// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Core/TerraDyneEditController.h"
#include "Core/TerraDyneManager.h"
#include "UI/TerraDyneToolWidget.h"
#include "World/TerraDyneChunk.h"
#include "Engine/Engine.h"

#if WITH_EDITOR

#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Tests/TerraDynePIETestGameMode.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace TerraDyneEditControllerPIETest
{
	struct FConfigBackup
	{
		bool bCaptured = false;
		TSubclassOf<AGameModeBase> DefaultGameMode;
	};

	static FConfigBackup GBackup;

	static bool IsNearlyZeroBuffer(const TArray<float>& Buffer, float Tolerance = KINDA_SMALL_NUMBER)
	{
		for (float Value : Buffer)
		{
			if (!FMath::IsNearlyZero(Value, Tolerance))
			{
				return false;
			}
		}

		return true;
	}

	static UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static UWorld* GetPIEWorld()
	{
		return GEditor ? GEditor->PlayWorld : nullptr;
	}

	static ATerraDyneEditController* GetPIEController()
	{
		UWorld* const PIEWorld = GetPIEWorld();
		if (!PIEWorld)
		{
			return nullptr;
		}

		for (FConstPlayerControllerIterator It = PIEWorld->GetPlayerControllerIterator(); It; ++It)
		{
			if (ATerraDyneEditController* const Controller = Cast<ATerraDyneEditController>(It->Get()))
			{
				return Controller;
			}
		}

		return nullptr;
	}

	static bool ConfigureEditorWorldForPIETest(FAutomationTestBase* Test)
	{
		UWorld* const EditorWorld = FAutomationEditorCommonUtils::CreateNewMap();
		Test->TestNotNull(TEXT("Editor world should exist"), EditorWorld);
		if (!EditorWorld)
		{
			return false;
		}

		AWorldSettings* const WorldSettings = EditorWorld->GetWorldSettings();
		Test->TestNotNull(TEXT("World settings should exist"), WorldSettings);
		if (!WorldSettings)
		{
			return false;
		}

		GBackup.bCaptured = true;
		GBackup.DefaultGameMode = WorldSettings->DefaultGameMode;

		WorldSettings->DefaultGameMode = ATerraDynePIETestGameMode::StaticClass();

		return true;
	}

	static void RestoreEditorWorldAfterPIETest()
	{
		if (!GBackup.bCaptured)
		{
			return;
		}

		if (UWorld* const EditorWorld = GetEditorWorld())
		{
			if (AWorldSettings* const WorldSettings = EditorWorld->GetWorldSettings())
			{
				WorldSettings->DefaultGameMode = GBackup.DefaultGameMode;
			}
		}

		GBackup = FConfigBackup();
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
	FTerraDyneWaitForPIEControllerCommand,
	FAutomationTestBase*,
	Test,
	double,
	StartTime,
	double,
	TimeoutSeconds);

bool FTerraDyneWaitForPIEControllerCommand::Update()
{
	if (TerraDyneEditControllerPIETest::GetPIEController() != nullptr)
	{
		return true;
	}

	if ((FPlatformTime::Seconds() - StartTime) >= TimeoutSeconds)
	{
		Test->AddError(TEXT("Timed out waiting for the TerraDyne PIE controller."));
		return true;
	}

	return false;
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
	FTerraDyneVerifyDisabledPlayModeUIBranchCommand,
	FAutomationTestBase*,
	Test);

bool FTerraDyneVerifyDisabledPlayModeUIBranchCommand::Update()
{
	using namespace TerraDyneEditControllerPIETest;

	UWorld* const PIEWorld = GetPIEWorld();
	Test->TestNotNull(TEXT("PIE world should exist"), PIEWorld);
	if (!PIEWorld)
	{
		return true;
	}

	ATerraDyneEditController* const Controller = GetPIEController();
	Test->TestNotNull(TEXT("PIE controller should be a TerraDyne edit controller"), Controller);
	if (!Controller)
	{
		return true;
	}

	Test->TestFalse(TEXT("Play mode tool UI should be disabled on the PIE controller"), Controller->bEnablePlayModeToolUI);
	Test->TestFalse(TEXT("Mouse cursor should be hidden when the play mode tool UI is disabled"), Controller->bShowMouseCursor);
	Test->TestFalse(TEXT("Click events should be disabled when the play mode tool UI is disabled"), Controller->bEnableClickEvents);
	Test->TestFalse(TEXT("Mouse-over events should be disabled when the play mode tool UI is disabled"), Controller->bEnableMouseOverEvents);

	TArray<UUserWidget*> FoundWidgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PIEWorld, FoundWidgets, Controller->UIClass, false);
	Test->TestEqual(TEXT("No TerraDyne tool widget should be present in PIE when the UI switch is disabled"), FoundWidgets.Num(), 0);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ATerraDyneManager* const Manager = PIEWorld->SpawnActor<ATerraDyneManager>(ATerraDyneManager::StaticClass(), FTransform::Identity, SpawnParams);
	Test->TestNotNull(TEXT("PIE manager should spawn"), Manager);
	if (!Manager)
	{
		return true;
	}

	Manager->GlobalChunkSize = 1000.0f;

	ATerraDyneChunk* const Chunk = PIEWorld->SpawnActorDeferred<ATerraDyneChunk>(
		ATerraDyneChunk::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Test->TestNotNull(TEXT("PIE chunk should spawn deferred"), Chunk);
	if (!Chunk)
	{
		return true;
	}

	Chunk->bIsAuthoredChunk = true;
	Chunk->GridCoordinate = FIntPoint::ZeroValue;
	Chunk->WorldSize = 1000.0f;
	Chunk->ChunkSizeWorldUnits = 1000.0f;
	Chunk->Resolution = 32;
	Chunk->FinishSpawning(FTransform::Identity);

	Manager->RebuildChunkMap();
	Test->TestEqual(TEXT("Manager should register the authored test chunk at grid [0,0]"), Manager->GetChunkAtCoord(FIntPoint::ZeroValue), Chunk);
	Test->TestTrue(TEXT("Fresh authored test chunk should start with zero sculpt data"), IsNearlyZeroBuffer(Chunk->SculptBuffer));

	const int32 UndoDepthBefore = Manager->GetUndoDepth(Controller);
	const int32 RedoDepthBefore = Manager->GetRedoDepth(Controller);

	FTerraDyneBrushParams BrushParams;
	BrushParams.WorldLocation = FVector::ZeroVector;
	BrushParams.Radius = 450.0f;
	BrushParams.Strength = 1500.0f;
	BrushParams.BrushMode = ETerraDyneBrushMode::Raise;
	BrushParams.WeightLayerIndex = 0;
	BrushParams.FlattenHeight = 0.0f;
	BrushParams.bIsStrokeStart = true;

	Controller->Server_ApplyBrush(BrushParams);
	Controller->Server_CommitStroke();
	Controller->OnUndoPressed();
	Controller->OnRedoPressed();

	Test->TestTrue(TEXT("Disabled play mode tool UI should block brush RPCs from modifying sculpt data"), IsNearlyZeroBuffer(Chunk->SculptBuffer));
	Test->TestEqual(TEXT("Disabled play mode tool UI should not create undo history"), Manager->GetUndoDepth(Controller), UndoDepthBefore);
	Test->TestEqual(TEXT("Disabled play mode tool UI should not create redo history"), Manager->GetRedoDepth(Controller), RedoDepthBefore);

	return true;
}

DEFINE_LATENT_AUTOMATION_COMMAND(FTerraDyneRestorePIEControllerConfigCommand);

bool FTerraDyneRestorePIEControllerConfigCommand::Update()
{
	TerraDyneEditControllerPIETest::RestoreEditorWorldAfterPIETest();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneEditControllerDisabledPlayModeUIPIETest,
	"TerraDyne.Controller.PIE.DisabledPlayModeUI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FTerraDyneEditControllerDisabledPlayModeUIPIETest::RunTest(const FString& Parameters)
{
	using namespace TerraDyneEditControllerPIETest;

	if (!ConfigureEditorWorldForPIETest(this))
	{
		RestoreEditorWorldAfterPIETest();
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FTerraDyneWaitForPIEControllerCommand(this, FPlatformTime::Seconds(), 15.0));
	ADD_LATENT_AUTOMATION_COMMAND(FTerraDyneVerifyDisabledPlayModeUIBranchCommand(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FTerraDyneRestorePIEControllerConfigCommand());

	return true;
}

#endif // WITH_EDITOR
