// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TerraDyneSaveGame.h"
#include "World/TerraDyneTileData.h"
#include "TerraDyneReplicationComponent.generated.h"

class APlayerController;
class ATerraDyneManager;

/**
 * Optional connection-owned replication bridge.
 *
 * Add this component to any PlayerController that needs TerraDyne state. It keeps large chunk
 * structs out of RPC parameters by compressing, fragmenting, throttling, validating, and
 * reassembling state below the engine's constructed-bunch limit.
 */
UCLASS(ClassGroup = (TerraDyne), meta = (BlueprintSpawnableComponent))
class TERRADYNE_API UTerraDyneReplicationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTerraDyneReplicationComponent();

	/** Server-only: enqueue one authoritative chunk state for this owning connection. */
	bool SendChunkState(const FTerraDyneChunkData& Data);

	/** Client-side: request authoritative state when a replicated/streamed chunk becomes available. */
	void QueueChunkStateRequest(FIntPoint Coordinate);

	APlayerController* GetOwningPlayerController() const;

	UFUNCTION(Server, Reliable)
	void Server_RequestFullSync();

	UFUNCTION(Server, Reliable)
	void Server_RequestChunkState(FIntPoint Coordinate);

	UFUNCTION(Client, Reliable)
	void Client_ReceiveChunkStateFragment(
		int32 TransferId,
		FIntPoint Coordinate,
		int32 FragmentIndex,
		int32 FragmentCount,
		int32 TotalPayloadBytes,
		const TArray<uint8>& FragmentBytes);

	/** Server-to-client authoritative edit event, routed per connection rather than globally. */
	UFUNCTION(Client, Reliable)
	void Client_ApplyBrush(const FTerraDyneBrushParams& Params);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FIncomingTransfer
	{
		FIntPoint Coordinate = FIntPoint::ZeroValue;
		int32 FragmentCount = 0;
		int32 TotalPayloadBytes = 0;
		int32 ReceivedBytes = 0;
		double LastUpdateSeconds = 0.0;
		TArray<TArray<uint8>> Fragments;
		TBitArray<> ReceivedFragments;
	};

	struct FOutgoingTransfer
	{
		int32 TransferId = 0;
		FIntPoint Coordinate = FIntPoint::ZeroValue;
		int32 FragmentSize = 0;
		int32 NextFragmentIndex = 0;
		TArray<uint8> Packet;
	};

	ATerraDyneManager* GetTerrainManager() const;
	bool IsCoordinateRelevantToOwner(FIntPoint Coordinate) const;
	bool ConsumeStateRequestToken();
	void TickOutgoingTransfers();
	void TickIncomingTransfers(double NowSeconds);
	void TickDecodedStates();
	void TickChunkStateRequests(double NowSeconds);
	void ApplyOrQueueDecodedState(FTerraDyneChunkData&& Data);
	void DropOldestIncomingTransfer();

	TMap<int32, FIncomingTransfer> IncomingTransfers;
	TArray<FOutgoingTransfer> OutgoingTransfers;
	TMap<FIntPoint, FTerraDyneChunkData> PendingDecodedStates;
	TArray<FIntPoint> DecodedStateInsertionOrder;
	TSet<FIntPoint> QueuedChunkStateRequests;
	TMap<FIntPoint, double> OutstandingChunkStateRequests;

	int32 NextTransferId = 1;
	int32 QueuedOutgoingBytes = 0;
	float AvailableStateRequestTokens = -1.0f;
	double LastStateRequestRefillSeconds = 0.0;
	double LastFullSyncRequestSeconds = -1000000.0;
	double LastClientRequestSendSeconds = 0.0;
};
