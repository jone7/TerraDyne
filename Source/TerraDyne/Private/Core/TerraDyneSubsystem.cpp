// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneSubsystem.h"
#include "Core/TerraDyneManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Grass/TerraDyneGrassSystem.h"
#include "Async/TaskGraphInterfaces.h"
#include "Settings/TerraDyneSettings.h"
#include "TerraDyneModule.h"
#if WITH_EDITOR
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

void UTerraDyneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyneSubsystem: Initializing for World %s"), *GetWorld()->GetName());

	// create the Grass System (Non-UObject worker)
	// We use MakeShared to keep it alive as long as this Subsystem exists
	GrassSystem = MakeShared<FTerraDyneGrassSystem>();
	
	// If the Grass System needs to know about the world (for line traces or spawning), pass it here
	if (GrassSystem.IsValid())
	{
		GrassSystem->Initialize(GetWorld());
	}
}

void UTerraDyneSubsystem::Deinitialize()
{
	// Ensure we don't leave lingering threads or GC references
	FlushPendingTasks();

	if (GrassSystem.IsValid())
	{
		GrassSystem->Shutdown();
		GrassSystem.Reset();
	}

	ActiveManager.Reset();

	UE_LOG(LogTerraDyne, Log, TEXT("TerraDyneSubsystem: Deinitialized."));

	Super::Deinitialize();
}

//--- Manager Registry ---//

void UTerraDyneSubsystem::RegisterManager(ATerraDyneManager* InManager)
{
	if (!InManager) return;

	if (ActiveManager.IsValid() && ActiveManager.Get() != InManager)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("TerraDyneSubsystem: Multiple Managers detected! Overwriting registration to %s"), *InManager->GetName());
	}

	ActiveManager = InManager;
}

void UTerraDyneSubsystem::UnregisterManager(ATerraDyneManager* InManager)
{
	if (ActiveManager.Get() == InManager)
	{
		ActiveManager.Reset();
	}
}

ATerraDyneManager* UTerraDyneSubsystem::GetTerrainManager() const
{
	if (ActiveManager.IsValid())
	{
		return ActiveManager.Get();
	}
	return nullptr;
}

//--- Grass System Access ---//

TSharedPtr<FTerraDyneGrassSystem> UTerraDyneSubsystem::GetGrassSystem() const
{
	return GrassSystem;
}

//--- IO Safety ---//

void UTerraDyneSubsystem::FlushPendingTasks()
{
	// 1. Cancel/Finish Grass Generation checks
	if (GrassSystem.IsValid())
	{
		GrassSystem->CancelAllTasks();
	}

	// 2. Wait for IO (Thread Pool)
	// If we have critical save games writing to disk, we generally don't want to kill the process.
	// FNonAbandonableTasks running on the thread pool usually finish on their own,
	// but explicit synchronization can be done here if we tracked specific IO tasks.

	// In a production environment, you might use a specific FGraphEvent to track active loads/saves
	// and wait for them:
	// FTaskGraphInterface::Get().WaitUntilTaskCompletes(MyTrackingHandle);
}

void UTerraDyneSubsystem::ShowNotification(const FText& Message, ETerraDyneNotifySeverity Severity)
{
	// Always log
	switch (Severity)
	{
	case ETerraDyneNotifySeverity::Error:
		UE_LOG(LogTerraDyne, Error, TEXT("Notification: %s"), *Message.ToString());
		break;
	case ETerraDyneNotifySeverity::Warning:
		UE_LOG(LogTerraDyne, Warning, TEXT("Notification: %s"), *Message.ToString());
		break;
	default:
		UE_LOG(LogTerraDyne, Log, TEXT("Notification: %s"), *Message.ToString());
		break;
	}

#if WITH_EDITOR
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 5.0f;
	Info.bUseLargeFont = false;
	Info.bFireAndForget = true;
	FSlateNotificationManager::Get().AddNotification(Info);
#else
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	if (Settings && !Settings->bShowRuntimeNotifications)
	{
		return;
	}

	FColor Color = FColor::White;
	if (Severity == ETerraDyneNotifySeverity::Warning) Color = FColor::Yellow;
	else if (Severity == ETerraDyneNotifySeverity::Error) Color = FColor::Red;

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, Color, Message.ToString());
	}
#endif
}
