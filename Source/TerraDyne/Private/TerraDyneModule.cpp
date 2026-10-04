// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "TerraDyneModule.h"
#include "Modules/ModuleManager.h"
DEFINE_LOG_CATEGORY(LogTerraDyne);

// 开源包不包含计算 Shader，本适配显式使用 CPU 笔刷。
void FTerraDyneModule::StartupModule()
{
    UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne Runtime Module Started: CPU brushes, async mesh and collision."));
}

// 模块停止时保留可诊断的生命周期记录。
void FTerraDyneModule::ShutdownModule()
{
    UE_LOG(LogTerraDyne, Log, TEXT("TerraDyne Runtime Module Shutting Down."));
}
IMPLEMENT_MODULE(FTerraDyneModule, TerraDyne)
