// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/TerraDyneToolWidget.h"
#include "UI/STerraDynePanel.h"
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneManager.h"
#include "World/TerraDyneChunk.h"
#include "Kismet/GameplayStatics.h"
#include "TerraDyneModule.h"

void UTerraDyneToolWidget::NativeConstruct()
{
	Super::NativeConstruct();
	
	// Initialize default values
	CurrentTool = ETerraDyneToolMode::SculptRaise;
	BrushRadius = 2000.0f;
	BrushStrength = 0.5f;
	ActiveLayerIndex = 0;
	
	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyneToolWidget constructed"));
}

TSharedRef<SWidget> UTerraDyneToolWidget::RebuildWidget()
{
	MainPanel = SNew(STerraDynePanel).ToolWidget(this);
	return MainPanel.ToSharedRef();
}

void UTerraDyneToolWidget::SaveWorld()
{
	if (UWorld* World = GetWorld())
	{
		if (UTerraDyneSubsystem* Subsystem = World->GetSubsystem<UTerraDyneSubsystem>())
		{
			if (ATerraDyneManager* Manager = Subsystem->GetTerrainManager())
			{
				UE_LOG(LogTerraDyne, Log, TEXT("UI: Saving world terrain state..."));
				Manager->SaveWorld();
			}
		}
	}
}

void UTerraDyneToolWidget::ResetTerrain()
{
	TArray<AActor*> Chunks;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), Chunks);

	for (AActor* Actor : Chunks)
	{
		if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
		{
			const int32 Num = Chunk->HeightBuffer.Num();
			if (Chunk->BaseBuffer.Num() != Num || Chunk->SculptBuffer.Num() != Num || Chunk->DetailBuffer.Num() != Num)
			{
				UE_LOG(LogTerraDyne, Warning, TEXT("ResetTerrain: Buffer size mismatch on chunk [%d,%d], skipping."),
					Chunk->GridCoordinate.X, Chunk->GridCoordinate.Y);
				if (UTerraDyneSubsystem* Sys = GetWorld() ? GetWorld()->GetSubsystem<UTerraDyneSubsystem>() : nullptr)
				{
					Sys->ShowNotification(
						FText::Format(NSLOCTEXT("TerraDyne", "BufferMismatch", "Buffer mismatch on chunk [{0},{1}], skipping reset."),
							FText::AsNumber(Chunk->GridCoordinate.X), FText::AsNumber(Chunk->GridCoordinate.Y)),
						ETerraDyneNotifySeverity::Warning);
				}
				continue;
			}
			for (int32 i = 0; i < Num; i++)
			{
				Chunk->BaseBuffer[i]   = 0.5f;
				Chunk->SculptBuffer[i] = 0.0f;
				Chunk->DetailBuffer[i] = 0.0f;
				Chunk->HeightBuffer[i] = 0.5f;
			}
			for (int32 L = 0; L < ATerraDyneChunk::NumWeightLayers; L++)
			{
				Chunk->WeightBuffers[L].Init(0.0f, Num);
			}
			Chunk->UploadWeightTexture();
			Chunk->RebuildPhysicsMesh();
			Chunk->RequestGrassRegen();
		}
	}
	UE_LOG(LogTerraDyne, Warning, TEXT("UI: All Chunks Reset to Flat."));
}

void UTerraDyneToolWidget::ResetActiveLayer()
{
	UWorld* World = GetWorld();
	if (!World) return;

	UTerraDyneSubsystem* Subsystem = World->GetSubsystem<UTerraDyneSubsystem>();
	if (!Subsystem) return;

	ATerraDyneManager* Manager = Subsystem->GetTerrainManager();
	if (!Manager) return;

	ETerraDyneLayer LayerToReset = Manager->ActiveLayer;

	if (LayerToReset == ETerraDyneLayer::Active)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("ResetActiveLayer: ETerraDyneLayer::Active is a selection marker; no data to reset."));
		return;
	}

	TArray<AActor*> Chunks;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), ATerraDyneChunk::StaticClass(), Chunks);

	for (AActor* Actor : Chunks)
	{
		if (ATerraDyneChunk* Chunk = Cast<ATerraDyneChunk>(Actor))
		{
			int32 Num = Chunk->HeightBuffer.Num();
			if (LayerToReset == ETerraDyneLayer::Base)
			{
				for (int32 i = 0; i < Num; i++) Chunk->BaseBuffer[i] = 0.5f;
			}
			else if (LayerToReset == ETerraDyneLayer::Sculpt)
			{
				for (int32 i = 0; i < Num; i++) Chunk->SculptBuffer[i] = 0.0f;
			}
			else if (LayerToReset == ETerraDyneLayer::Detail)
			{
				for (int32 i = 0; i < Num; i++) Chunk->DetailBuffer[i] = 0.0f;
			}

			// Re-sum
			for (int32 i = 0; i < Num; i++)
			{
				Chunk->HeightBuffer[i] = FMath::Clamp(Chunk->BaseBuffer[i] + Chunk->SculptBuffer[i] + Chunk->DetailBuffer[i], 0.0f, 1.0f);
			}
			
			Chunk->RebuildPhysicsMesh();
			Chunk->RequestGrassRegen();
		}
	}
	UE_LOG(LogTerraDyne, Warning, TEXT("UI: Active Layer Reset."));
}
