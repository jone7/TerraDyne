// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneSaveGame.h"
#include "World/TerraDyneTileData.h"
#include "TerraDyneEditController.generated.h"

class UTerraDyneToolWidget;
class UTerraDyneReplicationComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;

UCLASS()
class TERRADYNE_API ATerraDyneEditController : public APlayerController
{
	GENERATED_BODY()

public:
	ATerraDyneEditController();

	UPROPERTY(EditDefaultsOnly, Category = "TerraDyne|UI")
	TSubclassOf<UTerraDyneToolWidget> UIClass;

	// Safe play-mode switch: when disabled, TerraDyne skips spawning the tool UI and also
	// suppresses edit-mode cursor/input/brush preview so the controller behaves like gameplay.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|UI", meta = (DisplayName = "Enable Play Mode Tool UI"))
	bool bEnablePlayModeToolUI = true;

	/** Safe-by-default gate for client-originated terrain RPCs. Enable only on an authorized controller class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TerraDyne|Multiplayer")
	bool bAllowRemoteTerrainEditing = false;

	/** Reusable state-transfer bridge. Projects may add this component to their own PlayerController instead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TerraDyne|Multiplayer")
	TObjectPtr<UTerraDyneReplicationComponent> TerraDyneReplication;

	/** Project authorization hook, evaluated on the server after structural/rate/distance validation. */
	UFUNCTION(BlueprintNativeEvent, BlueprintAuthorityOnly, Category = "TerraDyne|Multiplayer")
	bool IsRemoteTerrainEditAuthorized(const FTerraDyneBrushParams& Params) const;

	// Called from STerraDynePanel (Task 6) and keyboard binding
	void OnUndoPressed();
	void OnRedoPressed();

	/** Client → Server: request a brush application. */
	UFUNCTION(Server, Reliable)
	void Server_ApplyBrush(const FTerraDyneBrushParams& Params);

	/** Client → Server: commit the current stroke (mouse released). */
	UFUNCTION(Server, Reliable)
	void Server_CommitStroke();

	/** Client → Server: request undo. */
	UFUNCTION(Server, Reliable)
	void Server_Undo();

	/** Client → Server: request redo. */
	UFUNCTION(Server, Reliable)
	void Server_Redo();

	/** Deprecated local compatibility helper. Network state now uses TerraDyneReplication fragments. */
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Multiplayer",
		meta = (DeprecatedFunction, DeprecationMessage = "Use UTerraDyneReplicationComponent fragmented state transfer."))
	void Client_ReceiveChunkSync(const FTerraDyneChunkData& Data);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTerraDyneToolWidget> ActiveUI;

	TMap<FIntPoint, FTerraDyneChunkData> PendingChunkSyncs;

	// Mouse State
	bool bIsClicking;
	bool bHasValidHit;
	FVector LastHitLocation;
	
	// Default values when UI is not available
	ETerraDyneToolMode CurrentTool;
	float BrushRadius;
	float BrushStrength;
	
	// Debug
	bool bShowDebugCursor;
	FVector LastValidHitLocation;

	// Brush Preview Decal
	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> BrushDecal;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BrushDecalMID;

	// Flatten tool state — height locked on press-begin, cleared on release
	bool bFlattenHeightLocked;
	float LockedFlattenHeight;

	// Stroke tracking for undo/redo
	bool bStrokeBegun = false;
	float AvailableBrushRequestTokens = -1.0f;
	double LastBrushTokenRefillSeconds = 0.0;
	double LastBrushRejectionLogSeconds = -1000000.0;

	// Input Handlers
	void OnLeftClickStart();
	void OnLeftClickStop();
	void OnMouseWheel(float Val);

	// Helpers
	bool GetTerrainHit(FHitResult& OutHit);
	void PerformToolAction(const FVector& Location);
	bool ConsumeBrushRequestToken();
	bool ValidateRemoteBrushRequest(
		const FTerraDyneBrushParams& Params,
		class ATerraDyneManager* Manager,
		FString& OutReason);
	void LogRejectedBrushRequest(const FString& Reason);
};
