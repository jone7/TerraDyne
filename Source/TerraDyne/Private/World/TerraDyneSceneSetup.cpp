// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneSceneSetup.h"
#include "TerraDyneModule.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneWorldPreset.h"
#include "Grass/TerraDyneGrassTypes.h"
#include "World/TerraDyneLandscapeAssetSet.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneOrchestrator.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "GameFramework/Actor.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#endif

namespace
{
	static constexpr TCHAR DefaultStarterWorldPresetPath[] =
		TEXT("/TerraDyne/Samples/Presets/DA_TerraDyne_ShowcaseWorld.DA_TerraDyne_ShowcaseWorld");
	static constexpr TCHAR DefaultStarterGrassProfilePath[] =
		TEXT("/TerraDyne/Samples/Profiles/DA_TerraDyne_ShowcaseGrass.DA_TerraDyne_ShowcaseGrass");

	static UTerraDyneWorldPreset* LoadDefaultStarterWorldPreset()
	{
		return LoadObject<UTerraDyneWorldPreset>(nullptr, DefaultStarterWorldPresetPath);
	}

	static UTerraDyneGrassProfile* LoadDefaultStarterGrassProfile()
	{
		return LoadObject<UTerraDyneGrassProfile>(nullptr, DefaultStarterGrassProfilePath);
	}

	static void SetActorLabelIfSupported(AActor* Actor, const TCHAR* Label)
	{
#if WITH_EDITOR
		if (Actor && Label)
		{
			Actor->SetActorLabel(Label);
		}
#else
		(void)Actor;
		(void)Label;
#endif
	}

	static void HideLandscapeActorIfSupported(ALandscapeProxy* Landscape)
	{
		if (!Landscape)
		{
			return;
		}

		Landscape->SetActorHiddenInGame(true);
#if WITH_EDITOR
		Landscape->SetIsTemporarilyHiddenInEditor(true);
#endif
	}
}

ATerraDyneSceneSetup::ATerraDyneSceneSetup()
{
	PrimaryActorTick.bCanEverTick = false;
	ManagerClass = ATerraDyneManager::StaticClass();
	DemoTemplate = ETerraDyneDemoTemplate::SurvivalFramework;
	bPreferBakedLandscapeData = true;
}

void ATerraDyneSceneSetup::InitializeWorld(ALandscapeProxy* TargetLandscape)
{
	UWorld* World = GetWorld();
	if (!World) return;

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne Wizard: Starting One-Click Showcase Setup..."));
	UTerraDyneWorldPreset* EffectiveWorldPreset = WorldPreset.Get() ? WorldPreset.Get() : LoadDefaultStarterWorldPreset();
	UTerraDyneGrassProfile* EffectiveGrassProfile = LoadDefaultStarterGrassProfile();

	// 1. Clean old Managers/Orchestrators
	TArray<AActor*> OldActors;
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneManager::StaticClass(), OldActors);
	UGameplayStatics::GetAllActorsOfClass(World, ATerraDyneOrchestrator::StaticClass(), OldActors);
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("TerraDyne: Initialize World")));
#endif
	for (AActor* Act : OldActors)
	{
		if (!Act)
		{
			continue;
		}

		World->DestroyActor(Act);
	}

	// 2. Spawn Manager at Origin
	FActorSpawnParameters Params;
	UClass* ClassToSpawn = ManagerClass.Get() ? ManagerClass.Get() : ATerraDyneManager::StaticClass();

	ATerraDyneManager* Manager = World->SpawnActor<ATerraDyneManager>(
		ClassToSpawn,
		FVector::ZeroVector, FRotator::ZeroRotator, Params
	);

	if (Manager)
	{
		SetActorLabelIfSupported(Manager, TEXT("TerraDyne_System_Manager"));

		Manager->ActiveLayer = ETerraDyneLayer::Sculpt;
		Manager->GlobalChunkSize = 10000.0f;
		Manager->WorldPreset = EffectiveWorldPreset;
		if (EffectiveWorldPreset)
		{
			Manager->ApplyWorldPreset(EffectiveWorldPreset);
		}
		if (!Manager->ActiveGrassProfile && EffectiveGrassProfile)
		{
			Manager->ActiveGrassProfile = EffectiveGrassProfile;
		}

		switch (DemoTemplate)
		{
		case ETerraDyneDemoTemplate::FullFeatureShowcase:
			Manager->bSpawnDefaultChunksOnBeginPlay = false;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = true;
			break;

		case ETerraDyneDemoTemplate::SurvivalFramework:
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;

			// Survival/exploration expects an open world that streams as the player travels,
			// not a 21x21 chunk box.
			Manager->ProceduralWorldSettings.bEnableInfiniteEdgeGrowth = true;

			// Wire up a basic two-biome split when no preset was available, so the
			// framework concept is visibly active out of the box.
			if (Manager->BiomeOverlays.Num() == 0)
			{
				FTerraDyneBiomeOverlay Lowlands;
				Lowlands.BiomeTag = TEXT("Lowlands");
				Lowlands.bApplyToAuthoredChunks = false;
				Lowlands.bApplyToProceduralChunks = true;
				Lowlands.ProceduralNoiseMin = 0.0f;
				Lowlands.ProceduralNoiseMax = 0.55f;
				Lowlands.Priority = 4;
				Manager->BiomeOverlays.Add(Lowlands);

				FTerraDyneBiomeOverlay Highlands;
				Highlands.BiomeTag = TEXT("Highlands");
				Highlands.bApplyToAuthoredChunks = false;
				Highlands.bApplyToProceduralChunks = true;
				Highlands.ProceduralNoiseMin = 0.55f;
				Highlands.ProceduralNoiseMax = 1.0f;
				Highlands.Priority = 5;
				Manager->BiomeOverlays.Add(Highlands);
			}
			break;

		case ETerraDyneDemoTemplate::AuthoredWorldConversion:
			Manager->bSpawnDefaultChunksOnBeginPlay = false;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;

			if (bPreferBakedLandscapeData && AuthoredWorldAssetSet)
			{
				if (!Manager->InitializeFromBakedLandscapeAssetSet(AuthoredWorldAssetSet, true))
				{
					UE_LOG(
						LogTerraDyne,
						Warning,
						TEXT("TerraDyne SceneSetup: Failed to initialize from baked authored-world asset set %s."),
						*GetNameSafe(AuthoredWorldAssetSet));
				}
				else if (MigrationOptions.bHideSourceLandscape && TargetLandscape)
				{
					HideLandscapeActorIfSupported(TargetLandscape);
					if (ALandscape* RootLandscape = TargetLandscape->GetLandscapeActor())
					{
						HideLandscapeActorIfSupported(RootLandscape);
					}
				}
			}
			else if (TargetLandscape)
			{
#if WITH_EDITOR
				FScopedSlowTask SlowTask(1.0f, FText::FromString(TEXT("Importing Landscape into TerraDyne...")));
				SlowTask.MakeDialog(true);

				SlowTask.EnterProgressFrame(1.0f);
				Manager->LandscapeMigrationOptions = MigrationOptions;
				Manager->TargetLandscapeSource = TargetLandscape;
				Manager->ImportFromLandscapeWithOptions(TargetLandscape, MigrationOptions);
#else
				UE_LOG(
					LogTerraDyne,
					Warning,
					TEXT("TerraDyne SceneSetup: Authored World Conversion landscape import is editor-only. ")
					TEXT("Packaged builds can use InitializeWorld(), but must rely on pre-imported TerraDyne data instead of live Landscape conversion."));
#endif
			}
			else if (AuthoredWorldAssetSet)
			{
				Manager->InitializeFromBakedLandscapeAssetSet(AuthoredWorldAssetSet, true);
			}
			else
			{
				UE_LOG(
					LogTerraDyne,
					Warning,
					TEXT("TerraDyne SceneSetup: Authored World Conversion needs either a Landscape source or a baked Landscape asset set."));
			}
			break;

		case ETerraDyneDemoTemplate::Sandbox:
		default:
			Manager->ProceduralWorldSettings.bEnableSeededOutskirts = false;
			Manager->ProceduralWorldSettings.bGeneratePopulationFromRules = false;
			Manager->ProceduralWorldSettings.bEnableInfiniteEdgeGrowth = true;
			Manager->bSpawnDefaultChunksOnBeginPlay = true;
			Manager->bSetupDefaultLightingOnBeginPlay = true;
			Manager->bSpawnShowcaseOnBeginPlay = false;
			break;
		}
	}

	// 3. Spawn Orchestrator (The Showcase Director or UI Bootstrapper)
	ATerraDyneOrchestrator* Director = World->SpawnActor<ATerraDyneOrchestrator>(
		ATerraDyneOrchestrator::StaticClass(),
		FVector(0,0,1000), FRotator::ZeroRotator
	);
	if (Director)
	{
		if (DemoTemplate == ETerraDyneDemoTemplate::FullFeatureShowcase)
		{
			SetActorLabelIfSupported(Director, TEXT("TerraDyne_Showcase_Director"));
		}
		else
		{
			SetActorLabelIfSupported(Director, TEXT("TerraDyne_UI_Bootstrapper"));
			Director->bInteractiveOnly = true;
		}
	}

	// 4. Setup Spectacular Lighting
	if (!UGameplayStatics::GetActorOfClass(World, ADirectionalLight::StaticClass()))
	{
		ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(0,0,5000), FRotator(-45, -45, 0));
		if (Sun && Sun->GetLightComponent())
		{
			SetActorLabelIfSupported(Sun, TEXT("TerraDyne_Sun"));
			Sun->GetLightComponent()->SetIntensity(3.0f);
			Sun->SetCastShadows(true);
		}
	}

	if (!UGameplayStatics::GetActorOfClass(World, ASkyLight::StaticClass()))
	{
		ASkyLight* Sky = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Sky && Sky->GetLightComponent())
		{
			SetActorLabelIfSupported(Sky, TEXT("TerraDyne_Sky"));
			Sky->GetLightComponent()->bRealTimeCapture = true;
		}
	}

	if (DemoTemplate == ETerraDyneDemoTemplate::FullFeatureShowcase)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("SHOWCASE READY: Just press PLAY to watch the feature tour."));
	}
	else
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TERRADYNE WORLD READY: Just press PLAY to enter the configured starter world."));
	}
}
