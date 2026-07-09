// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "REBulletRenderSubsystem.generated.h"

class AActor;
class UInstancedStaticMeshComponent;

/**
 *  탄막 ISM 소유자. Processor는 UMassProcessor(액터 아님)라 ISM을 직접 못 가진다.
 *  월드 BeginPlay 시 홀더 액터를 스폰해 ISM(빨강 엔진 구체)을 부착하고 GetISM()로 노출.
 *  데디서버(NM_DedicatedServer)는 렌더 불요 → 홀더/ISM 생성 skip(GetISM()이 null).
 */
UCLASS()
class UREBulletRenderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UInstancedStaticMeshComponent* GetISM() const { return ISM; }

private:
	UPROPERTY()
	TObjectPtr<AActor> Holder = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ISM = nullptr;
};
