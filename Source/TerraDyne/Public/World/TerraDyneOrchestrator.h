// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TerraDyneWorldTypes.h"
#include "TerraDyneOrchestrator.generated.h"

UENUM(BlueprintType)
enum class EShowcasePhase : uint8
{
	Warmup,
	AuthoredWorldDemo,
	SculptingDemo,
	LayerDemo,
	PaintDemo,
	UndoRedoDemo,
	PopulationDemo,
	ProceduralWorldDemo,
	GameplayHooksDemo,
	DesignerWorkflowDemo,
	PersistenceDemo,
	ReplicationDemo,
	LODDemo,
	Interactive
};

USTRUCT()
struct FShowcaseCameraWaypoint
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float TransitDuration = 1.0f;
	float HoldDuration = 1.0f;
};

USTRUCT()
struct FShowcasePhaseConfig
{
	GENERATED_BODY()

	float Duration = 5.0f;
	FString Title;
	FString Description;
};

/**
 * ATerraDyneOrchestrator - drives the product demo flow and can bootstrap
 * a fallback runtime world when the map was not fully prepared in advance.
 */
UCLASS()
class TERRADYNE_API ATerraDyneOrchestrator : public AActor
{
	GENERATED_BODY()

public:
	ATerraDyneOrchestrator();
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Showcase")
	EShowcasePhase CurrentPhase;

	UPROPERTY(Transient)
	TObjectPtr<class UUserWidget> ActiveUI;

	UFUNCTION(BlueprintCallable, Category = "Showcase")
	void RestartShowcase();

	UFUNCTION(BlueprintCallable, Category = "Showcase")
	void StartPersistenceTest();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	bool bInteractiveOnly = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	float PhaseTimer;
	float ActionTimer;
	FVector OriginalPlayerPos;

	// Input State
	bool bIsClicking;
	bool bFlattenHeightLocked;
	float LockedFlattenHeight;
	void SetupInput();
	void OnLeftClickStart();
	void OnLeftClickStop();
	void PerformToolAction();

	// Phase Config
	TMap<EShowcasePhase, FShowcasePhaseConfig> PhaseConfigs;
	TArray<EShowcasePhase> ShowcaseSequence;
	void InitPhaseConfigs();
	void InitShowcaseSequence();
	int32 GetPhaseIndex(EShowcasePhase Phase) const;

	// Camera Waypoint System
	TArray<FShowcaseCameraWaypoint> CameraTrack;
	int32 CameraWaypointIndex;
	float CameraWaypointTimer;
	bool bCameraInTransit;
	bool bCameraEnabled;
	FVector CameraTransitStartLocation;
	FRotator CameraTransitStartRotation;
	void SetCameraTrack(const TArray<FShowcaseCameraWaypoint>& Track);
	void UpdateCamera(float DeltaTime);

	// Overlay Helpers
	void SetOverlay(const FString& Title, const FString& Description, const FString& Detail = TEXT(""));
	void ClearOverlay();

	// World Bootstrap
	UPROPERTY(Transient)
	TObjectPtr<class UTerraDyneWorldPreset> ShowcasePreset;

	bool bShowcaseBaselineCaptured = false;

	TWeakObjectPtr<class ATerraDyneManager> BoundManager;
	void EnsureShowcaseWorld();
	class ATerraDyneManager* GetManager() const;
	class ATerraDyneManager* EnsureShowcaseManager();
	class UTerraDyneWorldPreset* BuildFallbackPreset();
	class ATerraDyneChunk* EnsurePrimaryAuthoredChunk(class ATerraDyneManager* Manager);
	void SeedFallbackTerrain(class ATerraDyneManager* Manager);
	void EnsureFallbackFoliagePresentation(class ATerraDyneManager* Manager, class ATerraDyneChunk* Chunk);
	void EnsureShowcaseTerrainMaterial(class ATerraDyneManager* Manager);
	void EnsureGrassProfile(class ATerraDyneManager* Manager);
	void EnsureBasePopulation(class ATerraDyneManager* Manager);
	FGuid FindPopulationIdByTypeId(const class ATerraDyneManager* Manager, FName TypeId) const;
	void CaptureShowcaseBaseline();
	bool RestoreShowcaseBaseline();
	int32 ResolveShowcaseGrassLayerIndex(class ATerraDyneManager* Manager) const;
	float MeasureShowcaseLayerPaintCoverage(class ATerraDyneManager* Manager, int32 LayerIndex) const;
	void BindManagerEvents(class ATerraDyneManager* Manager);
	void UnbindManagerEvents();
	void ClearShowcaseTimers();

	// Phase Transition
	EShowcasePhase GetNextPhase(EShowcasePhase Current) const;
	void AdvancePhase();
	void OnPhaseEnter(EShowcasePhase Phase);

	// Phase Update Methods
	void UpdateAuthoredWorldDemo(float DeltaTime);
	void UpdateSculptingDemo(float DeltaTime);
	void UpdateLayerDemo(float DeltaTime);
	void UpdatePaintDemo(float DeltaTime);
	void UpdateUndoRedoDemo(float DeltaTime);
	void UpdatePopulationDemo(float DeltaTime);
	void UpdateProceduralWorldDemo(float DeltaTime);
	void UpdateGameplayHooksDemo(float DeltaTime);
	void UpdateDesignerWorkflowDemo(float DeltaTime);
	void UpdatePersistenceDemo(float DeltaTime);
	void UpdateReplicationDemo(float DeltaTime);
	void UpdateLODDemo(float DeltaTime);

	// Demo sub-state
	int32 UndoRedoDemoStep;
	float UndoRedoSubTimer;
	bool bPopulationRuntimePlacementTriggered;
	bool bPopulationHarvestTriggered;
	bool bPopulationDestroyTriggered;
	bool bPopulationRestoreTriggered;
	bool bGameplayPulseTriggered;
	FTimerHandle PersistenceMutationTimer;
	FTimerHandle PersistenceRestoreTimer;

	// Delegate telemetry
	int32 TerrainEventCount;
	int32 FoliageEventCount;
	int32 PopulationEventCount;
	FIntPoint LastTerrainEventCoord;
	int32 LastFoliageEventInstances;
	FName LastPopulationReason;
	FName LastPopulationTypeId;

	UFUNCTION()
	void HandleTerrainChanged(const FTerraDyneTerrainChangeEvent& Event);

	UFUNCTION()
	void HandleFoliageChanged(const FTerraDyneFoliageChangeEvent& Event);

	UFUNCTION()
	void HandlePopulationChanged(const FTerraDynePopulationChangeEvent& Event);
};
