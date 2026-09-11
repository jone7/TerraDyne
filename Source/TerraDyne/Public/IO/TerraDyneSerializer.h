// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Core/TerraDyneSaveGame.h"

/**
 * Versioned, bounded codec shared by chunk cache persistence and network state transfer.
 *
 * The encoded packet contains a fixed header, an optional Zlib payload, and a CRC of the
 * uncompressed data. Decoders reject unsupported versions, inconsistent array sizes, corrupt
 * payloads, and configured size-limit violations before a chunk is applied.
 */
class TERRADYNE_API FTerraDyneStateCodec
{
public:
	static constexpr int32 DefaultMaxUncompressedBytes = 64 * 1024 * 1024;
	static constexpr int32 DefaultMaxPacketBytes = 16 * 1024 * 1024;

	static bool EncodeChunk(
		const FTerraDyneChunkData& Data,
		TArray<uint8>& OutPacket,
		FString* OutError = nullptr,
		int32 MaxUncompressedBytes = DefaultMaxUncompressedBytes);

	static bool DecodeChunk(
		const TArray<uint8>& Packet,
		FTerraDyneChunkData& OutData,
		FString* OutError = nullptr,
		int32 MaxPacketBytes = DefaultMaxPacketBytes,
		int32 MaxUncompressedBytes = DefaultMaxUncompressedBytes);

	/** Decode the pre-0.6 cache layout: [UncompressedSize:int32][Zlib payload]. */
	static bool DecodeLegacyCache(
		const TArray<uint8>& LegacyPacket,
		FTerraDyneChunkData& OutData,
		FString* OutError = nullptr,
		int32 MaxUncompressedBytes = DefaultMaxUncompressedBytes);

	static bool IsVersionedPacket(const TArray<uint8>& Packet);
	static bool ValidateChunkData(const FTerraDyneChunkData& Data, FString* OutError = nullptr);

private:
	static constexpr uint32 PacketMagic = 0x48434454; // "TDCH" in a little-endian byte stream.
	static constexpr uint16 PacketVersion = 1;
	static constexpr uint16 FlagCompressed = 1 << 0;

	static bool SerializeChunkData(const FTerraDyneChunkData& Data, TArray<uint8>& OutRaw, FString* OutError);
	static bool DeserializeChunkData(const TArray<uint8>& Raw, FTerraDyneChunkData& OutData, FString* OutError);
	static bool DeserializeLegacyChunkData(const TArray<uint8>& Raw, FTerraDyneChunkData& OutData, FString* OutError);
};
