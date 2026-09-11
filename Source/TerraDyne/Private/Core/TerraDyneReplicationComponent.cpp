// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Core/TerraDyneReplicationComponent.h"

#include "Core/TerraDyneManager.h"
#include "Core/TerraDyneSubsystem.h"
#include "IO/TerraDyneSerializer.h"
#include "Settings/TerraDyneSettings.h"
#include "TerraDyneModule.h"
#include "World/TerraDyneChunk.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

UTerraDyneReplicationComponent::UTerraDyneReplicationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UTerraDyneReplicationComponent::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* Controller = GetOwningPlayerController())
	{
		if (!Controller->HasAuthority() && Controller->IsLocalController())
		{
			Server_RequestFullSync();
		}
	}
}

void UTerraDyneReplicationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	IncomingTransfers.Reset();
	OutgoingTransfers.Reset();
	PendingDecodedStates.Reset();
	DecodedStateInsertionOrder.Reset();
	QueuedChunkStateRequests.Reset();
	OutstandingChunkStateRequests.Reset();
	QueuedOutgoingBytes = 0;
	Super::EndPlay(EndPlayReason);
}

APlayerController* UTerraDyneReplicationComponent::GetOwningPlayerController() const
{
	return Cast<APlayerController>(GetOwner());
}

ATerraDyneManager* UTerraDyneReplicationComponent::GetTerrainManager() const
{
	UWorld* World = GetWorld();
	UTerraDyneSubsystem* Subsystem = World ? World->GetSubsystem<UTerraDyneSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetTerrainManager() : nullptr;
}

bool UTerraDyneReplicationComponent::ConsumeStateRequestToken()
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float Rate = Settings ? FMath::Max(1.0f, Settings->MaxChunkStateRequestsPerSecond) : 16.0f;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (AvailableStateRequestTokens < 0.0f)
	{
		AvailableStateRequestTokens = Rate;
		LastStateRequestRefillSeconds = Now;
	}

	const double Elapsed = FMath::Max(0.0, Now - LastStateRequestRefillSeconds);
	AvailableStateRequestTokens = FMath::Min(Rate, AvailableStateRequestTokens + static_cast<float>(Elapsed) * Rate);
	LastStateRequestRefillSeconds = Now;
	if (AvailableStateRequestTokens < 1.0f)
	{
		return false;
	}
	AvailableStateRequestTokens -= 1.0f;
	return true;
}

bool UTerraDyneReplicationComponent::IsCoordinateRelevantToOwner(FIntPoint Coordinate) const
{
	const ATerraDyneManager* Manager = GetTerrainManager();
	const ATerraDyneChunk* Chunk = Manager ? Manager->GetChunkAtCoord(Coordinate) : nullptr;
	const APlayerController* Controller = GetOwningPlayerController();
	if (!Manager || !Chunk || !Controller)
	{
		return false;
	}

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float Radius = Settings ? Settings->TerrainReplicationRadius : 150000.0f;
	if (Radius <= 0.0f)
	{
		return true;
	}

	const AActor* ReferenceActor = Controller->GetPawn();
	if (!ReferenceActor)
	{
		ReferenceActor = Controller->GetViewTarget();
	}
	return ReferenceActor && FVector::DistSquared(ReferenceActor->GetActorLocation(), Chunk->GetActorLocation()) <= FMath::Square(Radius);
}

void UTerraDyneReplicationComponent::Server_RequestFullSync_Implementation()
{
	if (!ConsumeStateRequestToken())
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - LastFullSyncRequestSeconds < 5.0)
	{
		UE_LOG(LogTerraDyne, Verbose, TEXT("Ignoring repeated TerraDyne full-sync request from %s."), *GetNameSafe(GetOwner()));
		return;
	}
	LastFullSyncRequestSeconds = Now;

	if (ATerraDyneManager* Manager = GetTerrainManager())
	{
		Manager->SendFullSyncToReplicationComponent(this);
	}
}

void UTerraDyneReplicationComponent::Server_RequestChunkState_Implementation(FIntPoint Coordinate)
{
	if (!ConsumeStateRequestToken() || !IsCoordinateRelevantToOwner(Coordinate))
	{
		return;
	}
	if (ATerraDyneManager* Manager = GetTerrainManager())
	{
		if (ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(Coordinate))
		{
			SendChunkState(Chunk->GetSerializedData());
		}
	}
}

bool UTerraDyneReplicationComponent::SendChunkState(const FTerraDyneChunkData& Data)
{
	APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || !Controller->HasAuthority())
	{
		return false;
	}

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxUncompressed = Settings
		? Settings->MaxUncompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxUncompressedBytes;
	const int32 MaxPacket = Settings
		? Settings->MaxCompressedChunkStateBytes
		: FTerraDyneStateCodec::DefaultMaxPacketBytes;
	const int32 MaxQueuedBytes = Settings
		? Settings->MaxQueuedStateBytesPerConnection
		: 32 * 1024 * 1024;

	TArray<uint8> Packet;
	FString Error;
	if (!FTerraDyneStateCodec::EncodeChunk(Data, Packet, &Error, MaxUncompressed))
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Chunk [%d,%d] state encode failed: %s"),
			Data.Coordinate.X, Data.Coordinate.Y, *Error);
		return false;
	}
	if (Packet.Num() > MaxPacket || QueuedOutgoingBytes + Packet.Num() > MaxQueuedBytes)
	{
		UE_LOG(LogTerraDyne, Warning,
			TEXT("Chunk [%d,%d] state was not queued for %s: packet/connection byte budget exceeded (%d bytes)."),
			Data.Coordinate.X, Data.Coordinate.Y, *GetNameSafe(Controller), Packet.Num());
		return false;
	}

	FOutgoingTransfer& Transfer = OutgoingTransfers.AddDefaulted_GetRef();
	Transfer.TransferId = NextTransferId++;
	if (NextTransferId <= 0)
	{
		NextTransferId = 1;
	}
	Transfer.Coordinate = Data.Coordinate;
	Transfer.FragmentSize = FMath::Clamp(Settings ? Settings->StateFragmentSizeBytes : 48 * 1024, 1024, 48 * 1024);
	Transfer.Packet = MoveTemp(Packet);
	QueuedOutgoingBytes += Transfer.Packet.Num();
	return true;
}

void UTerraDyneReplicationComponent::TickOutgoingTransfers()
{
	if (OutgoingTransfers.Num() == 0)
	{
		return;
	}

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	int32 RemainingBudget = Settings ? FMath::Max(1, Settings->MaxStateFragmentsPerTick) : 4;
	int32 NumToRemove = 0;
	
	for (int32 i = 0; i < OutgoingTransfers.Num() && RemainingBudget > 0; )
	{
		FOutgoingTransfer& Transfer = OutgoingTransfers[i];
		const int32 FragmentCount = FMath::DivideAndRoundUp(Transfer.Packet.Num(), Transfer.FragmentSize);
		if (FragmentCount <= 0 || FragmentCount > 8192)
		{
			QueuedOutgoingBytes -= Transfer.Packet.Num();
			NumToRemove++;
			i++;
			continue;
		}

		const int32 Offset = Transfer.NextFragmentIndex * Transfer.FragmentSize;
		const int32 BytesThisFragment = FMath::Min(Transfer.FragmentSize, Transfer.Packet.Num() - Offset);
		TArray<uint8> FragmentBytes;
		FragmentBytes.Append(Transfer.Packet.GetData() + Offset, BytesThisFragment);
		Client_ReceiveChunkStateFragment(
			Transfer.TransferId,
			Transfer.Coordinate,
			Transfer.NextFragmentIndex,
			FragmentCount,
			Transfer.Packet.Num(),
			FragmentBytes);

		++Transfer.NextFragmentIndex;
		--RemainingBudget;
		if (Transfer.NextFragmentIndex >= FragmentCount)
		{
			QueuedOutgoingBytes -= Transfer.Packet.Num();
			NumToRemove++;
			i++;
		}
	}

	if (NumToRemove > 0)
	{
		// TODO: Consider TQueue or circular buffer for O(1) removal
		OutgoingTransfers.RemoveAt(0, NumToRemove, EAllowShrinking::No);
	}
}

void UTerraDyneReplicationComponent::DropOldestIncomingTransfer()
{
	int32 OldestId = INDEX_NONE;
	double OldestTime = TNumericLimits<double>::Max();
	for (const auto& Pair : IncomingTransfers)
	{
		if (Pair.Value.LastUpdateSeconds < OldestTime)
		{
			OldestTime = Pair.Value.LastUpdateSeconds;
			OldestId = Pair.Key;
		}
	}
	if (OldestId != INDEX_NONE)
	{
		IncomingTransfers.Remove(OldestId);
	}
}

void UTerraDyneReplicationComponent::Client_ReceiveChunkStateFragment_Implementation(
	int32 TransferId,
	FIntPoint Coordinate,
	int32 FragmentIndex,
	int32 FragmentCount,
	int32 TotalPayloadBytes,
	const TArray<uint8>& FragmentBytes)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxPacket = Settings ? Settings->MaxCompressedChunkStateBytes : 8 * 1024 * 1024;
	const int32 MaxFragment = Settings ? FMath::Clamp(Settings->StateFragmentSizeBytes, 1024, 48 * 1024) : 48 * 1024;
	const int32 MaxTransfers = Settings ? FMath::Max(1, Settings->MaxPendingStateTransfers) : 8;
	if (TransferId <= 0 || FragmentCount <= 0 || FragmentCount > 8192 ||
		FragmentIndex < 0 || FragmentIndex >= FragmentCount ||
		TotalPayloadBytes <= 0 || TotalPayloadBytes > MaxPacket ||
		FragmentBytes.Num() <= 0 || FragmentBytes.Num() > MaxFragment)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Rejected invalid TerraDyne state fragment metadata."));
		return;
	}

	FIncomingTransfer* Transfer = IncomingTransfers.Find(TransferId);
	if (!Transfer)
	{
		if (IncomingTransfers.Num() >= MaxTransfers)
		{
			DropOldestIncomingTransfer();
		}
		FIncomingTransfer& NewTransfer = IncomingTransfers.Add(TransferId);
		NewTransfer.Coordinate = Coordinate;
		NewTransfer.FragmentCount = FragmentCount;
		NewTransfer.TotalPayloadBytes = TotalPayloadBytes;
		NewTransfer.Fragments.SetNum(FragmentCount);
		NewTransfer.ReceivedFragments.Init(false, FragmentCount);
		Transfer = &NewTransfer;
	}

	if (Transfer->Coordinate != Coordinate || Transfer->FragmentCount != FragmentCount ||
		Transfer->TotalPayloadBytes != TotalPayloadBytes)
	{
		IncomingTransfers.Remove(TransferId);
		UE_LOG(LogTerraDyne, Warning, TEXT("Discarded inconsistent TerraDyne state transfer %d."), TransferId);
		return;
	}

	Transfer->LastUpdateSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!Transfer->ReceivedFragments[FragmentIndex])
	{
		Transfer->Fragments[FragmentIndex] = FragmentBytes;
		Transfer->ReceivedFragments[FragmentIndex] = true;
		Transfer->ReceivedBytes += FragmentBytes.Num();
	}
	if (Transfer->ReceivedFragments.CountSetBits() != FragmentCount)
	{
		return;
	}
	if (Transfer->ReceivedBytes != TotalPayloadBytes)
	{
		IncomingTransfers.Remove(TransferId);
		UE_LOG(LogTerraDyne, Warning, TEXT("Discarded TerraDyne state transfer %d with an invalid assembled size."), TransferId);
		return;
	}

	TArray<uint8> Packet;
	Packet.Reserve(TotalPayloadBytes);
	for (const TArray<uint8>& Fragment : Transfer->Fragments)
	{
		Packet.Append(Fragment);
	}
	IncomingTransfers.Remove(TransferId);

	FTerraDyneChunkData Decoded;
	FString Error;
	const int32 MaxUncompressed = Settings ? Settings->MaxUncompressedChunkStateBytes : 64 * 1024 * 1024;
	if (!FTerraDyneStateCodec::DecodeChunk(Packet, Decoded, &Error, MaxPacket, MaxUncompressed) ||
		Decoded.Coordinate != Coordinate)
	{
		UE_LOG(LogTerraDyne, Warning, TEXT("Rejected TerraDyne chunk state transfer %d: %s"), TransferId, *Error);
		return;
	}
	OutstandingChunkStateRequests.Remove(Coordinate);
	QueuedChunkStateRequests.Remove(Coordinate);
	ApplyOrQueueDecodedState(MoveTemp(Decoded));
}

void UTerraDyneReplicationComponent::ApplyOrQueueDecodedState(FTerraDyneChunkData&& Data)
{
	if (ATerraDyneManager* Manager = GetTerrainManager())
	{
		if (ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(Data.Coordinate))
		{
			Chunk->LoadFromData(Data);
			return;
		}
	}

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const int32 MaxPendingDecoded = FMath::Max(4, (Settings ? Settings->MaxPendingStateTransfers : 8) * 4);
	if (!PendingDecodedStates.Contains(Data.Coordinate) && PendingDecodedStates.Num() >= MaxPendingDecoded)
	{
		if (DecodedStateInsertionOrder.Num() > 0)
		{
			FIntPoint OldestCoord = DecodedStateInsertionOrder[0];
			DecodedStateInsertionOrder.RemoveAt(0);
			PendingDecodedStates.Remove(OldestCoord);
		}
		else
		{
			// NOTE: TMap iteration order is non-deterministic. This evicts a pseudo-random entry.
			// TODO: Track insertion order with TQueue<FIntPoint> for proper FIFO eviction.
			auto It = PendingDecodedStates.CreateIterator();
			if (It)
			{
				It.RemoveCurrent();
			}
		}
	}
	if (!PendingDecodedStates.Contains(Data.Coordinate))
	{
		DecodedStateInsertionOrder.Add(Data.Coordinate);
	}
	PendingDecodedStates.Add(Data.Coordinate, MoveTemp(Data));
}

void UTerraDyneReplicationComponent::Client_ApplyBrush_Implementation(const FTerraDyneBrushParams& Params)
{
	if (ATerraDyneManager* Manager = GetTerrainManager())
	{
		Manager->ApplyGlobalBrush(
			Params.WorldLocation,
			Params.Radius,
			Params.Strength,
			Params.BrushMode,
			Params.TargetLayer,
			Params.WeightLayerIndex,
			Params.FlattenHeight);
	}
}

void UTerraDyneReplicationComponent::QueueChunkStateRequest(FIntPoint Coordinate)
{
	APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || Controller->HasAuthority() || !Controller->IsLocalController())
	{
		return;
	}
	QueuedChunkStateRequests.Add(Coordinate);
}

void UTerraDyneReplicationComponent::TickIncomingTransfers(double NowSeconds)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float Timeout = Settings ? FMath::Max(1.0f, Settings->StateTransferTimeoutSeconds) : 15.0f;
	for (auto It = IncomingTransfers.CreateIterator(); It; ++It)
	{
		if (NowSeconds - It.Value().LastUpdateSeconds > Timeout)
		{
			It.RemoveCurrent();
		}
	}
}

void UTerraDyneReplicationComponent::TickDecodedStates()
{
	ATerraDyneManager* Manager = GetTerrainManager();
	if (!Manager || PendingDecodedStates.Num() == 0)
	{
		return;
	}

	for (auto It = PendingDecodedStates.CreateIterator(); It; ++It)
	{
		if (ATerraDyneChunk* Chunk = Manager->GetChunkAtCoord(It.Key()))
		{
			Chunk->LoadFromData(It.Value());
			DecodedStateInsertionOrder.RemoveSingle(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UTerraDyneReplicationComponent::TickChunkStateRequests(double NowSeconds)
{
	APlayerController* Controller = GetOwningPlayerController();
	if (!Controller || Controller->HasAuthority() || !Controller->IsLocalController())
	{
		return;
	}

	for (auto It = OutstandingChunkStateRequests.CreateIterator(); It; ++It)
	{
		if (NowSeconds - It.Value() > 5.0)
		{
			QueuedChunkStateRequests.Add(It.Key());
			It.RemoveCurrent();
		}
	}

	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	const float Rate = Settings ? FMath::Max(1.0f, Settings->MaxChunkStateRequestsPerSecond) : 16.0f;
	if (QueuedChunkStateRequests.Num() == 0 || NowSeconds - LastClientRequestSendSeconds < (1.0 / Rate))
	{
		return;
	}

	auto It = QueuedChunkStateRequests.CreateIterator();
	if (It)
	{
		const FIntPoint Coordinate = *It;
		It.RemoveCurrent();
		OutstandingChunkStateRequests.Add(Coordinate, NowSeconds);
		LastClientRequestSendSeconds = NowSeconds;
		Server_RequestChunkState(Coordinate);
	}
}

void UTerraDyneReplicationComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		TickOutgoingTransfers();
	}
	else
	{
		TickIncomingTransfers(Now);
		TickDecodedStates();
		TickChunkStateRequests(Now);
	}
}
