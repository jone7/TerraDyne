// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "TerraDyneModule.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

// Define the log category
DEFINE_LOG_CATEGORY(LogTerraDyne);

#define LOCTEXT_NAMESPACE "FTerraDyneModule"

namespace
{
	void AddTerraDyneShaderDirCandidate(TArray<FString>& CandidateDirs, const FString& CandidateDir)
	{
		FString NormalizedDir = CandidateDir;
		FPaths::NormalizeDirectoryName(NormalizedDir);
		CandidateDirs.AddUnique(NormalizedDir);
	}

	void AddTerraDyneShaderDirParentCandidates(TArray<FString>& CandidateDirs, const FString& BaseDir)
	{
		FString SearchDir = BaseDir;
		FPaths::NormalizeDirectoryName(SearchDir);

		for (int32 ParentIndex = 0; ParentIndex < 8 && !SearchDir.IsEmpty(); ++ParentIndex)
		{
			AddTerraDyneShaderDirCandidate(CandidateDirs, FPaths::Combine(SearchDir, TEXT("Shaders")));

			const FString ParentDir = FPaths::GetPath(SearchDir);
			if (ParentDir == SearchDir)
			{
				break;
			}

			SearchDir = ParentDir;
		}
	}

	FString FindTerraDyneShaderDirectory()
	{
		TArray<FString> CandidateDirs;

		const TSharedPtr<IPlugin> TerraDynePlugin = IPluginManager::Get().FindPlugin(TEXT("TerraDyne"));
		if (TerraDynePlugin.IsValid())
		{
			AddTerraDyneShaderDirParentCandidates(CandidateDirs, TerraDynePlugin->GetBaseDir());
		}

		AddTerraDyneShaderDirCandidate(CandidateDirs, FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("TerraDyne"), TEXT("Shaders")));
		AddTerraDyneShaderDirCandidate(CandidateDirs, FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("Marketplace"), TEXT("TerraDyne"), TEXT("Shaders")));

		for (const FString& CandidateDir : CandidateDirs)
		{
			if (FPaths::DirectoryExists(CandidateDir))
			{
				return CandidateDir;
			}
		}

		return FString();
	}
}

void FTerraDyneModule::StartupModule()
{
	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne Runtime Module Started."));

	const FString PluginShaderDir = FindTerraDyneShaderDirectory();
	if (PluginShaderDir.IsEmpty())
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyne shader directory was not found. Custom TerraDyne shaders will be unavailable."));
		return;
	}

	AddShaderSourceDirectoryMapping(TEXT("/Plugin/TerraDyne"), PluginShaderDir);
	UE_LOG(LogTerraDyne, Log, TEXT("Mapped TerraDyne shader directory: %s"), *PluginShaderDir);
}

void FTerraDyneModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module. 
	// For modules that support dynamic reloading, we call this function before unloading the module.
	
	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne Runtime Module Shutting Down."));
}

#undef LOCTEXT_NAMESPACE
	
// Implement the module. 
// "TerraDyne" must match the name in your .uplugin and .Build.cs
IMPLEMENT_MODULE(FTerraDyneModule, TerraDyne)
