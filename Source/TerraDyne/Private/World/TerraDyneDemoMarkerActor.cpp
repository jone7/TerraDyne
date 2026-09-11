// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#include "World/TerraDyneDemoMarkerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/Material.h"

ATerraDyneDemoMarkerActor::ATerraDyneDemoMarkerActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	MarkerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MarkerMesh"));
	SetRootComponent(MarkerMesh);

	MarkerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerMesh->SetMobility(EComponentMobility::Movable);
	MarkerMesh->SetCanEverAffectNavigation(false);
	MarkerMesh->SetCastShadow(true);
	MarkerMesh->SetRelativeScale3D(FVector(0.18f, 0.18f, 2.2f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		MarkerMesh->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterial> BaseMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (BaseMaterial.Succeeded())
	{
		MarkerMesh->SetMaterial(0, BaseMaterial.Object);
	}
}
