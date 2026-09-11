// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/TerraDyneEditController.h"
#include "IO/TerraDyneSerializer.h"
#include "Settings/TerraDyneSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace TerraDyneProductionTests
{
	FTerraDyneChunkData MakeChunkData(int32 Resolution = 32)
	{
		FTerraDyneChunkData Data;
		Data.Coordinate = FIntPoint(7, -3);
		Data.Resolution = Resolution;
		Data.ZScale = 3000.0f;
		const int32 Samples = Resolution * Resolution;
		Data.HeightData.SetNumUninitialized(Samples);
		Data.BaseData.SetNumUninitialized(Samples);
		Data.SculptData.SetNumZeroed(Samples);
		Data.DetailData.SetNumZeroed(Samples);
		Data.WeightData.SetNumZeroed(Samples * 4);
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			const float Height = static_cast<float>(Index % Resolution) / static_cast<float>(Resolution - 1);
			Data.HeightData[Index] = Height;
			Data.BaseData[Index] = Height;
			Data.WeightData[Index * 4] = static_cast<uint8>(Index % 256);
		}
		return Data;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneStateCodecRoundTripTest,
	"TerraDyne.Production.StateCodec.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneStateCodecRoundTripTest::RunTest(const FString& Parameters)
{
	const FTerraDyneChunkData Source = TerraDyneProductionTests::MakeChunkData();
	TArray<uint8> Packet;
	FString Error;
	TestTrue(TEXT("A valid chunk should encode"), FTerraDyneStateCodec::EncodeChunk(Source, Packet, &Error));
	TestTrue(TEXT("Encoded packet should have a versioned header"), FTerraDyneStateCodec::IsVersionedPacket(Packet));
	TestTrue(TEXT("Encoded packet should be non-empty"), Packet.Num() > 0);

	FTerraDyneChunkData Decoded;
	TestTrue(TEXT("A valid packet should decode"), FTerraDyneStateCodec::DecodeChunk(Packet, Decoded, &Error));
	TestEqual(TEXT("Coordinate should round-trip"), Decoded.Coordinate, Source.Coordinate);
	TestEqual(TEXT("Resolution should round-trip"), Decoded.Resolution, Source.Resolution);
	TestEqual(TEXT("Height samples should round-trip"), Decoded.HeightData.Num(), Source.HeightData.Num());
	TestTrue(TEXT("Height values should round-trip exactly"), Decoded.HeightData == Source.HeightData);
	TestTrue(TEXT("RGBA weights should round-trip exactly"), Decoded.WeightData == Source.WeightData);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneStateCodecIntegrityTest,
	"TerraDyne.Production.StateCodec.IntegrityAndLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneStateCodecIntegrityTest::RunTest(const FString& Parameters)
{
	FTerraDyneChunkData Source = TerraDyneProductionTests::MakeChunkData();
	TArray<uint8> Packet;
	FString Error;
	TestTrue(TEXT("Test state should encode"), FTerraDyneStateCodec::EncodeChunk(Source, Packet, &Error));

	FTerraDyneChunkData Decoded;
	TestFalse(
		TEXT("Decoder should enforce the configured packet limit"),
		FTerraDyneStateCodec::DecodeChunk(Packet, Decoded, &Error, Packet.Num() - 1));

	// Corrupt the header CRC rather than the compressed payload so the test does not intentionally
	// emit an engine-level Zlib error (automation treats Error logs as test failures).
	constexpr int32 CrcOffset = sizeof(uint32) + (2 * sizeof(uint16)) + (2 * sizeof(int32));
	if (Packet.IsValidIndex(CrcOffset))
	{
		Packet[CrcOffset] ^= 0x5A;
	}
	TestFalse(TEXT("Corrupt state should fail CRC validation"),
		FTerraDyneStateCodec::DecodeChunk(Packet, Decoded, &Error));

	Source.HeightData.Pop();
	TestFalse(TEXT("Inconsistent sample arrays should be rejected before encoding"),
		FTerraDyneStateCodec::EncodeChunk(Source, Packet, &Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTerraDyneSafeProductionDefaultsTest,
	"TerraDyne.Production.SafeDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerraDyneSafeProductionDefaultsTest::RunTest(const FString& Parameters)
{
	const UTerraDyneSettings* Settings = GetDefault<UTerraDyneSettings>();
	TestNotNull(TEXT("TerraDyne settings should exist"), Settings);
	if (Settings)
	{
		TestTrue(TEXT("State fragments must remain below 64 KiB"), Settings->StateFragmentSizeBytes <= 48 * 1024);
		TestTrue(TEXT("State packet limit should be positive"), Settings->MaxCompressedChunkStateBytes > 0);
		TestFalse(TEXT("Manager should not be globally relevant by default"), Settings->bManagerAlwaysRelevant);
		TestTrue(TEXT("Chunk cache writes should be asynchronous by default"), Settings->bUseAsyncChunkCacheWrites);
	}

	const ATerraDyneEditController* ControllerCDO = GetDefault<ATerraDyneEditController>();
	TestNotNull(TEXT("Edit controller CDO should exist"), ControllerCDO);
	if (ControllerCDO)
	{
		TestFalse(TEXT("Remote terrain editing must be opt-in"), ControllerCDO->bAllowRemoteTerrainEditing);
		TestNotNull(TEXT("Built-in controller should own a reusable replication component"), ControllerCDO->TerraDyneReplication.Get());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
