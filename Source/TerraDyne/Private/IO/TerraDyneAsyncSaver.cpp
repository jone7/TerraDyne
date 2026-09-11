// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "IO/TerraDyneAsyncSaver.h"
#include "IO/TerraDyneSerializer.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

bool FTerraDyneAsyncSaver::SaveBytesAtomicallySync(
	const FString& DestinationPath,
	const TArray<uint8>& Bytes,
	FString& OutError)
{
	if (DestinationPath.IsEmpty() || Bytes.Num() == 0)
	{
		OutError = TEXT("Destination path or payload is empty.");
		return false;
	}

	const FString Directory = FPaths::GetPath(DestinationPath);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = FString::Printf(TEXT("Failed to create cache directory '%s'."), *Directory);
		return false;
	}

	const FString TempPath = FString::Printf(
		TEXT("%s.tmp.%s"),
		*DestinationPath,
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	if (!FFileHelper::SaveArrayToFile(Bytes, *TempPath))
	{
		OutError = FString::Printf(TEXT("Failed to write temporary file '%s'."), *TempPath);
		return false;
	}

	if (!IFileManager::Get().Move(*DestinationPath, *TempPath, true, true, false, true))
	{
		IFileManager::Get().Delete(*TempPath, false, true, true);
		OutError = FString::Printf(TEXT("Failed to atomically replace '%s'."), *DestinationPath);
		return false;
	}

	return true;
}

void FTerraDyneAsyncSaver::SaveBytesAtomically(
	FString DestinationPath,
	TArray<uint8> Bytes,
	FOnTerraDyneAsyncSaveComplete Completion)
{
	Async(EAsyncExecution::ThreadPool,
		[DestinationPath = MoveTemp(DestinationPath),
		 Bytes = MoveTemp(Bytes),
		 Completion = MoveTemp(Completion)]() mutable
		{
			FString Error;
			const bool bSuccess = SaveBytesAtomicallySync(DestinationPath, Bytes, Error);
			AsyncTask(ENamedThreads::GameThread,
				[bSuccess, Error = MoveTemp(Error), Completion = MoveTemp(Completion)]() mutable
				{
					Completion.ExecuteIfBound(bSuccess, Error);
				});
		});
}

void FTerraDyneAsyncSaver::SaveChunkStateAtomically(
	FString DestinationPath,
	FTerraDyneChunkData Data,
	int32 MaxUncompressedBytes,
	int32 MaxPacketBytes,
	FOnTerraDyneAsyncSaveComplete Completion)
{
	Async(EAsyncExecution::ThreadPool,
		[DestinationPath = MoveTemp(DestinationPath),
		 Data = MoveTemp(Data),
		 MaxUncompressedBytes,
		 MaxPacketBytes,
		 Completion = MoveTemp(Completion)]() mutable
		{
			TArray<uint8> Packet;
			FString Error;
			bool bSuccess = FTerraDyneStateCodec::EncodeChunk(
				Data, Packet, &Error, MaxUncompressedBytes);
			if (bSuccess && Packet.Num() > MaxPacketBytes)
			{
				Error = FString::Printf(TEXT("Encoded chunk packet size %d exceeds the %d-byte cache limit."),
					Packet.Num(), MaxPacketBytes);
				bSuccess = false;
			}
			if (bSuccess)
			{
				bSuccess = SaveBytesAtomicallySync(DestinationPath, Packet, Error);
			}
			AsyncTask(ENamedThreads::GameThread,
				[bSuccess, Error = MoveTemp(Error), Completion = MoveTemp(Completion)]() mutable
				{
					Completion.ExecuteIfBound(bSuccess, Error);
				});
		});
}
