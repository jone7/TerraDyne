// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Custom Log Category for Editor/Baking operations
TERRADYNEEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogTerraDyneEditor, Log, All);

class FTerraDyneEditorModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * Accessor for the module instance.
	 */
	static inline FTerraDyneEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FTerraDyneEditorModule>("TerraDyneEditor");
	}

	/**
	 * Check if the module is loaded.
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("TerraDyneEditor");
	}

private:
	void RegisterMenus();
	void PluginButtonClicked();
	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);
};
