// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "World/TerraDyneSceneSetup.h" // For ETerraDyneDemoTemplate + FTerraDyneLandscapeMigrationOptions

template <typename OptionType> class SComboBox;

class TERRADYNEEDITOR_API STerraDyneSetupWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STerraDyneSetupWizard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~STerraDyneSetupWizard();

private:
	FReply OnInitializeClicked();
	FReply OnDocumentationClicked();
	FReply OnRefreshLandscapesClicked();
	void OnBakedAssetSetChanged(const struct FAssetData& AssetData);
	void OnBakePathTextChanged(const FText& NewText);
	void OnLiveImportCheckStateChanged(ECheckBoxState NewState);

	// Combo Box Handlers
	TSharedRef<SWidget> MakeTemplateComboWidget(TSharedPtr<FString> InItem);
	void OnTemplateSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedTemplateText() const;

	ETerraDyneDemoTemplate SelectedTemplate = ETerraDyneDemoTemplate::SurvivalFramework;

	TArray<TSharedPtr<FString>> TemplateOptions;
	TSharedPtr<FString> SelectedTemplateOption;

	// Landscape Handling
	TArray<TSharedPtr<FString>> LandscapeOptions;
	TSharedPtr<FString> SelectedLandscapeOption;
	TArray<TWeakObjectPtr<class ALandscapeProxy>> DetectedLandscapes;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LandscapeComboBox;

	TSharedRef<SWidget> MakeLandscapeComboWidget(TSharedPtr<FString> InItem);
	void OnLandscapeSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	FText GetSelectedLandscapeText() const;

	EVisibility GetLandscapeConfigVisibility() const;
	EVisibility GetLandscapeComboVisibility() const;
	EVisibility GetNoLandscapeWarningVisibility() const;
	EVisibility GetBakeDestinationVisibility() const;
	bool IsInitializeEnabled() const;
	ECheckBoxState GetLiveImportCheckState() const;

	void RefreshLandscapeList();
	void OnEditorMapOpened(const FString& Filename, bool bAsTemplate);
	void OnEditorMapChange(uint32 MapChangeFlags);

	// User-tunable migration options forwarded to the spawned Manager when AuthoredWorldConversion runs.
	FTerraDyneLandscapeMigrationOptions MigrationOptions;
	TWeakObjectPtr<class UTerraDyneLandscapeAssetSet> SelectedLandscapeAssetSet;
	FString BakedLandscapeOutputPath = TEXT("/Game/TerraDyne/BakedData");
	bool bUseLiveLandscapeImport = false;

	FDelegateHandle MapOpenedHandle;
	FDelegateHandle MapChangeHandle;

	TSharedPtr<struct FSlateDynamicImageBrush> LogoBrush;
	TSharedPtr<struct FSlateDynamicImageBrush> BannerBrush;
};
