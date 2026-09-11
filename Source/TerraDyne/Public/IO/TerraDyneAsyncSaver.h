// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneSaveGame.h"

DECLARE_DELEGATE_TwoParams(FOnTerraDyneAsyncSaveComplete, bool /* bSuccess */, const FString& /* Error */);

/** Thread-pool backed atomic byte writer used by the streaming cache. */
class TERRADYNE_API FTerraDyneAsyncSaver
{
public:
	static void SaveBytesAtomically(
		FString DestinationPath,
		TArray<uint8> Bytes,
		FOnTerraDyneAsyncSaveComplete Completion = FOnTerraDyneAsyncSaveComplete());

	/** Encode, compress, CRC, and atomically write a chunk entirely off the game thread. */
	static void SaveChunkStateAtomically(
		FString DestinationPath,
		FTerraDyneChunkData Data,
		int32 MaxUncompressedBytes,
		int32 MaxPacketBytes,
		FOnTerraDyneAsyncSaveComplete Completion = FOnTerraDyneAsyncSaveComplete());

	static bool SaveBytesAtomicallySync(
		const FString& DestinationPath,
		const TArray<uint8>& Bytes,
		FString& OutError);
};
