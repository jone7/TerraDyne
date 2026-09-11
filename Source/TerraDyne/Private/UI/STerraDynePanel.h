// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UI/TerraDyneToolWidget.h"
#include "World/TerraDyneTileData.h"

namespace TerraDynePanelConstants
{
	constexpr float PanelWidth = 380.0f;
	constexpr float HeaderHeight = 50.0f;
	constexpr float SliderRadiusMin = 100.0f;
	constexpr float SliderRadiusMax = 10000.0f;
	constexpr float SliderStrengthMin = 0.0f;
	constexpr float SliderStrengthMax = 5.0f;
	constexpr float StrengthDisplayScale = 20.0f; // 0-5 range -> 0-100%
	constexpr float RadiusDisplayScale = 100.0f;  // UU -> meters
	constexpr int32 MaxPaintLayer = 3;
}

/**
 * Native Slate implementation of the TerraDyne Control Panel.
 * Provides a professional, runtime-ready GUI for the plugin.
 */
class STerraDynePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STerraDynePanel) {}
	SLATE_ARGUMENT(TWeakObjectPtr<UTerraDyneToolWidget>, ToolWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	//--- State & References ---
	TWeakObjectPtr<UTerraDyneToolWidget> OwnerWidget;
	
	// Window State
	bool bHasInitializedPosition = false;
	bool bIsDragging = false;
	FVector2D DragOffset;
	FVector2D WindowPosition = FVector2D(50, 50);
	
	// Helper to get the Orchestrator from the World
	class ATerraDyneOrchestrator* GetOrchestrator() const;
	class ATerraDyneManager* GetManager() const;

	//--- UI Callbacks ---
	// Undo / Redo
	FReply OnUndoClicked();
	FReply OnRedoClicked();
	bool IsUndoEnabled() const;
	bool IsRedoEnabled() const;

	// Tools
	FReply SetToolMode(ETerraDyneToolMode NewMode);
	FSlateColor GetToolModeColor(ETerraDyneToolMode Mode) const;
	FReply SetActiveLayer(ETerraDyneLayer NewLayer);
	FSlateColor GetLayerColor(ETerraDyneLayer Layer) const;
	FReply ResetActiveLayer();
	FReply SetPaintLayer(int32 LayerIndex);
	FSlateColor GetPaintLayerColor(int32 LayerIndex) const;
	EVisibility GetPaintSectionVisibility() const;
	float GetBrushRadius() const;
	void OnBrushRadiusChanged(float NewValue);
	FText GetBrushRadiusText() const;
	float GetBrushStrength() const;
	void OnBrushStrengthChanged(float NewValue);
	FText GetBrushStrengthText() const;
	
	// Actions
	FReply OnSaveWorldClicked();
	FReply OnResetTerrainClicked();
	FReply OnToggleDebugOverlay();

	// Visualization
	FText GetVersionText() const;
	FText GetStatsText() const;
	FText GetGPUStatsText() const;
	FSlateColor GetGPUStatusColor() const;

	FMargin GetWindowPadding() const { return FMargin(WindowPosition.X, WindowPosition.Y, 0, 0); }
};
