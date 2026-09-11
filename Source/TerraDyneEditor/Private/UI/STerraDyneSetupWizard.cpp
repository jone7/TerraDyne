// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "UI/STerraDyneSetupWizard.h"
#include "Baking/TerraDyneBaker.h"
#include "TerraDyneEditorModule.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Images/SImage.h"
#include "PropertyCustomizationHelpers.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "LandscapeProxy.h"
#include "EngineUtils.h"
#include "World/TerraDyneLandscapeAssetSet.h"

#define LOCTEXT_NAMESPACE "STerraDyneSetupWizard"

void STerraDyneSetupWizard::Construct(const FArguments& InArgs)
{
	FSlateColor TerraDyneBlue = FSlateColor(FColor::FromHex("4080FF"));

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin("TerraDyne");
	FString PluginDir = Plugin.IsValid() ? Plugin->GetBaseDir() : FPaths::ProjectPluginsDir() / TEXT("TerraDyne");

	FString LogoPath = FPaths::Combine(PluginDir, TEXT("Resources"), TEXT("TerraDyneLogo.jpg"));
	if (FPaths::FileExists(LogoPath))
	{
		LogoBrush = MakeShareable(new FSlateDynamicImageBrush(FName(*LogoPath), FVector2D(512.f, 512.f)));
	}

	FString BannerPath = FPaths::Combine(PluginDir, TEXT("Resources"), TEXT("TerraDyneLine.png"));
	if (FPaths::FileExists(BannerPath))
	{
		BannerBrush = MakeShareable(new FSlateDynamicImageBrush(FName(*BannerPath), FVector2D(1024.f, 256.f)));
	}

	// Initialize Template Options
	TemplateOptions.Add(MakeShared<FString>("Survival Framework (Recommended First Run)"));
	TemplateOptions.Add(MakeShared<FString>("Full Feature Showcase (Cinematic Tour)"));
	TemplateOptions.Add(MakeShared<FString>("Authored World Conversion (Takes over existing landscape)"));
	TemplateOptions.Add(MakeShared<FString>("Sandbox (Blank Canvas)"));
	SelectedTemplate = ETerraDyneDemoTemplate::SurvivalFramework;
	SelectedTemplateOption = TemplateOptions[0];

	// Populate landscapes from the current editor world.
	RefreshLandscapeList();

	// Keep the landscape list in sync when the user opens or changes maps after the wizard is already open.
	MapOpenedHandle = FEditorDelegates::OnMapOpened.AddRaw(this, &STerraDyneSetupWizard::OnEditorMapOpened);
	MapChangeHandle = FEditorDelegates::MapChange.AddRaw(this, &STerraDyneSetupWizard::OnEditorMapChange);

	// Helper to build a checkbox row bound by pointer to a bool member of MigrationOptions.
	auto MakeOptionRow = [this](const FText& Label, const FText& Tooltip, bool* OptionPtr) -> TSharedRef<SHorizontalBox>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([OptionPtr]()
				{
					return (*OptionPtr) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([OptionPtr](ECheckBoxState NewState)
				{
					*OptionPtr = (NewState == ECheckBoxState::Checked);
				})
				.ToolTipText(Tooltip)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.ToolTipText(Tooltip)
				.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
			];
	};

	// Color palette — tinted dark backgrounds give the cards real presence against the editor's grey.
	const FSlateColor CardBgColor      = FSlateColor(FLinearColor(0.020f, 0.024f, 0.034f, 1.0f));
	const FSlateColor HeroBandColor    = FSlateColor(FLinearColor(0.038f, 0.060f, 0.115f, 1.0f));
	const FSlateColor WarningBgColor   = FSlateColor(FLinearColor(0.180f, 0.105f, 0.025f, 1.0f));
	const FSlateColor SeparatorColor   = FSlateColor(FLinearColor(0.090f, 0.130f, 0.220f, 1.0f));

	// Section title: 6px-wide accent stripe + blue label text. Far more readable than bold-grey.
	auto MakeSectionTitle = [TerraDyneBlue](const FText& Title) -> TSharedRef<SHorizontalBox>
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(6.0f)
				.HeightOverride(22.0f)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(TerraDyneBlue)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Title)
				.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
				.ColorAndOpacity(TerraDyneBlue)
			];
	};

	const FMargin CardOuterPadding(24.0f, 12.0f);
	const FMargin CardInnerPadding(18.0f, 14.0f);

	ChildSlot
	[
		SNew(SBorder)
		.Padding(0.0f)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)

			// --- Header Section (blue-tinted hero band) ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(HeroBandColor)
				.Padding(FMargin(24.0f, 28.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 18.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(96.0f)
						.HeightOverride(96.0f)
						[
							SNew(SImage)
							.Image(LogoBrush.IsValid() ? LogoBrush.Get() : FCoreStyle::Get().GetDefaultBrush())
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Left)
						[
							SNew(SImage)
							.Image(BannerBrush.IsValid() ? BannerBrush.Get() : FCoreStyle::Get().GetDefaultBrush())
							.DesiredSizeOverride(FVector2D(240.0f, 60.0f))
						]
						// Slim accent stripe under the banner.
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Left)
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(60.0f)
							.HeightOverride(2.0f)
							[
								SNew(SImage)
								.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
								.ColorAndOpacity(TerraDyneBlue)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SubTitleText", "Zero-Configuration World Initialization"))
							.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]
				]
			]

			// --- Intro / Description ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(CardOuterPadding.Left, 18.0f, CardOuterPadding.Right, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DescText", "Welcome to TerraDyne. This wizard prepares your level in one click - injecting the core Manager, sample world preset, grass profile, Orchestrator, and a default lighting rig. The recommended first run is a survival-ready starter world; switch templates only when you need a focused showcase or authored landscape conversion."))
				.AutoWrapText(true)
				.Font(FAppStyle::GetFontStyle("Heading3"))
				.LineHeightPercentage(1.2f)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

			// --- Card: Choose Your World ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(CardOuterPadding)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(CardBgColor)
				.Padding(CardInnerPadding)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						MakeSectionTitle(LOCTEXT("SectionChooseWorld", "Choose Your World"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("TemplateLabel", "Starting Template:"))
							.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SNew(SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&TemplateOptions)
							.OnGenerateWidget(this, &STerraDyneSetupWizard::MakeTemplateComboWidget)
							.OnSelectionChanged(this, &STerraDyneSetupWizard::OnTemplateSelectionChanged)
							[
								SNew(STextBlock)
								.Text(this, &STerraDyneSetupWizard::GetSelectedTemplateText)
								.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
							]
						]
					]
				]
			]

			// --- Card: Authored World Conversion (visible only when AWC selected) ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(CardOuterPadding.Left, 0.0f, CardOuterPadding.Right, CardOuterPadding.Top)
			[
				SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(CardBgColor)
				.Padding(CardInnerPadding)
				.Visibility(this, &STerraDyneSetupWizard::GetLandscapeConfigVisibility)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						MakeSectionTitle(LOCTEXT("SectionAuthored", "Authored World Conversion"))
					]

					// Row: target landscape combo + refresh
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 6.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LandscapeSourceLabel", "Target Landscape:"))
							.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Center)
						[
							SAssignNew(LandscapeComboBox, SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&LandscapeOptions)
							.OnGenerateWidget(this, &STerraDyneSetupWizard::MakeLandscapeComboWidget)
							.OnSelectionChanged(this, &STerraDyneSetupWizard::OnLandscapeSelectionChanged)
							.Visibility(this, &STerraDyneSetupWizard::GetLandscapeComboVisibility)
							[
								SNew(STextBlock)
								.Text(this, &STerraDyneSetupWizard::GetSelectedLandscapeText)
								.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(8.0f, 0.0f, 0.0f, 0.0f)
						[
							SNew(SButton)
							.Text(LOCTEXT("RefreshLandscapes", "Refresh"))
							.ToolTipText(LOCTEXT("RefreshLandscapesTip", "Re-scan the current level for Landscape actors. Use after adding a Landscape or switching maps."))
							.OnClicked(this, &STerraDyneSetupWizard::OnRefreshLandscapesClicked)
							.ContentPadding(FMargin(12.0f, 4.0f))
							.Cursor(EMouseCursor::Hand)
						]
					]

					// Inline warning shown when no landscapes are detected.
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 8.0f)
					[
						SNew(SBorder)
						.Visibility(this, &STerraDyneSetupWizard::GetNoLandscapeWarningVisibility)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(WarningBgColor)
						.Padding(FMargin(12.0f, 10.0f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 10.0f, 0.0f)
							[
								SNew(SBox)
								.WidthOverride(4.0f)
								.HeightOverride(28.0f)
								[
									SNew(SImage)
									.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
									.ColorAndOpacity(FSlateColor(FColor::FromHex("FFB347")))
								]
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("NoLandscapeWarning", "No Landscape actor found in the current level. Add one (or open the level that contains your custom landscape), then click Refresh."))
								.AutoWrapText(true)
								.ColorAndOpacity(FSlateColor(FColor::FromHex("FFD899")))
								.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
							]
						]
					]

					// Subtle separator between landscape selection and import options.
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f)
					[
						SNew(SBox)
						.HeightOverride(1.0f)
						[
							SNew(SImage)
							.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.ColorAndOpacity(SeparatorColor)
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 8.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RuntimeDataHeader", "Runtime Authored Data"))
						.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
						.ColorAndOpacity(TerraDyneBlue)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("RuntimeDataDesc",
							"Recommended workflow: initialize authored worlds from a baked Landscape asset set. This works in packaged builds and uses the same runtime payload as the editor setup path."))
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 10.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("BakedAssetSetLabel", "Baked Asset Set:"))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(UTerraDyneLandscapeAssetSet::StaticClass())
							.DisplayUseSelected(false)
							.DisplayBrowse(true)
							.ObjectPath_Lambda([this]()
							{
								return SelectedLandscapeAssetSet.IsValid()
									? SelectedLandscapeAssetSet->GetPathName()
									: FString();
							})
							.OnObjectChanged(this, &STerraDyneSetupWizard::OnBakedAssetSetChanged)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						MakeOptionRow(
							LOCTEXT("LiveImportOnly", "Use Live Landscape Import Instead Of Baked Runtime Data"),
							LOCTEXT("LiveImportOnlyTip",
								"Editor-only fallback. Leave this off to keep authored conversion packaged-build-safe and reusable."),
							&bUseLiveLandscapeImport)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 10.0f)
					[
						SNew(SBox)
						.Visibility(this, &STerraDyneSetupWizard::GetBakeDestinationVisibility)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(0.0f, 0.0f, 10.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("BakePathLabel", "Bake Output Folder:"))
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(SEditableTextBox)
								.Text_Lambda([this]() { return FText::FromString(BakedLandscapeOutputPath); })
								.OnTextChanged(this, &STerraDyneSetupWizard::OnBakePathTextChanged)
								.ToolTipText(LOCTEXT("BakePathTip",
									"If no baked asset set is selected, Initialize World will bake the selected Landscape into this Content Browser folder and use it immediately."))
							]
						]
					]

					// Import Options subheading
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ImportOptionsHeader", "Import Options"))
						.Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))
						.ColorAndOpacity(TerraDyneBlue)
					]

					// 2-column grid of import option toggles
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SUniformGridPanel)
						.SlotPadding(FMargin(8.0f, 4.0f))
						+ SUniformGridPanel::Slot(0, 0)
						[
							MakeOptionRow(
								LOCTEXT("OptHide", "Hide Source Landscape"),
								LOCTEXT("OptHideTip", "Hide the original Landscape actor in the level after importing."),
								&MigrationOptions.bHideSourceLandscape)
						]
						+ SUniformGridPanel::Slot(1, 0)
						[
							MakeOptionRow(
								LOCTEXT("OptClear", "Clear Existing Chunks"),
								LOCTEXT("OptClearTip", "Destroy any existing TerraDyne chunks before importing."),
								&MigrationOptions.bClearExistingChunks)
						]
						+ SUniformGridPanel::Slot(0, 1)
						[
							MakeOptionRow(
								LOCTEXT("OptPause", "Pause Streaming During Import"),
								LOCTEXT("OptPauseTip", "Temporarily pause chunk streaming so the import can run uncontested."),
								&MigrationOptions.bPauseStreamingDuringImport)
						]
						+ SUniformGridPanel::Slot(1, 1)
						[
							MakeOptionRow(
								LOCTEXT("OptWeights", "Import Weight Layers"),
								LOCTEXT("OptWeightsTip", "Bake the source landscape's painted weight layers into TerraDyne weightmaps."),
								&MigrationOptions.bImportWeightLayers)
						]
						+ SUniformGridPanel::Slot(0, 2)
						[
							MakeOptionRow(
								LOCTEXT("OptCapture", "Capture Layer Mappings"),
								LOCTEXT("OptCaptureTip", "Record the mapping from source layer names to TerraDyne weight layer indices."),
								&MigrationOptions.bCaptureLayerMappings)
						]
						+ SUniformGridPanel::Slot(1, 2)
						[
							MakeOptionRow(
								LOCTEXT("OptGrass", "Regenerate Grass From Imported Layers"),
								LOCTEXT("OptGrassTip", "Re-spawn grass using the freshly imported weight layers."),
								&MigrationOptions.bRegenerateGrassFromImportedLayers)
						]
						+ SUniformGridPanel::Slot(0, 3)
						[
							MakeOptionRow(
								LOCTEXT("OptAdoptMat", "Adopt Landscape Material As Master Material"),
								LOCTEXT("OptAdoptMatTip", "Replace the TerraDyne master material with the source landscape's material. Off by default; landscape materials usually do not work on dynamic meshes."),
								&MigrationOptions.bAdoptLandscapeMaterialAsMasterMaterial)
						]
						+ SUniformGridPanel::Slot(1, 3)
						[
							MakeOptionRow(
								LOCTEXT("OptFoliage", "Transfer Placed Foliage"),
								LOCTEXT("OptFoliageTip", "Move existing painted foliage instances onto the new TerraDyne terrain."),
								&MigrationOptions.bTransferPlacedFoliage)
						]
						+ SUniformGridPanel::Slot(0, 4)
						[
							MakeOptionRow(
								LOCTEXT("OptFoliageFollow", "Foliage Follows Terrain"),
								LOCTEXT("OptFoliageFollowTip", "Keep transferred foliage attached to the live terrain surface as it deforms."),
								&MigrationOptions.bTransferredFoliageFollowsTerrain)
						]
					]
				]
			]

			// --- Separator above the action row ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(CardOuterPadding.Left, 12.0f, CardOuterPadding.Right, 0.0f)
			[
				SNew(SBox)
				.HeightOverride(1.0f)
				[
					SNew(SImage)
					.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.ColorAndOpacity(SeparatorColor)
				]
			]

			// --- Action Section ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(20.0f, 22.0f, 20.0f, 22.0f)
			.HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 12.0f, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("InitWorld", "Initialize World"))
					.ToolTipText(LOCTEXT("InitTooltip", "Replaces existing TerraDyne Manager/Orchestrator actors, applies the packaged sample preset/profile, and generates the selected template. Other level contents are preserved."))
					.OnClicked(this, &STerraDyneSetupWizard::OnInitializeClicked)
					.IsEnabled_Lambda([this]() { return IsInitializeEnabled(); })
					.ContentPadding(FMargin(48.f, 16.f))
					.ButtonStyle(FAppStyle::Get(), "FlatButton.Success")
					.TextStyle(FAppStyle::Get(), "NormalText.Important")
					.Cursor(EMouseCursor::Hand)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("OpenDocs", "Read the Manual"))
					.ToolTipText(LOCTEXT("DocsTooltip", "Open the comprehensive TerraDyne documentation in your web browser."))
					.OnClicked(this, &STerraDyneSetupWizard::OnDocumentationClicked)
					.ContentPadding(FMargin(22.f, 16.f))
					.Cursor(EMouseCursor::Hand)
				]
			]

			// --- Footer / Branding ---
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(FMargin(0.0f, 10.0f))
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BrandingText", "Created by GregOrigin"))
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
		]
	];
}

STerraDyneSetupWizard::~STerraDyneSetupWizard()
{
	if (MapOpenedHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle);
		MapOpenedHandle.Reset();
	}
	if (MapChangeHandle.IsValid())
	{
		FEditorDelegates::MapChange.Remove(MapChangeHandle);
		MapChangeHandle.Reset();
	}
}

void STerraDyneSetupWizard::RefreshLandscapeList()
{
	DetectedLandscapes.Reset();
	LandscapeOptions.Reset();
	SelectedLandscapeOption.Reset();

	if (GEditor)
	{
		if (UWorld* World = GEditor->GetEditorWorldContext().World())
		{
			for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
			{
				ALandscapeProxy* Landscape = *It;
				if (Landscape)
				{
					DetectedLandscapes.Add(Landscape);
					LandscapeOptions.Add(MakeShared<FString>(Landscape->GetActorLabel()));
				}
			}
		}
	}

	if (LandscapeOptions.Num() > 0)
	{
		SelectedLandscapeOption = LandscapeOptions[0];
	}

	if (LandscapeComboBox.IsValid())
	{
		LandscapeComboBox->RefreshOptions();
		LandscapeComboBox->ClearSelection();
		if (SelectedLandscapeOption.IsValid())
		{
			LandscapeComboBox->SetSelectedItem(SelectedLandscapeOption);
		}
	}
}

void STerraDyneSetupWizard::OnEditorMapOpened(const FString& /*Filename*/, bool /*bAsTemplate*/)
{
	RefreshLandscapeList();
}

void STerraDyneSetupWizard::OnEditorMapChange(uint32 /*MapChangeFlags*/)
{
	RefreshLandscapeList();
}

FReply STerraDyneSetupWizard::OnRefreshLandscapesClicked()
{
	RefreshLandscapeList();
	return FReply::Handled();
}

TSharedRef<SWidget> STerraDyneSetupWizard::MakeTemplateComboWidget(TSharedPtr<FString> InItem)
{
	FText ToolTip = FText::GetEmpty();
	if (InItem->Contains("Survival")) ToolTip = LOCTEXT("TipSurvival", "Recommended first run: streaming chunks, packaged sample preset, grass profile, biome overlays, AI zones, build zones, and starter world defaults.");
	else if (InItem->Contains("Cinematic")) ToolTip = LOCTEXT("TipCinematic", "Take a cinematic tour of the TerraDyne systems and features.");
	else if (InItem->Contains("Authored")) ToolTip = LOCTEXT("TipAuthored", "Take over an existing landscape and convert it to TerraDyne chunks.");
	else if (InItem->Contains("Sandbox")) ToolTip = LOCTEXT("TipSandbox", "A flat, minimal infinite canvas when you intentionally want fewer starter systems.");

	return SNew(STextBlock)
		.Text(FText::FromString(*InItem))
		.ToolTipText(ToolTip)
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"));
}

void STerraDyneSetupWizard::OnTemplateSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		SelectedTemplateOption = NewSelection;

		// Map string back to Enum
		int32 Index = TemplateOptions.Find(NewSelection);
		switch (Index)
		{
		case 0: SelectedTemplate = ETerraDyneDemoTemplate::SurvivalFramework; break;
		case 1: SelectedTemplate = ETerraDyneDemoTemplate::FullFeatureShowcase; break;
		case 2: SelectedTemplate = ETerraDyneDemoTemplate::AuthoredWorldConversion; break;
		case 3: SelectedTemplate = ETerraDyneDemoTemplate::Sandbox; break;
		default: SelectedTemplate = ETerraDyneDemoTemplate::SurvivalFramework; break;
		}

		// Whenever the user enters Authored World Conversion, re-scan landscapes so the dropdown
		// reflects the *current* level (not whatever was loaded when the wizard was first opened).
		if (SelectedTemplate == ETerraDyneDemoTemplate::AuthoredWorldConversion)
		{
			RefreshLandscapeList();
		}
	}
}

FText STerraDyneSetupWizard::GetSelectedTemplateText() const
{
	return SelectedTemplateOption.IsValid() ? FText::FromString(*SelectedTemplateOption) : FText::GetEmpty();
}

TSharedRef<SWidget> STerraDyneSetupWizard::MakeLandscapeComboWidget(TSharedPtr<FString> InItem)
{
	return SNew(STextBlock)
		.Text(FText::FromString(*InItem))
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"));
}

void STerraDyneSetupWizard::OnLandscapeSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	if (NewSelection.IsValid())
	{
		SelectedLandscapeOption = NewSelection;
	}
}

FText STerraDyneSetupWizard::GetSelectedLandscapeText() const
{
	return SelectedLandscapeOption.IsValid() ? FText::FromString(*SelectedLandscapeOption) : FText::GetEmpty();
}

EVisibility STerraDyneSetupWizard::GetLandscapeConfigVisibility() const
{
	return SelectedTemplate == ETerraDyneDemoTemplate::AuthoredWorldConversion
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility STerraDyneSetupWizard::GetLandscapeComboVisibility() const
{
	return DetectedLandscapes.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility STerraDyneSetupWizard::GetNoLandscapeWarningVisibility() const
{
	return (DetectedLandscapes.Num() == 0 && (!SelectedLandscapeAssetSet.IsValid() || bUseLiveLandscapeImport))
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility STerraDyneSetupWizard::GetBakeDestinationVisibility() const
{
	return (SelectedTemplate == ETerraDyneDemoTemplate::AuthoredWorldConversion &&
		!bUseLiveLandscapeImport &&
		!SelectedLandscapeAssetSet.IsValid())
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

bool STerraDyneSetupWizard::IsInitializeEnabled() const
{
	if (SelectedTemplate != ETerraDyneDemoTemplate::AuthoredWorldConversion)
	{
		return true;
	}
	if (SelectedLandscapeAssetSet.IsValid() && !bUseLiveLandscapeImport)
	{
		return true;
	}
	if (!SelectedLandscapeOption.IsValid())
	{
		return false;
	}
	const int32 Index = LandscapeOptions.Find(SelectedLandscapeOption);
	if (!DetectedLandscapes.IsValidIndex(Index))
	{
		return false;
	}
	return DetectedLandscapes[Index].IsValid();
}

ECheckBoxState STerraDyneSetupWizard::GetLiveImportCheckState() const
{
	return bUseLiveLandscapeImport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void STerraDyneSetupWizard::OnBakedAssetSetChanged(const FAssetData& AssetData)
{
	SelectedLandscapeAssetSet = Cast<UTerraDyneLandscapeAssetSet>(AssetData.GetAsset());
}

void STerraDyneSetupWizard::OnBakePathTextChanged(const FText& NewText)
{
	BakedLandscapeOutputPath = NewText.ToString();
}

void STerraDyneSetupWizard::OnLiveImportCheckStateChanged(ECheckBoxState NewState)
{
	bUseLiveLandscapeImport = (NewState == ECheckBoxState::Checked);
}

FReply STerraDyneSetupWizard::OnDocumentationClicked()
{
	FString DocsPath = TEXT("https://gregorigin.com/Terradyne/");
	FPlatformProcess::LaunchURL(*DocsPath, nullptr, nullptr);
	return FReply::Handled();
}

FReply STerraDyneSetupWizard::OnInitializeClicked()
{
	if (!GEditor)
	{
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return FReply::Handled();
	}

	// Resolve the target landscape up-front and hard-fail before spawning anything if Authored World Conversion
	// was selected without a valid landscape. This prevents the silent "empty world" outcome where a Manager
	// would otherwise be created with chunks disabled and no import to fall back on.
	ALandscapeProxy* TargetLandscape = nullptr;
	UTerraDyneLandscapeAssetSet* BakedAssetSet = SelectedLandscapeAssetSet.Get();
	if (SelectedTemplate == ETerraDyneDemoTemplate::AuthoredWorldConversion)
	{
		const bool bNeedLandscapeSelection = bUseLiveLandscapeImport || !BakedAssetSet;
		if (bNeedLandscapeSelection && SelectedLandscapeOption.IsValid())
		{
			const int32 Index = LandscapeOptions.Find(SelectedLandscapeOption);
			if (DetectedLandscapes.IsValidIndex(Index) && DetectedLandscapes[Index].IsValid())
			{
				TargetLandscape = DetectedLandscapes[Index].Get();
			}
		}

		if (bNeedLandscapeSelection && !TargetLandscape)
		{
			FNotificationInfo Info(LOCTEXT("NoLandscapeError",
				"TerraDyne Setup: Authored World Conversion needs a Landscape actor in the current level. Add or load one, click Refresh, and try again."));
			Info.ExpireDuration = 6.0f;
			Info.bUseSuccessFailIcons = true;
			TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
			if (Item.IsValid())
			{
				Item->SetCompletionState(SNotificationItem::CS_Fail);
			}
			UE_LOG(LogTerraDyneEditor, Warning,
				TEXT("TerraDyne Setup Wizard: Authored World Conversion aborted - no valid Landscape actor selected."));
			return FReply::Handled();
		}

		if (!bUseLiveLandscapeImport && !BakedAssetSet)
		{
			BakedAssetSet = UTerraDyneBaker::BakeLandscapeToAssetSet(TargetLandscape, BakedLandscapeOutputPath, MigrationOptions);
			if (!BakedAssetSet)
			{
				FNotificationInfo Info(LOCTEXT("BakeLandscapeFailed",
					"TerraDyne Setup: Failed to bake the selected Landscape into a runtime asset set. See the Output Log."));
				Info.ExpireDuration = 6.0f;
				Info.bUseSuccessFailIcons = true;
				TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
				if (Item.IsValid())
				{
					Item->SetCompletionState(SNotificationItem::CS_Fail);
				}
				return FReply::Handled();
			}
		}
	}

	bool bSucceeded = false;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATerraDyneSceneSetup* SetupWizard = World->SpawnActor<ATerraDyneSceneSetup>(ATerraDyneSceneSetup::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);

	if (SetupWizard)
	{
		SetupWizard->DemoTemplate = SelectedTemplate;
		SetupWizard->MigrationOptions = MigrationOptions;
		SetupWizard->AuthoredWorldAssetSet = BakedAssetSet;
		SetupWizard->bPreferBakedLandscapeData = (BakedAssetSet != nullptr) && !bUseLiveLandscapeImport;

		SetupWizard->InitializeWorld(TargetLandscape);

		// Clean up the wizard actor so it doesn't clutter the outliner
		SetupWizard->Destroy();
		bSucceeded = true;

		UE_LOG(LogTerraDyneEditor, Warning, TEXT("TerraDyne Setup Wizard: World Initialization Complete."));
	}

	// Dismiss the wizard tab once initialization completes so the user can't keep re-clicking.
	if (bSucceeded)
	{
		static const FName WizardTabName("TerraDyneSetupWizard");
		if (TSharedPtr<SDockTab> WizardTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(WizardTabName)))
		{
			WizardTab->RequestCloseTab();
		}
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
