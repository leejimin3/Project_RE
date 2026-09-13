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
	UInstancedStaticMeshComponent* GetArcISM() const { return ArcISM; }
	UInstancedStaticMeshComponent* GetMarkerISM() const { return MarkerISM; }
	UInstancedStaticMeshComponent* GetExplosionCoreISM() const { return ExplosionCoreISM; }
	UInstancedStaticMeshComponent* GetExplosionRingISM() const { return ExplosionRingISM; }
	UInstancedStaticMeshComponent* GetExplosionSmokeISM() const { return ExplosionSmokeISM; }

private:
	UPROPERTY()
	TObjectPtr<AActor> Holder = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ISM = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ArcISM = nullptr;     // 곡사탄(주황 구체, Z 궤적)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MarkerISM = nullptr;  // 착지 예고(빨강 평면 원)

	// 폭발 (#149, #151) — 폭발 1개 = 아래 세 통에 인스턴스 1개씩. 컴포넌트를 만들지
	// 않으므로 드로우콜이 동시 폭발 개수와 무관하다(층 수가 상한).
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionCoreISM = nullptr;   // 불덩이(구체)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionRingISM = nullptr;   // 수평 충격파(평면 원환)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionSmokeISM = nullptr;  // 연기 대역(큰 어두운 구체)
};
