#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneEditController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/SpectatorPawn.h"
#include "CookerTerrainTrial.generated.h"

class ATerraDyneManager;
class UTerraDyneLandscapeAssetSet;

UCLASS()
class TERRADYNE_API ACookerTerrainTrialPawn : public ASpectatorPawn
{
    GENERATED_BODY()
public:
    // 测试相机由控制器统一驱动，避免绘地时鼠标同时转动镜头。
    ACookerTerrainTrialPawn();
};

UCLASS()
class TERRADYNE_API ACookerTerrainTrialController : public ATerraDyneEditController
{
    GENERATED_BODY()
protected:
    // 仅在测试进程内注册输入映射，不保存或修改 Cooker 的输入配置文件。
    virtual void SetupInputComponent() override;
    // 设置独立试验的俯视相机，复用插件已有工具界面。
    virtual void BeginPlay() override;
    // 右键转向、WASD 移动及 QE 升降；地形笔刷仍由插件共同入口执行。
    virtual void Tick(float DeltaSeconds) override;
};

UCLASS()
class TERRADYNE_API ACookerTerrainTrialGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    // 将试验地图隔离于 Cooker 正式角色、HUD 和存档逻辑。
    ACookerTerrainTrialGameMode();
};

UCLASS()
class TERRADYNE_API ACookerTerrainTrialBootstrap : public AActor
{
    GENERATED_BODY()
public:
    // 只在独立试验地图初始化已烘焙 Landscape，并按需进行真实碰撞探测。
    ACookerTerrainTrialBootstrap();
    // 编辑器转换后的地形资产；运行时不调用 Landscape 编辑器接口。
    UPROPERTY(EditAnywhere, Category="Cooker Terrain Trial")
    TObjectPtr<UTerraDyneLandscapeAssetSet> AssetSet;
protected:
    // 创建管理器并载入资产，命令行 TerrainTrialProbe 开启自动检查。
    virtual void BeginPlay() override;
    // 等待异步网格与碰撞完成，核对挖低前后真实射线命中高度。
    virtual void Tick(float DeltaSeconds) override;
private:
    // 本地图唯一管理器，避免另建地形修改算法。
    UPROPERTY(Transient)
    TObjectPtr<ATerraDyneManager> Manager;
    // 0 等初始碰撞、1 等挖低后碰撞、2 等恢复后碰撞。
    int32 ProbeStage = 0;
    // 每阶段起始秒数，用于超时及异步提交耗时记录。
    double StageStartSeconds = 0.0;
    // 第一块地形内部固定探测位置，单位厘米。
    FVector ProbePosition = FVector(1550.0, 1550.0, 0.0);
    // 修改前真实碰撞高度，单位厘米。
    double InitialHeight = 0.0;
    // CPU 高度查询与真实碰撞期望一致的高度，单位厘米。
    double ExpectedHeight = 0.0;
};
