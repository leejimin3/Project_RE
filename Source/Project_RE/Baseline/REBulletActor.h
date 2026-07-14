// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "REBulletActor.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

/**
 *  액터 기반 탄환 1발 — Mass 경로(#15 SimProcessor + #17 RenderProcessor)의 비교군.
 *  일부러 최적화하지 않는다: 풀링 없음, 개별 Tick, 액터마다 메시 컴포넌트 1개.
 *  "액터로 짜면 보통 이렇게 짠다"의 정직한 버전이어야 Mass와의 비교가 공정하다 (#45).
 *  측정 전용 — 게임 코드가 이 클래스에 의존하면 안 된다.
 */
UCLASS()
class AREBulletActor : public AActor
{
	GENERATED_BODY()

public:
	AREBulletActor();

	/** 스폰 직후 스포너가 호출. 머티리얼은 스포너가 만든 공유 MID (액터마다 MID 만들면 불공정한 추가 비용). */
	void Init(const FVector& InVelocity, float InLifetime, UMaterialInterface* InMaterial);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	/** Mass의 FBulletSimFragment::Velocity 대응 (uu/s). */
	FVector Velocity = FVector::ZeroVector;

	/** Mass의 FBulletSimFragment::Lifetime 대응 (s). 0 이하면 자멸. */
	float Lifetime = 0.f;
};
