#include "Tests/CookerTerrainTrial.h"
#include "Core/TerraDyneManager.h"
#include "World/TerraDyneChunk.h"
#include "World/TerraDyneLandscapeAssetSet.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/KismetSystemLibrary.h"

// 禁用默认鼠标轴和键盘绑定，由试验控制器决定镜头输入。
ACookerTerrainTrialPawn::ACookerTerrainTrialPawn()
{
    bAddDefaultMovementBindings = false;
}

// 注册当前进程的笔刷动作；这些映射只服务试验控制器，不写 ini。
void ACookerTerrainTrialController::SetupInputComponent()
{
    UInputSettings* Settings = GetMutableDefault<UInputSettings>();
    Settings->AddActionMapping(FInputActionKeyMapping(TEXT("TerraDyneClick"), EKeys::LeftMouseButton), false);
    Settings->AddActionMapping(FInputActionKeyMapping(TEXT("TerraDyneUndo"), EKeys::Z, false, true), false);
    Settings->AddActionMapping(FInputActionKeyMapping(TEXT("TerraDyneRedo"), EKeys::Y, false, true), false);
    Settings->AddAxisMapping(FInputAxisKeyMapping(TEXT("MouseWheelAxis"), EKeys::MouseWheelAxis, 1.0f), false);
    Settings->ForceRebuildKeymaps();
    Super::SetupInputComponent();
}

// 插件创建工具界面后，将飞行相机定位到测试地形上方。
void ACookerTerrainTrialController::BeginPlay()
{
    Super::BeginPlay();
    SetControlRotation(FRotator(-55.0, 90.0, 0.0));
}

// 保留鼠标绘地，按住右键才旋转；所有键盘方向汇入 Pawn 移动组件。
void ACookerTerrainTrialController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    APawn* Pawn = GetPawn();
    if (!Pawn) return;
    if (IsInputKeyDown(EKeys::RightMouseButton))
    {
        float DeltaX = 0.0f;
        float DeltaY = 0.0f;
        GetInputMouseDelta(DeltaX, DeltaY);
        FRotator Rotation = GetControlRotation();
        Rotation.Yaw += DeltaX * 0.15f;
        Rotation.Pitch = FMath::Clamp(Rotation.Pitch - DeltaY * 0.15f, -85.0, -5.0);
        SetControlRotation(Rotation);
    }
    Pawn->SetActorRotation(GetControlRotation());
    const FRotator YawRotation(0.0, GetControlRotation().Yaw, 0.0);
    Pawn->AddMovementInput(YawRotation.Vector(), float(IsInputKeyDown(EKeys::W)) - float(IsInputKeyDown(EKeys::S)));
    Pawn->AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), float(IsInputKeyDown(EKeys::D)) - float(IsInputKeyDown(EKeys::A)));
    Pawn->AddMovementInput(FVector::UpVector, float(IsInputKeyDown(EKeys::E)) - float(IsInputKeyDown(EKeys::Q)));
}

// 使用独立相机和插件控制器，正式地图继续使用自己的 GameMode。
ACookerTerrainTrialGameMode::ACookerTerrainTrialGameMode()
{
    PlayerControllerClass = ACookerTerrainTrialController::StaticClass();
    DefaultPawnClass = ACookerTerrainTrialPawn::StaticClass();
    HUDClass = AHUD::StaticClass();
}

// 自动探测默认关闭，仅显式命令行测试时逐帧检查。
ACookerTerrainTrialBootstrap::ACookerTerrainTrialBootstrap()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}

// 在单机试验中创建管理器并从已烘焙数据初始化，拒绝误用于联机。
void ACookerTerrainTrialBootstrap::BeginPlay()
{
    Super::BeginPlay();
    if (GetNetMode() != NM_Standalone || !AssetSet)
    {
        UE_LOG(LogTemp, Error, TEXT("CookerTerrainTrial: standalone mode and baked AssetSet are required."));
        return;
    }
    Manager = GetWorld()->SpawnActorDeferred<ATerraDyneManager>(ATerraDyneManager::StaticClass(), FTransform::Identity);
    Manager->MasterMaterial = AssetSet->AdoptedMasterMaterial;
    Manager->FinishSpawning(FTransform::Identity);
    if (!Manager->InitializeFromBakedLandscapeAssetSet(AssetSet, true)) return;
    UE_LOG(LogTemp, Display, TEXT("CookerTerrainTrial: baked Landscape loaded chunks=%d vertices=%d"), Manager->GetActiveChunkCount(), Manager->GetTotalVertexCount());
    if (FParse::Param(FCommandLine::Get(), TEXT("TerrainTrialProbe")))
    {
        StageStartSeconds = FPlatformTime::Seconds();
        SetActorTickEnabled(true);
    }
}

// 将 CPU 修改、异步网格及 Chaos 真实碰撞一起核对，成功或超时后退出测试进程。
void ACookerTerrainTrialBootstrap::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = FPlatformTime::Seconds();
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CookerTerrainTrial), true);
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit,
        ProbePosition + FVector(0.0, 0.0, 20000.0), ProbePosition - FVector(0.0, 0.0, 20000.0), ECC_Visibility, Params);
    const bool bTerrainHit = bHit && Hit.GetActor()->IsA<ATerraDyneChunk>();
    if (bTerrainHit && (ProbeStage == 0 || FMath::Abs(Hit.ImpactPoint.Z - ExpectedHeight) < 3.0))
    {
        UE_LOG(LogTemp, Display, TEXT("CookerTerrainTrial: stage=%d collisionZ=%.3f expectedZ=%.3f readyMs=%.3f"),
            ProbeStage, Hit.ImpactPoint.Z, ExpectedHeight, (Now - StageStartSeconds) * 1000.0);
        if (ProbeStage == 2)
        {
            UE_LOG(LogTemp, Display, TEXT("CookerTerrainTrial: PASS lower/undo and real collision"));
            SetActorTickEnabled(false);
            UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, true);
            return;
        }
        ATerraDyneChunk* Chunk = CastChecked<ATerraDyneChunk>(Hit.GetActor());
        APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        const double EditStart = FPlatformTime::Seconds();
        if (ProbeStage == 0)
        {
            InitialHeight = Hit.ImpactPoint.Z;
            Manager->BeginStroke(ProbePosition, 800.0f, Controller);
            Manager->ApplyGlobalBrush(ProbePosition, 800.0f, 500.0f, ETerraDyneBrushMode::Lower);
            Manager->CommitStroke(Controller);
        }
        else
        {
            Manager->Undo(Controller);
        }
        ExpectedHeight = Chunk->GetActorLocation().Z + Chunk->GetHeightAtLocation(ProbePosition - Chunk->GetActorLocation());
        if ((ProbeStage == 0 && ExpectedHeight >= InitialHeight - 10.0) ||
            (ProbeStage == 1 && FMath::Abs(ExpectedHeight - InitialHeight) > 3.0))
        {
            UE_LOG(LogTemp, Error, TEXT("CookerTerrainTrial: FAIL height mutation stage=%d"), ProbeStage);
            SetActorTickEnabled(false);
            UKismetSystemLibrary::QuitGame(this, Controller, EQuitPreference::Quit, true);
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("CookerTerrainTrial: edit stage=%d cpuMs=%.3f targetZ=%.3f"), ProbeStage,
            (FPlatformTime::Seconds() - EditStart) * 1000.0, ExpectedHeight);
        ProbeStage++;
        StageStartSeconds = FPlatformTime::Seconds();
    }
    else if (Now - StageStartSeconds > 20.0)
    {
        UE_LOG(LogTemp, Error, TEXT("CookerTerrainTrial: FAIL collision timeout stage=%d hit=%d collisionZ=%.3f expectedZ=%.3f"), ProbeStage, bTerrainHit, Hit.ImpactPoint.Z, ExpectedHeight);
        SetActorTickEnabled(false);
        UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, true);
    }
}
