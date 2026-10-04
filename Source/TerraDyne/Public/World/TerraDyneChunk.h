// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/TextureRenderTarget2D.h"
#include "World/TerraDyneTileData.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Grass/TerraDyneGrassTypes.h"
#include "TerraDyneChunk.generated.h"

UCLASS()
class TERRADYNE_API ATerraDyneChunk : public AActor
{
	GENERATED_BODY()
	friend class FTerraDyneChunkMeshBuildTask;

public:
	/** 最近请求的高度网格已安装后，调用方才可交接源地形碰撞。 */
	bool IsSurfaceReadyForHandoff() const { return !bMeshDirty && !bMeshBuildInFlight && PendingMeshBuildSerial == INDEX_NONE; }
	// Cooker 可注入异步碰撞交接组件，普通 TerraDyne 仍使用默认组件。
	ATerraDyneChunk(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, Category = "TerraDyne")
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComp;

	UPROPERTY(EditAnywhere, Replicated, Category = "TerraDyne")
	float ZScale;

	UPROPERTY(EditAnywhere, Replicated, Category = "TerraDyne")
	float WorldSize;

	UPROPERTY(EditAnywhere, Replicated, Category = "TerraDyne")
	int32 Resolution;

	/** 地表 UV0 的缩放；Cooker 用源 Landscape 网格坐标保持纹理对齐，不复制到网络。 */
	FVector2D SurfaceUVScale = FVector2D(1, 1);
	/** 地表 UV0 的源块偏移；邻块使用同一 Landscape 原点。 */
	FVector2D SurfaceUVOffset = FVector2D::ZeroVector;
	/** 显示权重的原始分辨率；零表示使用普通 TerraDyne 的当前分辨率。 */
	int32 SurfaceWeightResolution = 0;
    /** Cooker 可指定原分辨率权重贴图；未指定的插件地形使用自身可编辑权重。 */
    UPROPERTY(Transient) TObjectPtr<UTexture2D> SurfaceWeightTexture;
	/** Cooker 从原地形及跨块坡度派生的法线快照，不参与插件网络或保存。 */
	TArray<FVector3f> SurfaceNormals;

	UPROPERTY(VisibleAnywhere, Replicated, Category = "TerraDyne")
	FIntPoint GridCoordinate;

	UPROPERTY(VisibleAnywhere, Replicated, Category = "TerraDyne")
	float ChunkSizeWorldUnits;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "TerraDyne|Procedural")
	int32 ProceduralSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "TerraDyne|Procedural")
	bool bIsAuthoredChunk = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "TerraDyne|Procedural")
	FName PrimaryBiomeTag = NAME_None;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BrushMaterialBase;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> BaseLayerRT;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> SculptLayerRT;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> DetailLayerRT;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> HeightRT; // Current height RT (read target for display/collision)

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> HeightUploadTexture;

	// --- Height Buffers ---
	TArray<float> BaseBuffer;
	TArray<float> SculptBuffer;
	TArray<float> DetailBuffer;
	TArray<float> HeightBuffer;
	TArray<float> SmoothScratchBuffer;

	// --- Weight Buffers (paint layers 0-3) ---
	static constexpr int32 NumWeightLayers = 4;
	TArray<float> WeightBuffers[NumWeightLayers];

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> WeightTexture;

	// --- Grass ---
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> GrassISMs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TransferredFoliageISMs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> TransferredActorFoliageActors;

	UPROPERTY(EditAnywhere, Category = "TerraDyne|Grass")
	TObjectPtr<UTerraDyneGrassProfile> GrassProfile;

public:
	void Initialize(int32 InResolution, float InSize);
	void InitializeChunk(FIntPoint Coord, float Size, int32 InRes, UTexture2D* SourceHeight, UTexture2D* SourceWeight = nullptr);
	void SetMaterial(UMaterialInterface* InMaterial);

	// BrushMode replaces bIsHole; WeightLayerIndex and FlattenHeight are mode-specific
	void ApplyLocalIdempotentEdit(
		FVector RelativePos,
		float Radius,
		float Strength,
		ETerraDyneBrushMode BrushMode,
		ETerraDyneLayer TargetLayer = ETerraDyneLayer::Sculpt,
		int32 WeightLayerIndex = 0,
		float FlattenHeight = 0.f);

	void RebuildPhysicsMesh();
	float GetHeightAtLocation(FVector LocalPos) const;
	float GetWeightAtLocation(FVector LocalPos, int32 LayerIndex) const;
	bool IsUsingGPU() const { return bUseGPU; }
	FBox GetWorldBounds() const;

	// Weight texture upload
	void UploadWeightTexture();

	// Grass
	void SetGrassProfile(UTerraDyneGrassProfile* Profile);
	void RequestGrassRegen();
	void ApplyGrassResult(int32 VarietyIndex, TArray<FTransform>&& Transforms);

	// Imported placed foliage
	void SetTransferredFoliageData(
		const TArray<FString>& InStaticMeshPaths,
		const TArray<int32>& InMaterialCounts,
		const TArray<FString>& InOverrideMaterialPaths,
		const TArray<int32>& InDefinitionIndices,
		const TArray<FTransform>& InLocalTransforms,
		const TArray<float>& InTerrainOffsets,
		bool bInFollowsTerrain);
	void RefreshTransferredFoliagePlacement(bool bSnapToTerrain);
	void RequestTransferredFoliageRefresh();
	void SetTransferredFoliageFollowsTerrain(bool bInFollowsTerrain);
	int32 GetTransferredFoliageInstanceCount() const { return TransferredFoliageInstanceLocalTransforms.Num(); }
	int32 GetTransferredFoliageDefinitionCount() const { return TransferredFoliageStaticMeshPaths.Num(); }
	void SetTransferredActorFoliageData(
		const TArray<FString>& InActorClassPaths,
		const TArray<uint8>& InAttachFlags,
		const TArray<int32>& InDefinitionIndices,
		const TArray<FTransform>& InLocalTransforms,
		const TArray<float>& InTerrainOffsets);
	void RefreshTransferredActorFoliagePlacement(bool bSnapToTerrain);
	int32 GetTransferredActorFoliageInstanceCount() const { return TransferredActorFoliageInstanceLocalTransforms.Num(); }
	int32 GetTransferredActorFoliageDefinitionCount() const { return TransferredActorFoliageClassPaths.Num(); }
	bool GetTransferredActorFoliageInstanceTransform(int32 InstanceIndex, FTransform& OutTransform) const;

	// Persistence
	struct FTerraDyneChunkData GetSerializedData() const;
	void LoadFromData(const struct FTerraDyneChunkData& Data);

	// LOD & Optimization
	void UpdateLOD(const FVector& ViewerPos);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	bool bInitialized;
	// 当前开源适配只支持 CPU 笔刷；供原有能力查询返回准确状态。
	const bool bUseGPU = false;
	void UpdateDisplayMaterialParameters();
	
	// Collision Throttling
	float CollisionDebounceTimer;
	bool bCollisionDirty;

	// Mesh Throttling
	bool bMeshDirty = false;

	// Grass Debounce
	float GrassDebounceTimer;
	bool bGrassDirty;

	// Imported foliage refresh debounce
	float TransferredFoliageDebounceTimer;
	bool bTransferredFoliageDirty;
	bool bTransferredFoliageFollowsTerrain;
	TArray<FString> TransferredFoliageStaticMeshPaths;
	TArray<int32> TransferredFoliageMaterialCounts;
	TArray<FString> TransferredFoliageOverrideMaterialPaths;
	TArray<int32> TransferredFoliageDefinitionIndices;
	TArray<FTransform> TransferredFoliageInstanceLocalTransforms;
	TArray<float> TransferredFoliageInstanceTerrainOffsets;
	TArray<FString> TransferredActorFoliageClassPaths;
	TArray<uint8> TransferredActorFoliageAttachFlags;
	TArray<int32> TransferredActorFoliageDefinitionIndices;
	TArray<FTransform> TransferredActorFoliageInstanceLocalTransforms;
	TArray<float> TransferredActorFoliageInstanceTerrainOffsets;

	// 为 CPU 高度上传建立显示资源，禁止 GPU 计算和回读。
	void SetupHeightDisplayResources();
	/** 为当前分辨率建立线性权重纹理；加载烘焙块与新建块使用同一入口。 */
	void SetupWeightDisplayResources();
	void UpdateRenderTargetFromHeightmap();
	bool HasTerrainMeshTopology() const;
	void BuildTerrainSurfaceSamples(TArray<FVector3d>& OutVertexPositions, TArray<FVector3f>& OutVertexNormals) const;
	void InitializeTerrainMeshTopology(const TArray<FVector3d>& VertexPositions, const TArray<FVector3f>& VertexNormals);
	void UpdateTerrainMeshSurface(const TArray<FVector3d>& VertexPositions, const TArray<FVector3f>& VertexNormals);
	void StartAsyncMeshBuild();
	void ApplyPendingMeshBuild();
	void ReceiveAsyncMeshBuildResult(
		int32 BuildSerial,
		bool bCanReuseTopology,
		TArray<FVector3d>&& VertexPositions,
		TArray<FVector3f>&& VertexNormals);
	void RebuildMesh();
	void UpdateCollision();
	void ClearTransferredFoliageComponents();
	void ClearTransferredActorFoliageActors();

	TArray<FVector3d> PendingMeshVertexPositions;
	TArray<FVector3f> PendingMeshVertexNormals;
	int32 PendingMeshBuildSerial = INDEX_NONE;
	int32 RequestedMeshBuildSerial = 0;
	int32 InFlightMeshBuildSerial = INDEX_NONE;
	bool bPendingMeshCanReuseTopology = false;
	bool bMeshBuildInFlight = false;
};
