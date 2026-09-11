// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "Physics/TerraDyneCollision.h"

// Engine Includes
#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshSpatialFunctions.h"
#include "UDynamicMesh.h"

// Geometry Core (Low-level mesh manipulation)
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"

void UTerraDyneCollisionLib::ApplyHeightDataToMesh(
	UDynamicMeshComponent* TargetMeshComp, 
	const TArray<float>& HeightData, 
	int32 Resolution, 
	float ChunkSize,
	float ZScale)
{
	if (!TargetMeshComp || !TargetMeshComp->GetDynamicMesh())
	{
		return;
	}

	if (HeightData.Num() != (Resolution * Resolution))
	{
		// Data mismatch safety check
		return;
	}

	// EditMesh is the safe way to modify the FDynamicMesh3 geometry.
	// It handles the change tracking and internal locking.
	TargetMeshComp->GetDynamicMesh()->EditMesh([&](UE::Geometry::FDynamicMesh3& Mesh)
	{
		// Pre-calculate mapping constants to avoid divisions in loop
		const float HalfSize = ChunkSize * 0.5f;
		const float InvChunkSize = 1.0f / ChunkSize;
		const int32 MaxIndex = Resolution - 1;

		// Iterate over all vertices in the mesh
		for (int32 VertID : Mesh.VertexIndicesItr())
		{
			FVector3d Pos = Mesh.GetVertex(VertID);

			// 1. Convert Local Mesh Position (Centered) to 0..1 UV mappings
			// X range: [-HalfSize, +HalfSize] -> [0, 1]
			float U = (float(Pos.X) + HalfSize) * InvChunkSize;
			float V = (float(Pos.Y) + HalfSize) * InvChunkSize;

			// 2. Map UV to Grid Coordinates (0..MaxIndex)
			// We clamp to ensure we don't read out of bounds due to floating point precision at edges
			int32 GridX = FMath::Clamp(FMath::RoundToInt(U * MaxIndex), 0, MaxIndex);
			int32 GridY = FMath::Clamp(FMath::RoundToInt(V * MaxIndex), 0, MaxIndex);

			// 3. Sample Height
			// Note: If Mesh resolution != Grid resolution, you might want Bilinear Interpolation here.
			// For collision meshes, Nearest Neighbor (RoundToInt) is usually sufficient and faster.
			int32 ArrayIdx = (GridY * Resolution) + GridX;
			
			if (HeightData.IsValidIndex(ArrayIdx))
			{
				double NewZ = (double)(HeightData[ArrayIdx] * ZScale);
				
				// 4. Update Vertex
				Mesh.SetVertex(VertID, FVector3d(Pos.X, Pos.Y, NewZ));
			}
		}

		// Optional: Recompute Normals if needed for physics queries (slope determination)
		// Usually collision uses face normals which are auto-updated, but vertex normals might strictly require this.
		// UE::Geometry::FMeshNormals::QuickComputeVertexNormals(Mesh);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::VertexPositions);
}

void UTerraDyneCollisionLib::ConfigureForTerrainPhysics(UDynamicMeshComponent* TargetComponent)
{
	if (!TargetComponent) return;

	// 1. Collision Mode
	// "ComplexAsSimple" uses the actual triangle mesh for collision queries.
	// This is standard for Landscapes (StaticMesh/DynamicMesh).
	TargetComponent->CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;

	// 2. Optimization: Disable Rendering Features
	// The physics mesh is invisible; the VHFM handles the visuals.
	// Turning these off saves draw calls and shadow map overhead.
	TargetComponent->SetCastShadow(false);
	TargetComponent->bCastDynamicShadow = false;
	TargetComponent->bCastStaticShadow = false;
	TargetComponent->SetVisibility(false); // Often easier to just hide it, but ensure bHiddenInGame=true
	TargetComponent->SetHiddenInGame(true);

	// 3. Navigation
	// Ensure the NavMesh can walk on this
	TargetComponent->SetCanEverAffectNavigation(true);

	// 4. Physics Profile
	// Ensure it blocks standard traces
	TargetComponent->SetCollisionProfileName(TEXT("BlockAll"));
}

bool UTerraDyneCollisionLib::IsLocationTraceable(UDynamicMeshComponent* MeshComp, FVector LocalLocation)
{
	if (!MeshComp || !MeshComp->GetDynamicMesh()) return false;

	bool bIsTraceable = true;

	MeshComp->GetDynamicMesh()->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
	{
		if (Mesh.TriangleCount() == 0) return;

		// Determine grid parameters from mesh bounds
		UE::Geometry::FAxisAlignedBox3d Bounds = Mesh.GetBounds();
		double WorldSizeX = Bounds.Max.X - Bounds.Min.X;
		double WorldSizeY = Bounds.Max.Y - Bounds.Min.Y;
		if (WorldSizeX <= 0.0 || WorldSizeY <= 0.0) return;

		// Top surface vertex count = total / 2 (mesh has top + bottom surfaces)
		int32 TopVerts = Mesh.VertexCount() / 2;
		int32 Resolution = FMath::RoundToInt(FMath::Sqrt((double)TopVerts));
		if (Resolution < 2) return;

		// Convert local position to normalized [0,1] coordinates
		double NormX = (LocalLocation.X - Bounds.Min.X) / WorldSizeX;
		double NormY = (LocalLocation.Y - Bounds.Min.Y) / WorldSizeY;

		// Out of bounds
		if (NormX < 0.0 || NormX > 1.0 || NormY < 0.0 || NormY > 1.0) return;

		// Map to grid cell
		int32 CellX = FMath::Clamp(FMath::FloorToInt(NormX * (Resolution - 1)), 0, Resolution - 2);
		int32 CellY = FMath::Clamp(FMath::FloorToInt(NormY * (Resolution - 1)), 0, Resolution - 2);

		// Top surface triangles are appended first in RebuildMesh():
		// 2 triangles per cell, sequential order: row-major (Y outer, X inner)
		// Assumes: fresh mesh from RebuildMesh() with sequential triangle IDs.
		// Total expected top-surface triangles = 2 * (Resolution-1)^2
		int32 ExpectedTopTris = 2 * (Resolution - 1) * (Resolution - 1);
		if (Mesh.TriangleCount() < ExpectedTopTris) return; // Mesh layout mismatch — assume traceable

		// WARNING: This assumes AppendRectangleXY produces exactly 2 triangles per cell
		// in strict row-major order. If the mesh generation algorithm changes, this mapping
		// will silently produce incorrect results. Consider using TMeshAABBTree3 queries.
		int32 TriBase = (CellY * (Resolution - 1) + CellX) * 2;
		if (!ensure(TriBase >= 0 && TriBase + 1 < Mesh.TriangleCount()))
		{
			return; // Safety: triangle index out of range
		}

		// Check MaterialID on both triangles in this cell (hole = non-zero MaterialID)
		const UE::Geometry::FDynamicMeshMaterialAttribute* MatAttrib =
			Mesh.HasAttributes() ? Mesh.Attributes()->GetMaterialID() : nullptr;

		if (MatAttrib)
		{
			for (int32 Offset = 0; Offset < 2; Offset++)
			{
				int32 TriID = TriBase + Offset;
				if (Mesh.IsTriangle(TriID))
				{
					int32 MatID = 0;
					MatAttrib->GetValue(TriID, &MatID);
					if (MatID != 0)
					{
						bIsTraceable = false;
						return;
					}
				}
			}
		}
		// No material attribute layer → no holes possible → traceable
	});

	return bIsTraceable;
}
