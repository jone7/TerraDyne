// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "TerraDyneToolWidget.generated.h"

UENUM(BlueprintType)
enum class ETerraDyneToolMode : uint8
{
	SculptRaise,
	SculptLower,
	Flatten,
	Smooth,
	Paint
};

/**
 * Native logic for the Runtime Level Editor UI.
 * Acts as a host for the Slate STerraDynePanel.
 */
UCLASS()
class TERRADYNE_API UTerraDyneToolWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	//--- State Variables ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	ETerraDyneToolMode CurrentTool;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	float BrushRadius = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	float BrushStrength = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerraDyne|State")
	int32 ActiveLayerIndex = 0;

public:
	//--- Native Overrides ---
	virtual void NativeConstruct() override;
	virtual TSharedRef<SWidget> RebuildWidget() override;

	//--- Tool Handlers ---
	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Actions")
	void SaveWorld();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Actions")
	void ResetTerrain();

	UFUNCTION(BlueprintCallable, Category = "TerraDyne|Actions")
	void ResetActiveLayer();

private:
	TSharedPtr<class STerraDynePanel> MainPanel;
};
