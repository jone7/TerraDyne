// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "STerraDynePanel.h"
#include "UI/TerraDyneToolWidget.h"
#include "TerraDyneModule.h"
#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneEditController.h"
#include "World/TerraDyneOrchestrator.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Colors/SColorBlock.h"

#define LOCTEXT_NAMESPACE "TerraDyneUI"

using namespace TerraDynePanelConstants;

void STerraDynePanel::Construct(const FArguments& InArgs)
{
	OwnerWidget = InArgs._ToolWidget;

	// Define some simple styles locally
	FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 10);
	FSlateColor TitleColor = FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
	FSlateColor BGColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.9f);
	FSlateColor SectionBGColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.8f);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>(this, &STerraDynePanel::GetWindowPadding))
		.VAlign(VAlign_Top)
		.HAlign(HAlign_Left) // Aligned to the left for correct drag math
		[
			SNew(SBox)
			.WidthOverride(PanelWidth)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.85f)) // Darker, slightly more transparent
				.Padding(12.0f)
				[
					SNew(SVerticalBox)
					
					//--- HEADER (Draggable Area) ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 10)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f, 0.9f)) // Header background
						.Padding(FMargin(10.0f, 6.0f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Title", "TERRADYNE"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
								.ColorAndOpacity(TitleColor)
								.ShadowOffset(FVector2D(1.0f, 1.0f))
								.ShadowColorAndOpacity(FLinearColor::Black)
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(SSpacer)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetVersionText)
								.Font(LabelFont)
								.ColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f))
							]
						]
					]

					//--- UNDO / REDO SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("BtnUndo", "Undo"))
								.ToolTipText(LOCTEXT("TipUndo", "Undo the last sculpt or paint stroke."))
								.OnClicked(this, &STerraDynePanel::OnUndoClicked)
								.IsEnabled(this, &STerraDynePanel::IsUndoEnabled)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("BtnRedo", "Redo"))
								.ToolTipText(LOCTEXT("TipRedo", "Redo the previously undone stroke."))
								.OnClicked(this, &STerraDynePanel::OnRedoClicked)
								.IsEnabled(this, &STerraDynePanel::IsRedoEnabled)
								.HAlign(HAlign_Center)
							]
						]
					]

					//--- ACTIONS SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("BtnSave", "Save World"))
								.ToolTipText(LOCTEXT("TipSave", "Save the current terrain state to a save slot."))
								.OnClicked(this, &STerraDynePanel::OnSaveWorldClicked)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("BtnResetAll", "Reset Terrain"))
								.ToolTipText(LOCTEXT("TipResetAll", "Reset all chunks to flat terrain. This cannot be undone."))
								.OnClicked(this, &STerraDynePanel::OnResetTerrainClicked)
								.HAlign(HAlign_Center)
							]
						]
					]

					//--- SCULPTING SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("SculptHeader", "SCULPTING")).Font(HeaderFont)
							]
							
							// Tool Modes — Row 1: Raise / Lower / Smooth
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeRaise", "Raise"))
								.ToolTipText(LOCTEXT("TipRaise", "Sculpt the terrain upwards. Left click and drag."))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptRaise); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptRaise)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeLower", "Lower"))
								.ToolTipText(LOCTEXT("TipLower", "Sculpt the terrain downwards. Left click and drag."))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::SculptLower); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::SculptLower)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeSmooth", "Smooth"))
								.ToolTipText(LOCTEXT("TipSmooth", "Soften sharp terrain features by averaging vertex heights."))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Smooth); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Smooth)
								.HAlign(HAlign_Center)
							]
						]
						// Tool Modes — Row 2: Flatten / Paint
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModeFlatten", "Flatten"))
								.ToolTipText(LOCTEXT("TipFlatten", "Level the terrain to the height of your initial click."))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Flatten); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Flatten)
								.HAlign(HAlign_Center)
							]
							+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
							[
								SNew(SButton)
								.Text(LOCTEXT("ModePaint", "Paint"))
								.ToolTipText(LOCTEXT("TipPaint", "Paint surface weight masks directly onto the terrain."))
								.OnClicked_Lambda([this](){ return SetToolMode(ETerraDyneToolMode::Paint); })
								.ButtonColorAndOpacity(this, &STerraDynePanel::GetToolModeColor, ETerraDyneToolMode::Paint)
								.HAlign(HAlign_Center)
							]
						]
						// Paint Layer Picker (visible only in Paint mode)
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 2)
						[
							SNew(SBox)
							.Visibility(this, &STerraDynePanel::GetPaintSectionVisibility)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(STextBlock).Text(LOCTEXT("PaintLayerHeader", "PAINT LAYER")).Font(LabelFont).ColorAndOpacity(FLinearColor(0.8f, 0.5f, 0.2f))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
									[
										SNew(SButton)
										.Text(LOCTEXT("PaintLayer0", "0"))
										.OnClicked_Lambda([this](){ return SetPaintLayer(0); })
										.ButtonColorAndOpacity(this, &STerraDynePanel::GetPaintLayerColor, 0)
										.HAlign(HAlign_Center)
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
									[
										SNew(SButton)
										.Text(LOCTEXT("PaintLayer1", "1"))
										.OnClicked_Lambda([this](){ return SetPaintLayer(1); })
										.ButtonColorAndOpacity(this, &STerraDynePanel::GetPaintLayerColor, 1)
										.HAlign(HAlign_Center)
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
									[
										SNew(SButton)
										.Text(LOCTEXT("PaintLayer2", "2"))
										.OnClicked_Lambda([this](){ return SetPaintLayer(2); })
										.ButtonColorAndOpacity(this, &STerraDynePanel::GetPaintLayerColor, 2)
										.HAlign(HAlign_Center)
									]
									+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
									[
										SNew(SButton)
										.Text(LOCTEXT("PaintLayer3", "3"))
										.OnClicked_Lambda([this](){ return SetPaintLayer(3); })
										.ButtonColorAndOpacity(this, &STerraDynePanel::GetPaintLayerColor, 3)
										.HAlign(HAlign_Center)
									]
								]
							]
						]

						// Dynamic Layers
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 5)
							[
								SNew(STextBlock).Text(LOCTEXT("LayerHeader", "DYNAMIC LAYERS")).Font(LabelFont).ColorAndOpacity(FLinearColor::Gray)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerBase", "Base"))
									.ToolTipText(LOCTEXT("TipBase", "The foundational terrain layer (usually procedurally generated)."))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Base); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Base)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerSculpt", "Sculpt"))
									.ToolTipText(LOCTEXT("TipSculpt", "The sculpting layer for broad height modifications."))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Sculpt); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Sculpt)
									.HAlign(HAlign_Center)
								]
								+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(2)
								[
									SNew(SButton)
									.Text(LOCTEXT("LayerDetail", "Detail"))
									.ToolTipText(LOCTEXT("TipDetail", "The detail layer for fine, high-frequency noise and erosion adjustments."))
									.OnClicked_Lambda([this](){ return SetActiveLayer(ETerraDyneLayer::Detail); })
									.ButtonColorAndOpacity(this, &STerraDynePanel::GetLayerColor, ETerraDyneLayer::Detail)
									.HAlign(HAlign_Center)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SButton)
								.Text(LOCTEXT("BtnResetLayer", "Reset Active Layer"))
								.ToolTipText(LOCTEXT("TipResetLayer", "Clear all height modifications on the currently selected layer."))
								.OnClicked(this, &STerraDynePanel::ResetActiveLayer)
								.HAlign(HAlign_Center)
							]

							// Sliders
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock).Text(LOCTEXT("LblRadius", "Radius: "))
										.ToolTipText(LOCTEXT("TipRadius", "Adjust the size of the sculpt/paint brush."))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock)
										.Text(this, &STerraDynePanel::GetBrushRadiusText)
										.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SSlider)
									.ToolTipText(LOCTEXT("TipRadiusSlider", "Adjust the size of the sculpt/paint brush."))
									.MinValue(SliderRadiusMin)
									.MaxValue(SliderRadiusMax)
									.Value(this, &STerraDynePanel::GetBrushRadius)
									.OnValueChanged(this, &STerraDynePanel::OnBrushRadiusChanged)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock).Text(LOCTEXT("LblStrength", "Strength: "))
										.ToolTipText(LOCTEXT("TipStrength", "Adjust the intensity of the sculpt/paint brush."))
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(STextBlock)
										.Text(this, &STerraDynePanel::GetBrushStrengthText)
										.ColorAndOpacity(FLinearColor(0.25f, 0.5f, 1.0f))
									]
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SSlider)
									.ToolTipText(LOCTEXT("TipStrengthSlider", "Adjust the intensity of the sculpt/paint brush."))
									.MinValue(SliderStrengthMin)
									.MaxValue(SliderStrengthMax)
									.Value(this, &STerraDynePanel::GetBrushStrength)
									.OnValueChanged(this, &STerraDynePanel::OnBrushStrengthChanged)
								]
							]
						]
					]

					//--- TELEMETRY SECTION ---
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 5)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(SectionBGColor)
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock).Text(LOCTEXT("StatsHeader", "TELEMETRY")).Font(HeaderFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetStatsText)
								.Font(LabelFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
							[
								SNew(STextBlock)
								.Text(this, &STerraDynePanel::GetGPUStatsText)
								.ColorAndOpacity(this, &STerraDynePanel::GetGPUStatusColor)
								.Font(LabelFont)
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 5)
							[
								SNew(SButton)
								.HAlign(HAlign_Center)
								.Text(LOCTEXT("DebugOverlay", "Toggle Debug Overlay"))
								.ToolTipText(LOCTEXT("TipDebugOverlay", "Toggle chunk boundary visualization with color-coded streaming state."))
								.OnClicked(this, &STerraDynePanel::OnToggleDebugOverlay)
							]
						]
					]
				]
			]
		]
	];
}

void STerraDynePanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!bHasInitializedPosition)
	{
		bHasInitializedPosition = true;
		FVector2D ViewSize = AllottedGeometry.GetLocalSize();
		// Place panel on the right side initially
		WindowPosition = FVector2D(FMath::Max(0.0f, ViewSize.X - PanelWidth), HeaderHeight);
	}
}

FReply STerraDynePanel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// Simple hit test: if within top 50px relative to window position
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D RelativeMouse = LocalMouse - WindowPosition;

		if (RelativeMouse.X >= 0 && RelativeMouse.X <= PanelWidth && RelativeMouse.Y >= 0 && RelativeMouse.Y <= HeaderHeight)
		{
			bIsDragging = true;
			DragOffset = RelativeMouse;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging)
	{
		FVector2D LocalMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		FVector2D NewPos = LocalMouse - DragOffset;

		// Clamp so the panel stays within the viewport
		FVector2D ViewSize = MyGeometry.GetLocalSize();
		NewPos.X = FMath::Clamp(NewPos.X, 0.0f, FMath::Max(0.0f, ViewSize.X - PanelWidth));
		NewPos.Y = FMath::Clamp(NewPos.Y, 0.0f, FMath::Max(0.0f, ViewSize.Y - HeaderHeight));
		WindowPosition = NewPos;

		return FReply::Handled();
	}
	return FReply::Unhandled();
}

//--- Helpers ---
ATerraDyneOrchestrator* STerraDynePanel::GetOrchestrator() const
{
	if (OwnerWidget.IsValid())
	{
		return Cast<ATerraDyneOrchestrator>(UGameplayStatics::GetActorOfClass(OwnerWidget->GetWorld(), ATerraDyneOrchestrator::StaticClass()));
	}
	return nullptr;
}

ATerraDyneManager* STerraDynePanel::GetManager() const
{
	if (OwnerWidget.IsValid())
	{
		if (UWorld* World = OwnerWidget->GetWorld())
		{
			// Assuming Manager is a singleton or accessible via Subsystem, but looking for Actor for now
			return Cast<ATerraDyneManager>(UGameplayStatics::GetActorOfClass(World, ATerraDyneManager::StaticClass()));
		}
	}
	return nullptr;
}

//--- Callbacks ---

FText STerraDynePanel::GetBrushRadiusText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("RadiusFmt", "{0}m"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushRadius / RadiusDisplayScale)));
	}
	return LOCTEXT("RadiusNA", "--");
}

FText STerraDynePanel::GetBrushStrengthText() const
{
	if (OwnerWidget.IsValid())
	{
		return FText::Format(LOCTEXT("StrengthFmt", "{0}%"), FText::AsNumber(FMath::RoundToInt(OwnerWidget->BrushStrength * StrengthDisplayScale)));
	}
	return LOCTEXT("StrengthNA", "--");
}

FReply STerraDynePanel::OnSaveWorldClicked()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->SaveWorld();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnResetTerrainClicked()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ResetTerrain();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnToggleDebugOverlay()
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->bShowDebugOverlay = !Mgr->bShowDebugOverlay;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FText STerraDynePanel::GetVersionText() const
{
	return FText::FromString(FString::Printf(TEXT("v%s"), TERRADYNE_VERSION_STRING));
}

FReply STerraDynePanel::SetActiveLayer(ETerraDyneLayer NewLayer)
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->ActiveLayer = NewLayer;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor STerraDynePanel::GetLayerColor(ETerraDyneLayer Layer) const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->ActiveLayer == Layer)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
		}
	}
	return FLinearColor::Gray;
}

FReply STerraDynePanel::ResetActiveLayer()
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ResetActiveLayer();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::SetToolMode(ETerraDyneToolMode NewMode)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->CurrentTool = NewMode;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor STerraDynePanel::GetToolModeColor(ETerraDyneToolMode Mode) const
{
	if (OwnerWidget.IsValid())
	{
		if (OwnerWidget->CurrentTool == Mode)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // #4080FF
		}
	}
	return FLinearColor::Gray;
}

float STerraDynePanel::GetBrushRadius() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->BrushRadius : 2000.0f;
}

void STerraDynePanel::OnBrushRadiusChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushRadius = FMath::Clamp(NewValue, SliderRadiusMin, SliderRadiusMax);
	}
}

float STerraDynePanel::GetBrushStrength() const
{
	return OwnerWidget.IsValid() ? OwnerWidget->BrushStrength : 0.5f;
}

void STerraDynePanel::OnBrushStrengthChanged(float NewValue)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->BrushStrength = FMath::Clamp(NewValue, SliderStrengthMin, SliderStrengthMax);
	}
}

FText STerraDynePanel::GetStatsText() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		return FText::Format(LOCTEXT("StatsFmt", "Chunks: {0} | Verts: {1}"), 
			FText::AsNumber(Mgr->GetActiveChunkCount()), 
			FText::AsNumber(Mgr->GetTotalVertexCount()));
	}
	return LOCTEXT("StatsNA", "Terrain Manager Not Found");
}

FText STerraDynePanel::GetGPUStatsText() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		FTerraDyneGPUStats Stats = Mgr->GetGPUStats();
		return FText::Format(LOCTEXT("GPUStatsFmt", "{0}\n{1}"), 
			FText::FromString(Stats.ComputeBackend),
			FText::FromString(Stats.AdapterName));
	}
	return FText::GetEmpty();
}

FSlateColor STerraDynePanel::GetGPUStatusColor() const
{
	if (ATerraDyneManager* Mgr = GetManager())
	{
		if (Mgr->GetGPUStats().bIsCudaAvailable)
		{
			return FLinearColor(0.25f, 0.5f, 1.0f); // Blue for hybrid RT-assisted path
		}
	}
	return FLinearColor::Gray;
}

FReply STerraDynePanel::SetPaintLayer(int32 LayerIndex)
{
	if (OwnerWidget.IsValid())
	{
		OwnerWidget->ActiveLayerIndex = FMath::Clamp(LayerIndex, 0, MaxPaintLayer);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FSlateColor STerraDynePanel::GetPaintLayerColor(int32 LayerIndex) const
{
	if (OwnerWidget.IsValid() && OwnerWidget->ActiveLayerIndex == LayerIndex)
	{
		return FLinearColor(0.8f, 0.5f, 0.2f); // Orange for active paint layer
	}
	return FLinearColor::Gray;
}

EVisibility STerraDynePanel::GetPaintSectionVisibility() const
{
	if (OwnerWidget.IsValid() && OwnerWidget->CurrentTool == ETerraDyneToolMode::Paint)
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

bool STerraDynePanel::IsUndoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			return Mgr->CanUndo(PC);
		}
	}
	return false;
}

bool STerraDynePanel::IsRedoEnabled() const
{
	if (!OwnerWidget.IsValid()) return false;
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ATerraDyneManager* Mgr = GetManager())
		{
			return Mgr->CanRedo(PC);
		}
	}
	return false;
}

FReply STerraDynePanel::OnUndoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return FReply::Unhandled();

	if (ATerraDyneEditController* EC = Cast<ATerraDyneEditController>(PC))
	{
		EC->OnUndoPressed();
		return FReply::Handled();
	}

	// Fallback: call Manager directly when controller isn't ATerraDyneEditController
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->Undo(PC);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STerraDynePanel::OnRedoClicked()
{
	if (!OwnerWidget.IsValid()) return FReply::Unhandled();
	UWorld* World = OwnerWidget->GetWorld();
	if (!World) return FReply::Unhandled();
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return FReply::Unhandled();

	if (ATerraDyneEditController* EC = Cast<ATerraDyneEditController>(PC))
	{
		EC->OnRedoPressed();
		return FReply::Handled();
	}

	// Fallback: call Manager directly when controller isn't ATerraDyneEditController
	if (ATerraDyneManager* Mgr = GetManager())
	{
		Mgr->Redo(PC);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
