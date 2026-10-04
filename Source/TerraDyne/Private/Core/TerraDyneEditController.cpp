// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneEditController.h"
#include "UI/TerraDyneToolWidget.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneReplicationComponent.h"
#include "Core/TerraDyneSubsystem.h"
#include "DrawDebugHelpers.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/InputComponent.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/InputSettings.h"
#include "TerraDyneModule.h"
#include "Engine/World.h"
#include "World/TerraDyneTileData.h"
#include "World/TerraDyneChunk.h"
#include "Settings/TerraDyneSettings.h"

ATerraDyneEditController::ATerraDyneEditController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	bIsClicking = false;
	PrimaryActorTick.bCanEverTick = true;
	UIClass = UTerraDyneToolWidget::StaticClass();
	bAllowRemoteTerrainEditing = false;
	TerraDyneReplication = CreateDefaultSubobject<UTerraDyneReplicationComponent>(TEXT("TerraDyneReplication"));
	
	BrushRadius = 2000.0f;
	BrushStrength = 1.0f;
	CurrentTool = ETerraDyneToolMode::SculptRaise;
	bShowDebugCursor = true;
	bFlattenHeightLocked = false;
	LockedFlattenHeight = 0.f;
	LastValidHitLocation = FVector::ZeroVector;

	// Brush preview decal
	BrushDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("BrushPreviewDecal"));
	BrushDecal->SetupAttachment(GetRootComponent());
	BrushDecal->DecalSize = FVector(10000.0f, 2000.0f, 2000.0f); // Z-extent, X-radius, Y-radius
	BrushDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // Project downward
	BrushDecal->SetVisibility(false);

	// 开源包缺少专用笔刷材质，试验显式采用引擎默认 Decal 并保留调试轮廓。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BrushMatFinder(
		TEXT("/Engine/EngineMaterials/DefaultDecalMaterial"));
	if (BrushMatFinder.Succeeded())
	{
		BrushDecal->SetDecalMaterial(BrushMatFinder.Object);
	}
}

void ATerraDyneEditController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// TODO: Migrate to Enhanced Input System (UEnhancedInputComponent + UInputAction assets)
	// Legacy input is retained for backward compatibility with existing projects.

	if (!bEnablePlayModeToolUI)
	{
		return;
	}

	// Validate input action exists before binding
	const UInputSettings* InputSettings = GetDefault<UInputSettings>();
	bool bHasTerraDyneClick = false;
	if (InputSettings)
	{
		for (const FInputActionKeyMapping& Mapping : InputSettings->GetActionMappings())
		{
			if (Mapping.ActionName == TEXT("TerraDyneClick"))
			{
				bHasTerraDyneClick = true;
				break;
			}
		}
	}

	if (!bHasTerraDyneClick)
	{
		UE_LOG(LogTerraDyne, Error, TEXT("TerraDyne: Input action 'TerraDyneClick' not found. Add it in Project Settings > Input to enable terrain editing."));
		if (UWorld* World = GetWorld())
		{
			if (UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>())
			{
				Sys->ShowNotification(
					NSLOCTEXT("TerraDyne", "NoInput", "Input action 'TerraDyneClick' not found. Add it in Project Settings > Input."),
					ETerraDyneNotifySeverity::Error);
			}
		}
	}

	InputComponent->BindAction("TerraDyneClick", IE_Pressed, this, &ATerraDyneEditController::OnLeftClickStart);
	InputComponent->BindAction("TerraDyneClick", IE_Released, this, &ATerraDyneEditController::OnLeftClickStop);
	InputComponent->BindAxis("MouseWheelAxis", this, &ATerraDyneEditController::OnMouseWheel);
	InputComponent->BindAction("TerraDyneUndo", IE_Pressed, this, &ATerraDyneEditController::OnUndoPressed);
	InputComponent->BindAction("TerraDyneRedo", IE_Pressed, this, &ATerraDyneEditController::OnRedoPressed);
}

void ATerraDyneEditController::BeginPlay()
{
	Super::BeginPlay();
	
	UE_LOG(LogTerraDyne, Log, TEXT("EditController: Initializing..."));

	TArray<UUserWidget*> FoundWidgets;
	if (UIClass)
	{
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetWorld(), FoundWidgets, UIClass, false);
	}

	if (bEnablePlayModeToolUI)
	{
		// Spawn UI if it doesn't already exist
		if (UIClass)
		{
			if (FoundWidgets.Num() == 0)
			{
				ActiveUI = CreateWidget<UTerraDyneToolWidget>(this, UIClass);
				if (ActiveUI)
				{
					ActiveUI->AddToViewport(100);
					ActiveUI->SetVisibility(ESlateVisibility::Visible);
					UE_LOG(LogTerraDyne, Log, TEXT("EditController: UI spawned"));
				}
			}
			else
			{
				ActiveUI = Cast<UTerraDyneToolWidget>(FoundWidgets[0]);
				UE_LOG(LogTerraDyne, Log, TEXT("EditController: Connected to existing UI"));
			}
		}
		else
		{
			UE_LOG(LogTerraDyne, Warning, TEXT("EditController: No UIClass set"));
		}

		bShowMouseCursor = true;
		bEnableClickEvents = true;
		bEnableMouseOverEvents = true;

		// Set input mode - Game AND UI so mouse works
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);

		// Create dynamic material instance for brush decal
		if (BrushDecal && BrushDecal->GetDecalMaterial())
		{
			BrushDecalMID = BrushDecal->CreateDynamicMaterialInstance();
		}
		else if (BrushDecal)
		{
			// Fallback: flat-color translucent decal
			UMaterialInterface* FallbackMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultDecalMaterial.DefaultDecalMaterial"));
			if (FallbackMat)
			{
				BrushDecal->SetDecalMaterial(FallbackMat);
				BrushDecalMID = BrushDecal->CreateDynamicMaterialInstance();
			}
		}
	}
	else
	{
		for (UUserWidget* FoundWidget : FoundWidgets)
		{
			if (FoundWidget)
			{
				FoundWidget->RemoveFromParent();
			}
		}

		ActiveUI = nullptr;
		BrushDecalMID = nullptr;

		if (BrushDecal)
		{
			BrushDecal->SetVisibility(false);
		}

		bShowMouseCursor = false;
		bEnableClickEvents = false;
		bEnableMouseOverEvents = false;

		FInputModeGameOnly InputMode;
		SetInputMode(InputMode);

		UE_LOG(LogTerraDyne, Log, TEXT("EditController: Play mode tool UI disabled"));
	}

	// Move player up so they don't spawn inside terrain
	APawn* MyPawn = GetPawn();
	if (MyPawn)
	{
		MyPawn->SetActorLocation(FVector(0, 0, 2000));
	}
	
	UE_LOG(LogTerraDyne, Log, TEXT("EditController: Ready - %s"),
		bEnablePlayModeToolUI ? TEXT("Play mode tool UI enabled") : TEXT("Play mode tool UI disabled"));

}

void ATerraDyneEditController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Clean up undo/redo stacks for this player on the server
	if (HasAuthority())
	{
		UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
		if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
		{
			Manager->CleanupPlayerStacks(this);
		}
	}
	PendingChunkSyncs.Empty();
	Super::EndPlay(EndPlayReason);
}

void ATerraDyneEditController::OnLeftClickStart()
{
	if (!bEnablePlayModeToolUI)
	{
		bIsClicking = false;
		return;
	}

	bIsClicking = true;
	bFlattenHeightLocked = false;  // Reset lock each new press
	bStrokeBegun = false;
}

void ATerraDyneEditController::OnLeftClickStop()
{
	if (!bEnablePlayModeToolUI)
	{
		bIsClicking = false;
		bFlattenHeightLocked = false;
		bStrokeBegun = false;
		return;
	}

	bIsClicking = false;
	bFlattenHeightLocked = false;
	if (bStrokeBegun)
	{
		bStrokeBegun = false;
		if (HasAuthority())
		{
			UTerraDyneSubsystem* Sys = GetWorld()->GetSubsystem<UTerraDyneSubsystem>();
			if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
			{
				Manager->CommitStroke(this);
			}
		}
		else
		{
			Server_CommitStroke();
		}
	}
}

void ATerraDyneEditController::OnUndoPressed()
{
	if (!bEnablePlayModeToolUI)
	{
		return;
	}

	if (HasAuthority())
	{
		UWorld* World = GetWorld();
		if (!World) return;
		UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>();
		if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
		{
			Manager->Undo(this);
		}
	}
	else
	{
		Server_Undo();
	}
}

void ATerraDyneEditController::OnRedoPressed()
{
	if (!bEnablePlayModeToolUI)
	{
		return;
	}

	if (HasAuthority())
	{
		UWorld* World = GetWorld();
		if (!World) return;
		UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>();
		if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
		{
			Manager->Redo(this);
		}
	}
	else
	{
		Server_Redo();
	}
}

// --- Server RPCs ---

bool ATerraDyneEditController::IsRemoteTerrainEditAuthorized_Implementation(const FTerraDyneBrushParams& Params) const
{
	return bAllowRemoteTerrainEditing;
}

bool ATerraDyneEditController::ConsumeBrushRequestToken()
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float Rate = Settings ? FMath::Max(1.0f, Settings->MaxBrushRPCsPerSecond) : 30.0f;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (AvailableBrushRequestTokens < 0.0f)
	{
		AvailableBrushRequestTokens = Rate;
		LastBrushTokenRefillSeconds = Now;
	}
	const double Elapsed = FMath::Max(0.0, Now - LastBrushTokenRefillSeconds);
	AvailableBrushRequestTokens = FMath::Min(Rate, AvailableBrushRequestTokens + static_cast<float>(Elapsed) * Rate);
	LastBrushTokenRefillSeconds = Now;
	if (AvailableBrushRequestTokens < 1.0f)
	{
		return false;
	}
	AvailableBrushRequestTokens -= 1.0f;
	return true;
}

void ATerraDyneEditController::LogRejectedBrushRequest(const FString& Reason)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastBrushRejectionLogSeconds >= 1.0)
	{
		LastBrushRejectionLogSeconds = Now;
		UE_LOG(LogTerraDyne, Warning, TEXT("Rejected remote terrain edit from %s: %s"), *GetNameSafe(this), *Reason);
	}
}

bool ATerraDyneEditController::ValidateRemoteBrushRequest(
	const FTerraDyneBrushParams& Params,
	ATerraDyneManager* Manager,
	FString& OutReason)
{
	if (!Manager || !bAllowRemoteTerrainEditing || !IsRemoteTerrainEditAuthorized(Params))
	{
		OutReason = TEXT("remote terrain editing is not authorized");
		return false;
	}
	if (!ConsumeBrushRequestToken())
	{
		OutReason = TEXT("brush request rate exceeded");
		return false;
	}

	const FVector WorldLocation(Params.WorldLocation);
	if (WorldLocation.ContainsNaN() || !FMath::IsFinite(Params.Radius) ||
		!FMath::IsFinite(Params.Strength) || !FMath::IsFinite(Params.FlattenHeight))
	{
		OutReason = TEXT("request contains a non-finite value");
		return false;
	}
	if (static_cast<uint8>(Params.BrushMode) > static_cast<uint8>(ETerraDyneBrushMode::Paint) ||
		static_cast<uint8>(Params.TargetLayer) > static_cast<uint8>(ETerraDyneLayer::Active))
	{
		OutReason = TEXT("request contains an invalid brush mode or target layer");
		return false;
	}

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float MaxRadius = Settings ? Settings->MaxBrushRadius : 10000.0f;
	const float MaxStrength = Settings ? Settings->MaxBrushStrength : 5000.0f;
	if (Params.Radius <= 0.0f || Params.Radius > MaxRadius || FMath::Abs(Params.Strength) > MaxStrength)
	{
		OutReason = TEXT("brush radius or strength exceeds the server limit");
		return false;
	}
	if (Params.WeightLayerIndex < 0 || Params.WeightLayerIndex >= ATerraDyneChunk::NumWeightLayers)
	{
		OutReason = TEXT("paint layer index is invalid");
		return false;
	}

	const AActor* ReferenceActor = GetPawn();
	if (!ReferenceActor)
	{
		ReferenceActor = GetViewTarget();
	}
	const float MaxDistance = Settings ? Settings->MaxBrushDistanceFromOwner : 25000.0f;
	if (MaxDistance > 0.0f && (!ReferenceActor ||
		FVector::DistSquared(ReferenceActor->GetActorLocation(), WorldLocation) > FMath::Square(MaxDistance)))
	{
		OutReason = TEXT("brush is too far from the controlled pawn or view target");
		return false;
	}

	const TArray<ATerraDyneChunk*> AffectedChunks = Manager->GetChunksInRadius(WorldLocation, Params.Radius);
	const int32 MaxAffectedChunks = Settings ? Settings->MaxAffectedChunksPerBrush : 16;
	if (AffectedChunks.Num() == 0 || AffectedChunks.Num() > MaxAffectedChunks)
	{
		OutReason = TEXT("brush affects no editable chunk or exceeds the chunk-cost budget");
		return false;
	}

	if (Params.bIsStrokeStart)
	{
		if (Manager->HasPendingStroke(this))
		{
			OutReason = TEXT("a stroke is already active");
			return false;
		}
	}
	else if (!Manager->HasPendingStroke(this))
	{
		OutReason = TEXT("brush continuation has no active stroke");
		return false;
	}

	if (Params.BrushMode == ETerraDyneBrushMode::Flatten)
	{
		ATerraDyneChunk* Chunk = Manager->GetChunkAtLocation(WorldLocation);
		const float MaxFlattenDelta = Settings ? Settings->MaxFlattenHeightDelta : 10000.0f;
		if (!Chunk || FMath::Abs(
			Params.FlattenHeight - Chunk->GetHeightAtLocation(WorldLocation - Chunk->GetActorLocation())) > MaxFlattenDelta)
		{
			OutReason = TEXT("flatten target exceeds the server height-delta limit");
			return false;
		}
	}

	return true;
}

void ATerraDyneEditController::Server_ApplyBrush_Implementation(const FTerraDyneBrushParams& Params)
{
	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr;
	FString RejectionReason;
	if (!ValidateRemoteBrushRequest(Params, Manager, RejectionReason))
	{
		LogRejectedBrushRequest(RejectionReason);
		return;
	}

	if (Params.bIsStrokeStart)
	{
		Manager->BeginStroke(Params.WorldLocation, Params.Radius, this);
	}

	Manager->ApplyGlobalBrush(
		Params.WorldLocation,
		Params.Radius,
		Params.Strength,
		Params.BrushMode,
		Params.TargetLayer,
		Params.WeightLayerIndex,
		Params.FlattenHeight);
	Manager->Multicast_ApplyBrush(Params);
}

void ATerraDyneEditController::Server_CommitStroke_Implementation()
{
	if (!bAllowRemoteTerrainEditing || !ConsumeBrushRequestToken())
	{
		return;
	}

	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
	{
		Manager->CommitStroke(this);
	}
}

void ATerraDyneEditController::Server_Undo_Implementation()
{
	if (!bAllowRemoteTerrainEditing || !ConsumeBrushRequestToken())
	{
		return;
	}

	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
	{
		Manager->Undo(this);
	}
}

void ATerraDyneEditController::Server_Redo_Implementation()
{
	if (!bAllowRemoteTerrainEditing || !ConsumeBrushRequestToken())
	{
		return;
	}

	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	if (ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr)
	{
		Manager->Redo(this);
	}
}

void ATerraDyneEditController::Client_ReceiveChunkSync(const FTerraDyneChunkData& Data)
{
	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr;
	if (!Manager)
	{
		PendingChunkSyncs.Add(Data.Coordinate, Data);
		return;
	}

	ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(Data.Coordinate);
	if (Chunk)
	{
		Chunk->LoadFromData(Data);
		PendingChunkSyncs.Remove(Data.Coordinate);
		UE_LOG(LogTerraDyne, Log, TEXT("Client chunk sync: updated chunk [%d,%d]"),
			Data.Coordinate.X, Data.Coordinate.Y);
	}
	else
	{
		PendingChunkSyncs.Add(Data.Coordinate, Data);
		UE_LOG(LogTerraDyne, Warning, TEXT("Client chunk sync: chunk [%d,%d] not ready yet, queued for retry."),
			Data.Coordinate.X, Data.Coordinate.Y);
	}
}

void ATerraDyneEditController::OnMouseWheel(float Val)
{
	if (!bEnablePlayModeToolUI)
	{
		return;
	}

	if (Val != 0.0f)
	{
		BrushRadius = FMath::Clamp(BrushRadius + Val * 200.0f, 100.0f, 10000.0f);
	}
}

bool ATerraDyneEditController::GetTerrainHit(FHitResult& OutHit)
{
	FVector WorldLoc, WorldDir;
	if (!DeprojectMousePositionToWorld(WorldLoc, WorldDir))
	{
		return false;
	}
	
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	Params.bReturnFaceIndex = false;
	
	// Ignore player pawn
	if (APawn* MyPawn = GetPawn())
	{
		Params.AddIgnoredActor(MyPawn);
	}
	
	// Try WorldStatic first (terrain chunks)
	if (GetWorld()->LineTraceSingleByChannel(OutHit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_WorldStatic, Params))
	{
		return true;
	}
	
	// Fallback to Visibility
	if (GetWorld()->LineTraceSingleByChannel(OutHit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_Visibility, Params))
	{
		return true;
	}
	
	// Last resort - try Camera channel
	if (GetWorld()->LineTraceSingleByChannel(OutHit, WorldLoc, WorldLoc + WorldDir * 200000.0f, ECC_Camera, Params))
	{
		return true;
	}
	
	return false;
}

void ATerraDyneEditController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	ATerraDyneManager* Manager = Sys ? Sys->GetTerrainManager() : nullptr;
	if (Manager && PendingChunkSyncs.Num() > 0)
	{
		TArray<FIntPoint> ResolvedCoords;
		for (const auto& Pair : PendingChunkSyncs)
		{
			if (ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(Pair.Key))
			{
				Chunk->LoadFromData(Pair.Value);
				ResolvedCoords.Add(Pair.Key);
			}
		}

		for (const FIntPoint& Coord : ResolvedCoords)
		{
			PendingChunkSyncs.Remove(Coord);
		}
	}

	if (!bEnablePlayModeToolUI)
	{
		if (BrushDecal)
		{
			BrushDecal->SetVisibility(false);
		}
		return;
	}
	
	APawn* MyPawn = GetPawn();
	if (!MyPawn) return;
	
	FHitResult Hit;
	bool bHit = GetTerrainHit(Hit);

	// Get player location for reference
	FVector PlayerLoc = MyPawn->GetActorLocation();

	// Read current tool settings from UI (sync with UI)
	ETerraDyneToolMode ToolMode = CurrentTool;
	float CurrentRadius = BrushRadius;
	if (ActiveUI)
	{
		ToolMode = ActiveUI->CurrentTool;
		CurrentRadius = ActiveUI->BrushRadius;
	}

	// Update brush preview decal
	if (BrushDecal)
	{
		if (bHit)
		{
			BrushDecal->SetWorldLocation(Hit.Location);
			BrushDecal->DecalSize = FVector(10000.0f, CurrentRadius, CurrentRadius);
			BrushDecal->SetVisibility(true);

			if (BrushDecalMID)
			{
				BrushDecalMID->SetScalarParameterValue(TEXT("Radius"), CurrentRadius);
				// Color by tool mode: orange for paint, blue for sculpt
				FLinearColor BrushColor = (ToolMode == ETerraDyneToolMode::Paint)
					? FLinearColor(0.8f, 0.5f, 0.2f, 1.0f)
					: FLinearColor(0.25f, 0.5f, 1.0f, 1.0f);
				BrushDecalMID->SetVectorParameterValue(TEXT("Color"), BrushColor);
			}
		}
		else
		{
			BrushDecal->SetVisibility(false);
		}
	}
	
	if (bHit)
	{
		LastValidHitLocation = Hit.Location;
		
		if (bShowDebugCursor)
		{
			// Determine cursor color based on tool
			FColor CursorColor = FColor::Green;
			switch (ToolMode)
			{
			case ETerraDyneToolMode::SculptLower:
				CursorColor = FColor::Red;
				break;
			case ETerraDyneToolMode::Flatten:
				CursorColor = FColor::Blue;
				break;
			case ETerraDyneToolMode::Smooth:
				CursorColor = FColor::Yellow;
				break;
			case ETerraDyneToolMode::Paint:
				CursorColor = FColor::Purple;
				break;
			default:
				CursorColor = FColor::Green;
			}
			
			// MAIN CURSOR - At hit location on terrain (using UI radius)
			DrawDebugSphere(GetWorld(), Hit.Location, CurrentRadius, 32, CursorColor, false, -1.0f, 0, 3.0f);
			DrawDebugPoint(GetWorld(), Hit.Location, 10.0f, FColor::White, false, -1.0f);
		}
		
		// Apply tool if clicking
		if (bIsClicking)
		{
			PerformToolAction(Hit.Location);
		}
	}
	else if (bShowDebugCursor)
	{
		// NO HIT - Draw cursor in front of player as fallback
		FVector CameraLoc;
		FRotator CameraRot;
		GetPlayerViewPoint(CameraLoc, CameraRot);
		
		FVector Forward = CameraRot.Vector();
		FVector FallbackLoc = CameraLoc + Forward * 5000.0f;
		
		DrawDebugPoint(GetWorld(), FallbackLoc, 5.0f, FColor::Red, false, -1.0f);
	}
	
	// Always draw player position marker if debugging
	if (bShowDebugCursor)
	{
		// DrawDebugBox(GetWorld(), PlayerLoc, FVector(50,50,50), FColor::Blue, false, 0.0f, 0, 2.0f);
	}
}

void ATerraDyneEditController::PerformToolAction(const FVector& Location)
{
	if (!bEnablePlayModeToolUI)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World) return;
	
	UTerraDyneSubsystem* Sys = World->GetSubsystem<UTerraDyneSubsystem>();
	if (!Sys) return;
	ATerraDyneManager* Manager = Sys->GetTerrainManager();
	if (!Manager) return;

	ETerraDyneToolMode ToolMode = CurrentTool;
	float UseRadius = BrushRadius;
	float UseStrength = BrushStrength;
	int32 LayerIndex = 0;

	if (ActiveUI)
	{
		ToolMode = ActiveUI->CurrentTool;
		UseRadius = ActiveUI->BrushRadius;
		UseStrength = ActiveUI->BrushStrength * 2500.0f;
		LayerIndex = ActiveUI->ActiveLayerIndex;
	}

	// Map ETerraDyneToolMode → ETerraDyneBrushMode
	ETerraDyneBrushMode BrushMode = ETerraDyneBrushMode::Raise;
	float FlattenHeight = 0.f;
	ETerraDyneLayer TargetLayer = Manager->ActiveLayer;

	switch (ToolMode)
	{
	case ETerraDyneToolMode::SculptRaise:
		BrushMode = ETerraDyneBrushMode::Raise;
		break;
	case ETerraDyneToolMode::SculptLower:
		BrushMode = ETerraDyneBrushMode::Lower;
		break;
	case ETerraDyneToolMode::Smooth:
		BrushMode = ETerraDyneBrushMode::Smooth;
		break;
	case ETerraDyneToolMode::Flatten:
		BrushMode = ETerraDyneBrushMode::Flatten;
		// Lock target height on first click of this stroke
		if (!bFlattenHeightLocked)
		{
			if (ATerraDyneChunk* Chunk = Manager->GetChunkAtLocation(Location))
			{
				FVector RelPos = Location - Chunk->GetActorLocation();
				LockedFlattenHeight = Chunk->GetHeightAtLocation(RelPos);
			}
			bFlattenHeightLocked = true;
		}
		FlattenHeight = LockedFlattenHeight;
		break;
	case ETerraDyneToolMode::Paint:
		BrushMode = ETerraDyneBrushMode::Paint;
		break;
	default:
		BrushMode = ETerraDyneBrushMode::Raise;
		break;
	}

	const bool bFirstClick = !bStrokeBegun;
	if (bFirstClick)
	{
		bStrokeBegun = true;
	}

	FTerraDyneBrushParams Params;
	Params.WorldLocation = Location;
	Params.Radius = UseRadius;
	Params.Strength = UseStrength;
	Params.BrushMode = BrushMode;
	Params.TargetLayer = TargetLayer;
	Params.WeightLayerIndex = LayerIndex;
	Params.FlattenHeight = FlattenHeight;
	Params.bIsStrokeStart = bFirstClick;

	if (HasAuthority())
	{
		// Listen-server / standalone: apply directly + multicast to clients
		if (bFirstClick)
		{
			Manager->BeginStroke(Location, UseRadius, this);
		}
		Manager->ApplyGlobalBrush(Location, UseRadius, UseStrength, BrushMode, TargetLayer, LayerIndex, FlattenHeight);
		Manager->Multicast_ApplyBrush(Params);
	}
	else
	{
		// Client: send to server via RPC
		Server_ApplyBrush(Params);
	}
}
