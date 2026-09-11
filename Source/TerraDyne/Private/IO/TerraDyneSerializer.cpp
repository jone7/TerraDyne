// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneSerializer.h"

#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	void SetCodecError(FString* OutError, const FString& Message)
	{
		if (OutError)
		{
			*OutError = Message;
		}
	}

	void SerializeChunkFields(FArchive& Ar, FTerraDyneChunkData& Data)
	{
		Ar << Data.Coordinate;
		Ar << Data.Resolution;
		Ar << Data.ZScale;
		Ar << Data.HeightData;
		Ar << Data.BaseData;
		Ar << Data.SculptData;
		Ar << Data.DetailData;
		Ar << Data.WeightData;
		Ar << Data.bTransferredFoliageFollowsTerrain;
		Ar << Data.FoliageStaticMeshPaths;
		Ar << Data.FoliageMaterialCounts;
		Ar << Data.FoliageOverrideMaterialPaths;
		Ar << Data.FoliageDefinitionIndices;
		Ar << Data.FoliageInstanceLocalTransforms;
		Ar << Data.FoliageInstanceTerrainOffsets;
		Ar << Data.ActorFoliageClassPaths;
		Ar << Data.ActorFoliageAttachFlags;
		Ar << Data.ActorFoliageDefinitionIndices;
		Ar << Data.ActorFoliageInstanceLocalTransforms;
		Ar << Data.ActorFoliageInstanceTerrainOffsets;
	}

	bool IsFiniteArray(const TArray<float>& Values)
	{
		for (const float Value : Values)
		{
			if (!FMath::IsFinite(Value))
			{
				return false;
			}
		}
		return true;
	}
}

bool FTerraDyneStateCodec::ValidateChunkData(const FTerraDyneChunkData& Data, FString* OutError)
{
	if (Data.Resolution < 2 || Data.Resolution > 4096)
	{
		SetCodecError(OutError, FString::Printf(TEXT("Invalid chunk resolution %d."), Data.Resolution));
		return false;
	}

	if (!FMath::IsFinite(Data.ZScale) || Data.ZScale <= 0.0f)
	{
		SetCodecError(OutError, TEXT("Chunk ZScale must be finite and positive."));
		return false;
	}

	const int64 ExpectedSamples64 = static_cast<int64>(Data.Resolution) * Data.Resolution;
	if (ExpectedSamples64 > MAX_int32)
	{
		SetCodecError(OutError, TEXT("Chunk sample count overflow."));
		return false;
	}
	const int32 ExpectedSamples = static_cast<int32>(ExpectedSamples64);

	if (Data.HeightData.Num() != ExpectedSamples)
	{
		SetCodecError(OutError, FString::Printf(
			TEXT("HeightData contains %d samples; expected %d."), Data.HeightData.Num(), ExpectedSamples));
		return false;
	}

	auto ValidateOptionalFloatLayer = [ExpectedSamples, OutError](const TCHAR* Name, const TArray<float>& Layer) -> bool
	{
		if (Layer.Num() != 0 && Layer.Num() != ExpectedSamples)
		{
			SetCodecError(OutError, FString::Printf(
				TEXT("%s contains %d samples; expected zero or %d."), Name, Layer.Num(), ExpectedSamples));
			return false;
		}
		if (!IsFiniteArray(Layer))
		{
			SetCodecError(OutError, FString::Printf(TEXT("%s contains a non-finite value."), Name));
			return false;
		}
		return true;
	};

	if (!IsFiniteArray(Data.HeightData) ||
		!ValidateOptionalFloatLayer(TEXT("BaseData"), Data.BaseData) ||
		!ValidateOptionalFloatLayer(TEXT("SculptData"), Data.SculptData) ||
		!ValidateOptionalFloatLayer(TEXT("DetailData"), Data.DetailData))
	{
		if (OutError && OutError->IsEmpty())
		{
			*OutError = TEXT("HeightData contains a non-finite value.");
		}
		return false;
	}

	const int64 ExpectedWeightBytes64 = ExpectedSamples64 * 4;
	if (ExpectedWeightBytes64 > MAX_int32 ||
		(Data.WeightData.Num() != 0 && Data.WeightData.Num() != static_cast<int32>(ExpectedWeightBytes64)))
	{
		SetCodecError(OutError, FString::Printf(
			TEXT("WeightData contains %d bytes; expected zero or %lld."),
			Data.WeightData.Num(), ExpectedWeightBytes64));
		return false;
	}

	if (Data.FoliageDefinitionIndices.Num() != Data.FoliageInstanceLocalTransforms.Num() ||
		(Data.FoliageInstanceTerrainOffsets.Num() != 0 &&
		 Data.FoliageInstanceTerrainOffsets.Num() != Data.FoliageInstanceLocalTransforms.Num()))
	{
		SetCodecError(OutError, TEXT("Static-mesh foliage instance arrays have inconsistent lengths."));
		return false;
	}

	if (Data.ActorFoliageDefinitionIndices.Num() != Data.ActorFoliageInstanceLocalTransforms.Num() ||
		(Data.ActorFoliageInstanceTerrainOffsets.Num() != 0 &&
		 Data.ActorFoliageInstanceTerrainOffsets.Num() != Data.ActorFoliageInstanceLocalTransforms.Num()))
	{
		SetCodecError(OutError, TEXT("Actor foliage instance arrays have inconsistent lengths."));
		return false;
	}

	for (const int32 DefinitionIndex : Data.FoliageDefinitionIndices)
	{
		if (!Data.FoliageStaticMeshPaths.IsValidIndex(DefinitionIndex))
		{
			SetCodecError(OutError, TEXT("Static-mesh foliage references an invalid definition index."));
			return false;
		}
	}
	for (const int32 DefinitionIndex : Data.ActorFoliageDefinitionIndices)
	{
		if (!Data.ActorFoliageClassPaths.IsValidIndex(DefinitionIndex))
		{
			SetCodecError(OutError, TEXT("Actor foliage references an invalid definition index."));
			return false;
		}
	}

	return true;
}

bool FTerraDyneStateCodec::SerializeChunkData(
	const FTerraDyneChunkData& Data,
	TArray<uint8>& OutRaw,
	FString* OutError)
{
	if (!ValidateChunkData(Data, OutError))
	{
		return false;
	}

	OutRaw.Reset();
	FMemoryWriter Writer(OutRaw, true);
	uint16 SchemaVersion = 1;
	Writer << SchemaVersion;
	FTerraDyneChunkData MutableData = Data;
	SerializeChunkFields(Writer, MutableData);
	if (Writer.IsError())
	{
		SetCodecError(OutError, TEXT("Failed to serialize chunk state."));
		OutRaw.Reset();
		return false;
	}
	return true;
}

bool FTerraDyneStateCodec::DeserializeChunkData(
	const TArray<uint8>& Raw,
	FTerraDyneChunkData& OutData,
	FString* OutError)
{
	FMemoryReader Reader(Raw, true);
	uint16 SchemaVersion = 0;
	Reader << SchemaVersion;
	if (Reader.IsError() || SchemaVersion != 1)
	{
		SetCodecError(OutError, FString::Printf(TEXT("Unsupported chunk schema version %u."), SchemaVersion));
		return false;
	}

	FTerraDyneChunkData Decoded;
	SerializeChunkFields(Reader, Decoded);
	if (Reader.IsError() || !Reader.AtEnd())
	{
		SetCodecError(OutError, TEXT("Chunk state payload is truncated or contains trailing data."));
		return false;
	}
	if (!ValidateChunkData(Decoded, OutError))
	{
		return false;
	}
	OutData = MoveTemp(Decoded);
	return true;
}

bool FTerraDyneStateCodec::EncodeChunk(
	const FTerraDyneChunkData& Data,
	TArray<uint8>& OutPacket,
	FString* OutError,
	int32 MaxUncompressedBytes)
{
	TArray<uint8> Raw;
	if (!SerializeChunkData(Data, Raw, OutError))
	{
		return false;
	}
	if (Raw.Num() <= 0 || Raw.Num() > MaxUncompressedBytes)
	{
		SetCodecError(OutError, FString::Printf(
			TEXT("Serialized chunk size %d exceeds the %d-byte limit."), Raw.Num(), MaxUncompressedBytes));
		return false;
	}

	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num()));
	int32 CompressedSize = Compressed.Num();
	const bool bCompressed = FCompression::CompressMemory(
		NAME_Zlib, Compressed.GetData(), CompressedSize, Raw.GetData(), Raw.Num()) &&
		CompressedSize < Raw.Num();

	const TArray<uint8>* Payload = &Raw;
	uint16 Flags = 0;
	if (bCompressed)
	{
		Compressed.SetNum(CompressedSize);
		Payload = &Compressed;
		Flags |= FlagCompressed;
	}

	OutPacket.Reset();
	FMemoryWriter Writer(OutPacket, true);
	uint32 Magic = PacketMagic;
	uint16 Version = PacketVersion;
	int32 UncompressedSize = Raw.Num();
	int32 PayloadSize = Payload->Num();
	uint32 RawCrc = FCrc::MemCrc32(Raw.GetData(), Raw.Num());
	Writer << Magic;
	Writer << Version;
	Writer << Flags;
	Writer << UncompressedSize;
	Writer << PayloadSize;
	Writer << RawCrc;
	Writer.Serialize(const_cast<uint8*>(Payload->GetData()), PayloadSize);

	if (Writer.IsError())
	{
		SetCodecError(OutError, TEXT("Failed to build the encoded chunk packet."));
		OutPacket.Reset();
		return false;
	}
	return true;
}

bool FTerraDyneStateCodec::IsVersionedPacket(const TArray<uint8>& Packet)
{
	if (Packet.Num() < static_cast<int32>(sizeof(uint32)))
	{
		return false;
	}
	uint32 Magic = 0;
	FMemory::Memcpy(&Magic, Packet.GetData(), sizeof(uint32));
	return Magic == PacketMagic;
}

bool FTerraDyneStateCodec::DecodeChunk(
	const TArray<uint8>& Packet,
	FTerraDyneChunkData& OutData,
	FString* OutError,
	int32 MaxPacketBytes,
	int32 MaxUncompressedBytes)
{
	constexpr int32 HeaderBytes = sizeof(uint32) + (2 * sizeof(uint16)) + (3 * sizeof(uint32));
	if (Packet.Num() < HeaderBytes || Packet.Num() > MaxPacketBytes)
	{
		SetCodecError(OutError, FString::Printf(TEXT("Encoded chunk packet size %d is invalid."), Packet.Num()));
		return false;
	}

	FMemoryReader Reader(Packet, true);
	uint32 Magic = 0;
	uint16 Version = 0;
	uint16 Flags = 0;
	int32 UncompressedSize = 0;
	int32 PayloadSize = 0;
	uint32 ExpectedCrc = 0;
	Reader << Magic;
	Reader << Version;
	Reader << Flags;
	Reader << UncompressedSize;
	Reader << PayloadSize;
	Reader << ExpectedCrc;

	if (Reader.IsError() || Magic != PacketMagic || Version != PacketVersion ||
		(Flags & ~FlagCompressed) != 0)
	{
		SetCodecError(OutError, TEXT("Encoded chunk packet header is invalid or unsupported."));
		return false;
	}
	if (UncompressedSize <= 0 || UncompressedSize > MaxUncompressedBytes ||
		PayloadSize <= 0 || PayloadSize > MaxPacketBytes ||
		Reader.Tell() + PayloadSize != Packet.Num())
	{
		SetCodecError(OutError, TEXT("Encoded chunk packet declares invalid payload sizes."));
		return false;
	}

	TArray<uint8> Raw;
	Raw.SetNumUninitialized(UncompressedSize);
	const uint8* PayloadData = Packet.GetData() + Reader.Tell();
	if ((Flags & FlagCompressed) != 0)
	{
		if (!FCompression::UncompressMemory(
			NAME_Zlib, Raw.GetData(), UncompressedSize, PayloadData, PayloadSize))
		{
			SetCodecError(OutError, TEXT("Failed to decompress chunk state."));
			return false;
		}
	}
	else
	{
		if (PayloadSize != UncompressedSize)
		{
			SetCodecError(OutError, TEXT("Uncompressed chunk packet sizes do not match."));
			return false;
		}
		FMemory::Memcpy(Raw.GetData(), PayloadData, PayloadSize);
	}

	if (FCrc::MemCrc32(Raw.GetData(), Raw.Num()) != ExpectedCrc)
	{
		SetCodecError(OutError, TEXT("Chunk state CRC validation failed."));
		return false;
	}

	return DeserializeChunkData(Raw, OutData, OutError);
}

bool FTerraDyneStateCodec::DeserializeLegacyChunkData(
	const TArray<uint8>& Raw,
	FTerraDyneChunkData& OutData,
	FString* OutError)
{
	FMemoryReader Reader(Raw, true);
	FTerraDyneChunkData Decoded;
	Reader << Decoded.Coordinate;
	Reader << Decoded.Resolution;
	Reader << Decoded.ZScale;
	Reader << Decoded.HeightData;
	Reader << Decoded.BaseData;
	Reader << Decoded.SculptData;
	Reader << Decoded.DetailData;
	Reader << Decoded.WeightData;
	if (!Reader.AtEnd()) Reader << Decoded.bTransferredFoliageFollowsTerrain;
	if (!Reader.AtEnd()) Reader << Decoded.FoliageStaticMeshPaths;
	if (!Reader.AtEnd()) Reader << Decoded.FoliageMaterialCounts;
	if (!Reader.AtEnd()) Reader << Decoded.FoliageOverrideMaterialPaths;
	if (!Reader.AtEnd()) Reader << Decoded.FoliageDefinitionIndices;
	if (!Reader.AtEnd()) Reader << Decoded.FoliageInstanceLocalTransforms;
	if (!Reader.AtEnd()) Reader << Decoded.FoliageInstanceTerrainOffsets;
	if (!Reader.AtEnd()) Reader << Decoded.ActorFoliageClassPaths;
	if (!Reader.AtEnd()) Reader << Decoded.ActorFoliageAttachFlags;
	if (!Reader.AtEnd()) Reader << Decoded.ActorFoliageDefinitionIndices;
	if (!Reader.AtEnd()) Reader << Decoded.ActorFoliageInstanceLocalTransforms;
	if (!Reader.AtEnd()) Reader << Decoded.ActorFoliageInstanceTerrainOffsets;

	if (Reader.IsError() || !ValidateChunkData(Decoded, OutError))
	{
		if (OutError && OutError->IsEmpty())
		{
			*OutError = TEXT("Legacy chunk payload is invalid.");
		}
		return false;
	}
	OutData = MoveTemp(Decoded);
	return true;
}

bool FTerraDyneStateCodec::DecodeLegacyCache(
	const TArray<uint8>& LegacyPacket,
	FTerraDyneChunkData& OutData,
	FString* OutError,
	int32 MaxUncompressedBytes)
{
	if (LegacyPacket.Num() <= static_cast<int32>(sizeof(int32)))
	{
		SetCodecError(OutError, TEXT("Legacy chunk cache is too small."));
		return false;
	}

	int32 UncompressedSize = 0;
	FMemory::Memcpy(&UncompressedSize, LegacyPacket.GetData(), sizeof(int32));
	if (UncompressedSize <= 0 || UncompressedSize > MaxUncompressedBytes)
	{
		SetCodecError(OutError, TEXT("Legacy chunk cache declares an invalid uncompressed size."));
		return false;
	}

	TArray<uint8> Raw;
	Raw.SetNumUninitialized(UncompressedSize);
	if (!FCompression::UncompressMemory(
		NAME_Zlib,
		Raw.GetData(),
		UncompressedSize,
		LegacyPacket.GetData() + sizeof(int32),
		LegacyPacket.Num() - sizeof(int32)))
	{
		SetCodecError(OutError, TEXT("Failed to decompress legacy chunk cache."));
		return false;
	}
	return DeserializeLegacyChunkData(Raw, OutData, OutError);
}
