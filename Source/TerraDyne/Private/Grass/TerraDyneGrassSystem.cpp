// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Grass/TerraDyneGrassSystem.h"
#include "Async/Async.h"
#include "World/TerraDyneChunk.h"
#include "Async/AsyncWork.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"

// ---------------------------------------------------------------------------
// FTerraDyneGrassGenTask
// ---------------------------------------------------------------------------

class FTerraDyneGrassGenTask : public FNonAbandonableTask
{
	friend class FAutoDeleteAsyncTask<FTerraDyneGrassGenTask>;

	TWeakObjectPtr<ATerraDyneChunk> ChunkPtr;

	// Terrain data snapshots (plain data, no UObject pointers)
	TArray<float> HeightBuffer;
	TArray<TArray<float>> WeightBuffers; // indexed [Layer][Pixel]

	TArray<FTerraDyneGrassVarietyData> Varieties;

	int32 Resolution;
	float WorldSize;
	float ZScale;
	FVector ChunkOrigin; // World-space position of the chunk actor

	// Shared cancel token — set to true by FTerraDyneGrassSystem::CancelAllTasks()
	TSharedPtr<std::atomic<bool>> CancelToken;

public:
	FTerraDyneGrassGenTask(
		TWeakObjectPtr<ATerraDyneChunk> InChunk,
		TArray<float>&& InHeightBuffer,
		TArray<TArray<float>>&& InWeightBuffers,
		TArray<FTerraDyneGrassVarietyData>&& InVarieties,
		int32 InResolution,
		float InWorldSize,
		float InZScale,
		FVector InOrigin,
		TSharedPtr<std::atomic<bool>> InCancelToken)
		: ChunkPtr(InChunk)
		, HeightBuffer(MoveTemp(InHeightBuffer))
		, WeightBuffers(MoveTemp(InWeightBuffers))
		, Varieties(MoveTemp(InVarieties))
		, Resolution(InResolution)
		, WorldSize(InWorldSize)
		, ZScale(InZScale)
		, ChunkOrigin(InOrigin)
		, CancelToken(MoveTemp(InCancelToken))
	{
	}

	void DoWork()
	{
		// All terrain data was captured on the game thread; no UObject access needed here.

		const float HalfSize = WorldSize * 0.5f;
		const float Step = (Resolution > 1) ? WorldSize / (Resolution - 1) : WorldSize;

		// Per-variety, build a list of transforms, then dispatch to GT
		for (const FTerraDyneGrassVarietyData& Variety : Varieties)
		{
			TArray<FTransform> Instances;

			// Density is in instances per m² (10000 cm²). WorldSize is in cm.
			const float ChunkAreaM2 = (WorldSize / 100.f) * (WorldSize / 100.f);
			const int32 NumInstances = FMath::Max(1, FMath::RoundToInt(Variety.Density * ChunkAreaM2));

			// Deterministic per-chunk seed so re-gen gives the same result
			FRandomStream RNG(GetTypeHash(ChunkOrigin));

			const int32 LayerIdx = FMath::Clamp(Variety.WeightLayerIdx, 0, WeightBuffers.Num() - 1);
			const TArray<float>& WeightLayer = WeightBuffers[LayerIdx];

			for (int32 i = 0; i < NumInstances; i++)
			{
				const float LocalX = RNG.FRandRange(-HalfSize, HalfSize);
				const float LocalY = RNG.FRandRange(-HalfSize, HalfSize);

				// --- Sample weight (bilinear) ---
				float NX = (LocalX + HalfSize) / WorldSize;
				float NY = (LocalY + HalfSize) / WorldSize;
				int32 PX = FMath::Clamp(FMath::FloorToInt(NX * (Resolution - 1)), 0, Resolution - 2);
				int32 PY = FMath::Clamp(FMath::FloorToInt(NY * (Resolution - 1)), 0, Resolution - 2);
				float FX = NX * (Resolution - 1) - PX;
				float FY = NY * (Resolution - 1) - PY;

				auto SampleBuffer = [&](const TArray<float>& Buf, int32 X, int32 Y) -> float
				{
					int32 Idx = FMath::Clamp(Y * Resolution + X, 0, Buf.Num() - 1);
					return Buf.Num() > 0 ? Buf[Idx] : 0.f;
				};

				float W00 = SampleBuffer(WeightLayer, PX,   PY);
				float W10 = SampleBuffer(WeightLayer, PX+1, PY);
				float W01 = SampleBuffer(WeightLayer, PX,   PY+1);
				float W11 = SampleBuffer(WeightLayer, PX+1, PY+1);
				float Weight = FMath::BiLerp(W00, W10, W01, W11, FX, FY);

				if (Weight < Variety.MinWeight) continue;

				// --- Sample height (triangle-aware, matches mesh winding BL→BR→TR / BL→TR→TL) ---
				float H00 = SampleBuffer(HeightBuffer, PX,   PY);
				float H10 = SampleBuffer(HeightBuffer, PX+1, PY);
				float H01 = SampleBuffer(HeightBuffer, PX,   PY+1);
				float H11 = SampleBuffer(HeightBuffer, PX+1, PY+1);
				float Height;
				if (FY <= FX) // lower-right triangle: BL, BR, TR
					Height = H00 + FX * (H10 - H00) + FY * (H11 - H10);
				else          // upper-left triangle: BL, TR, TL
					Height = H00 + FX * (H11 - H01) + FY * (H01 - H00);

				// --- Slope check ---
				if (Variety.MaxSlopeAngle < 89.f)
				{
					float DX = (SampleBuffer(HeightBuffer, FMath::Min(PX+1, Resolution-1), PY) -
					            SampleBuffer(HeightBuffer, FMath::Max(PX-1, 0),             PY)) * ZScale;
					float DY = (SampleBuffer(HeightBuffer, PX, FMath::Min(PY+1, Resolution-1)) -
					            SampleBuffer(HeightBuffer, PX, FMath::Max(PY-1, 0)))             * ZScale;
					float StepWorld = Step * 2.f; // two-pixel span
					float SlopeDeg = FMath::RadiansToDegrees(
						FMath::Atan2(FMath::Sqrt(DX*DX + DY*DY), StepWorld));
					if (SlopeDeg > Variety.MaxSlopeAngle) continue;
				}

				// --- Build transform ---
				FVector WorldPos = ChunkOrigin + FVector(LocalX, LocalY, Height * ZScale);
				float Scale = RNG.FRandRange(Variety.ScaleMin, Variety.ScaleMax);
				FRotator Rot(0.f, RNG.FRandRange(0.f, 360.f), 0.f);

				Instances.Add(FTransform(Rot, WorldPos, FVector(Scale)));
			}

			if (Instances.Num() == 0) continue;

			// Dispatch results back to the game thread (fire-and-forget)
			int32 CapturedVIdx = Variety.VarietyIndex;
			TWeakObjectPtr<ATerraDyneChunk> CapturedChunk = ChunkPtr;
			TSharedPtr<std::atomic<bool>> CapturedToken = CancelToken;
			AsyncTask(ENamedThreads::GameThread,
				[CapturedChunk, CapturedVIdx, CapturedToken, CapturedInstances = MoveTemp(Instances)]() mutable
				{
					if (CapturedToken && CapturedToken->load(std::memory_order_relaxed)) return;
					if (CapturedChunk.IsValid())
					{
						CapturedChunk->ApplyGrassResult(CapturedVIdx, MoveTemp(CapturedInstances));
					}
				});
		}
	}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FTerraDyneGrassGenTask, STATGROUP_ThreadPoolAsyncTasks);
	}
};

// ---------------------------------------------------------------------------
// FTerraDyneGrassSystem
// ---------------------------------------------------------------------------

void FTerraDyneGrassSystem::Initialize(UWorld* InWorld)
{
	WorldRef = InWorld;
	CancelToken = MakeShared<std::atomic<bool>>(false);
}

void FTerraDyneGrassSystem::Shutdown()
{
	CancelAllTasks();
	WorldRef.Reset();
}

void FTerraDyneGrassSystem::RequestRegenForChunk(
	TWeakObjectPtr<ATerraDyneChunk> ChunkPtr,
	const TArray<float>& HeightBuffer,
	const TArray<float> WeightBuffers[],
	int32 NumWeightLayers,
	UTerraDyneGrassProfile* Profile,
	int32 ChunkResolution,
	float ChunkWorldSize,
	float ChunkZScale,
	FVector ChunkOrigin)
{
	if (!ChunkPtr.IsValid() || !Profile || Profile->Varieties.Num() == 0) return;

	// --- Snapshot data on Game Thread before launching background task ---

	// Copy height buffer
	TArray<float> HeightCopy = HeightBuffer;

	// Copy weight buffers
	TArray<TArray<float>> WeightCopies;
	WeightCopies.SetNum(NumWeightLayers);
	for (int32 L = 0; L < NumWeightLayers; L++)
	{
		WeightCopies[L] = WeightBuffers[L];
	}

	// Extract plain variety data (no UObject pointers)
	TArray<FTerraDyneGrassVarietyData> VarietyData;
	VarietyData.Reserve(Profile->Varieties.Num());
	for (int32 i = 0; i < Profile->Varieties.Num(); i++)
	{
		const FTerraDyneGrassVariety& V = Profile->Varieties[i];
		FTerraDyneGrassVarietyData D;
		D.VarietyIndex    = i;
		D.Density         = V.Density;
		D.WeightLayerIdx  = FMath::Clamp(V.WeightLayerIndex, 0, NumWeightLayers - 1);
		D.MinWeight       = V.MinWeight;
		D.ScaleMin        = V.ScaleRange.X;
		D.ScaleMax        = V.ScaleRange.Y;
		D.bAlignToSurface = V.bAlignToSurface;
		D.MaxSlopeAngle   = V.MaxSlopeAngle;
		VarietyData.Add(D);
	}

	(new FAutoDeleteAsyncTask<FTerraDyneGrassGenTask>(
		ChunkPtr,
		MoveTemp(HeightCopy),
		MoveTemp(WeightCopies),
		MoveTemp(VarietyData),
		ChunkResolution,
		ChunkWorldSize,
		ChunkZScale,
		ChunkOrigin,
		CancelToken))->StartBackgroundTask();
}

void FTerraDyneGrassSystem::RequestRegen(const FBox& /*WorldBounds*/)
{
	// Legacy stub — use RequestRegenForChunk instead.
}

void FTerraDyneGrassSystem::CancelAllTasks()
{
	// Signal all in-flight tasks to discard their results.
	// FAutoDeleteAsyncTask tasks are self-owned and finish on their own, but their
	// game-thread callbacks check the cancel token before applying results.
	if (CancelToken)
	{
		CancelToken->store(true, std::memory_order_relaxed);
	}
	// Mint a fresh token for any subsequent RequestRegenForChunk calls after restart.
	CancelToken = MakeShared<std::atomic<bool>>(false);
}
