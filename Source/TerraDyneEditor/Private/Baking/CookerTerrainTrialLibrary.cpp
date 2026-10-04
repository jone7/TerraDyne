#include "Baking/CookerTerrainTrialLibrary.h"
#include "Baking/TerraDyneBaker.h"
#include "Core/TerraDyneManager.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"

// 通过真实 Landscape 导入和插件正式烘焙链构造夹具，不手写派生地形数据。
UTerraDyneLandscapeAssetSet* UCookerTerrainTrialLibrary::CreateTrialLandscape(
    UObject* WorldContextObject, UMaterialInterface* Material, const FString& DestinationPath)
{
    UWorld* World = WorldContextObject->GetWorld();
    ALandscape* Landscape = World->SpawnActor<ALandscape>();
    Landscape->SetActorLabel(TEXT("TrialSourceLandscape"));
    Landscape->SetActorScale3D(FVector(100.0, 100.0, 50.0));
    Landscape->LandscapeMaterial = Material;
    TMap<FGuid, TArray<uint16>> Heights;
    TArray<uint16>& Samples = Heights.Add(FGuid());
    Samples.SetNumUninitialized(63 * 63);
    for (int32 Y = 0; Y < 63; ++Y)
        for (int32 X = 0; X < 63; ++X)
            Samples[Y * 63 + X] = uint16(32768 + 400 * FMath::Sin(X * PI / 62.0) * FMath::Sin(Y * PI / 62.0));
    TMap<FGuid, TArray<FLandscapeImportLayerInfo>> Layers;
    Layers.Add(FGuid());
    Landscape->Import(FGuid::NewGuid(), 0, 0, 62, 62, 1, 31, Heights, nullptr, Layers,
        ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());
    Landscape->CreateLandscapeInfo();
    Landscape->UpdateAllComponentMaterialInstances();
    FTerraDyneLandscapeMigrationOptions Options;
    Options.bImportWeightLayers = false;
    Options.bAdoptLandscapeMaterialAsMasterMaterial = true;
    Options.bTransferPlacedFoliage = false;
    UTerraDyneLandscapeAssetSet* AssetSet = UTerraDyneBaker::BakeLandscapeToAssetSet(Landscape, DestinationPath, Options);
    Landscape->SetActorEnableCollision(false);
    Landscape->SetActorHiddenInGame(true);
    Landscape->SetIsTemporarilyHiddenInEditor(true);
    return AssetSet;
}
