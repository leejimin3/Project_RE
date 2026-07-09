// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UREBulletRenderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 데디서버는 렌더 안 함. 게임 월드(PIE/스탠드얼론)만 ISM 생성.
	if (InWorld.GetNetMode() == NM_DedicatedServer || !InWorld.IsGameWorld())
	{
		return;
	}

	Holder = InWorld.SpawnActor<AActor>();
	if (!Holder)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] RenderSubsystem: Holder spawn failed"));
		return;
	}

	ISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	Holder->SetRootComponent(ISM);
	ISM->RegisterComponent();

	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ISM->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dyn = ISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);  // 탄환 빨강
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] RenderSubsystem: ISM ready (mesh=%d)"), ISM->GetStaticMesh() != nullptr);
}
