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

	/**
	 *  ISM 인스턴스를 트랜스폼 배열에 맞춘다 — 렌더 프로세서 셋(탄/곡사/폭발) 공용.
	 *  꼬리에서 add/remove 하므로 다른 인덱스가 안 밀린다(swap 없음). 트랜스폼은 배열째
	 *  한 번에 넘긴다 — 인스턴스당 개별 호출은 개수에 비례해 게임 스레드를 먹었다 (#95).
	 *  커스텀데이터는 이 호출 **뒤에** 써야 인덱스 범위가 유효하다.
	 */
	static void SyncInstances(UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xf);

	/**
	 *  스폰 팝 지속시간(s). 태어난 직후만 밝기가 솟았다가 정상으로 붙는다 — 직선탄·곡사탄 공용.
	 *  수명 페이드는 넣지 않는다: 죽기 직전 탄이 흐려지면 여전히 치명적인데 사라지는 중으로
	 *  오독되고, 탄막에서 히트박스 가독성은 공정성 문제다 (#97).
	 */
	static constexpr float SpawnPopSec = 0.1f;

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
