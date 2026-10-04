#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CookerTerrainTrialLibrary.generated.h"

class UTerraDyneLandscapeAssetSet;
class UMaterialInterface;

UCLASS()
class TERRADYNEEDITOR_API UCookerTerrainTrialLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // 仅在空试验地图创建四块小 Landscape，转换并保存到指定试验目录。
    UFUNCTION(BlueprintCallable, Category="Cooker Terrain Trial")
    static UTerraDyneLandscapeAssetSet* CreateTrialLandscape(UObject* WorldContextObject, UMaterialInterface* Material, const FString& DestinationPath);
};
