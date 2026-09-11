// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/TerraDyneManager.h"
#include "TerraDyneSceneSetup.generated.h"

// Forward Declarations
class UTerraDyneWorldPreset;
class UTerraDyneLandscapeAssetSet;

UENUM(BlueprintType)
enum class ETerraDyneDemoTemplate : uint8
{
	FullFeatureShowcase UMETA(DisplayName = "Full Feature Showcase"),
	Sandbox UMETA(DisplayName = "Sandbox"),
	SurvivalFramework UMETA(DisplayName = "Survival Framework"),
	AuthoredWorldConversion UMETA(DisplayName = "Authored World Conversion")
};

/**
 * A helper tool for ons-boarding. 
 * Drastically simplifies setting up a new TerraDyne level.
 */
UCLASS(Blueprintable)
class TERRADYNE_API ATerraDyneSceneSetup : public AActor
{
	GENERATED_BODY()
	
public:	
	ATerraDyneSceneSetup();

	//--- Configuration Assets ---//
	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	TSubclassOf<ATerraDyneManager> ManagerClass;

	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	TObjectPtr<UTerraDyneWorldPreset> WorldPreset;

	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	ETerraDyneDemoTemplate DemoTemplate = ETerraDyneDemoTemplate::SurvivalFramework;

	// Forwarded to the spawned Manager when the AuthoredWorldConversion template runs.
	UPROPERTY(EditAnywhere, Category = "TerraDyne Setup")
	FTerraDyneLandscapeMigrationOptions MigrationOptions;

	/** Runtime-safe authored-world payload generated from the editor conversion flow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne Setup")
	TObjectPtr<UTerraDyneLandscapeAssetSet> AuthoredWorldAssetSet;

	/** When true, the authored conversion path prefers the baked asset set over live editor-only import. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne Setup")
	bool bPreferBakedLandscapeData = true;

	//--- Actions ---//
	
	// The "One Button Solution"
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "TerraDyne Setup")
	void InitializeWorld(class ALandscapeProxy* TargetLandscape = nullptr);

	UFUNCTION(BlueprintPure, Category = "TerraDyne Setup")
	bool HasRuntimeAuthoredWorldData() const { return AuthoredWorldAssetSet != nullptr; }

};
