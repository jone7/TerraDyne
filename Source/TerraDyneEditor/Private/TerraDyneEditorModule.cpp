// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "TerraDyneEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "ToolMenus.h"
#include "UI/STerraDyneSetupWizard.h"

// Define the Log Category declared in the header
DEFINE_LOG_CATEGORY(LogTerraDyneEditor);

#define LOCTEXT_NAMESPACE "FTerraDyneEditorModule"

static const FName TerraDyneSetupWizardTabName("TerraDyneSetupWizard");

void FTerraDyneEditorModule::StartupModule()
{
	UE_LOG(LogTerraDyneEditor, Log, TEXT("TerraDyne Editor Module Started."));

	// Register Tab Spawners
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TerraDyneSetupWizardTabName, FOnSpawnTab::CreateRaw(this, &FTerraDyneEditorModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FTerraDyneSetupWizardTabTitle", "TerraDyne Setup Wizard"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FTerraDyneEditorModule::RegisterMenus));
}

void FTerraDyneEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TerraDyneSetupWizardTabName);

	UE_LOG(LogTerraDyneEditor, Log, TEXT("TerraDyne Editor Module Shutting Down."));
}

void FTerraDyneEditorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			
			FToolMenuEntry SetupEntry = FToolMenuEntry::InitMenuEntry(
				"TerraDyneSetupWizard",
				LOCTEXT("TerraDyneSetupWizard_Label", "TerraDyne Setup Wizard"),
				LOCTEXT("TerraDyneSetupWizard_ToolTip", "Open the unified TerraDyne setup and authored-world conversion workflow"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FTerraDyneEditorModule::PluginButtonClicked))
			);
			Section.AddEntry(SetupEntry);
		}
	}
}

void FTerraDyneEditorModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TerraDyneSetupWizardTabName);
}

TSharedRef<SDockTab> FTerraDyneEditorModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(STerraDyneSetupWizard)
		];
}

#undef LOCTEXT_NAMESPACE
	
// Implement the module logic
IMPLEMENT_MODULE(FTerraDyneEditorModule, TerraDyneEditor)

