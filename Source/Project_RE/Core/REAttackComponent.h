// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "REAttackComponent.generated.h"

class UAnimMontage;

/**
 *  플레이어 수동공격 컴포넌트 (M3.5 ①, 구 REAutoFireComponent #26).
 *  서버에서 FireInDirection(Dir) 호출 → Dir 방향 히트스캔 1발 → 보스 히트 시 TakeDamage.
 *  조준은 클라(커서 방향), 판정·데미지는 서버 — 방향만 RPC로 받는다(REPlayerController).
 *  rate limit(AttackInterval)은 서버가 재검증 — 클라 스팸/치팅 방어.
 */
UCLASS()
class UREAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UREAttackComponent();

	/**
	 *  서버: Dir(수평 단위벡터) 방향 히트스캔 1발.
	 *  rate limit 통과 시 발사하고 true, 간격 미달이면 발사 없이 false.
	 */
	bool FireInDirection(const FVector& Dir);

	/** 발사 간격 조회 — 컨트롤러가 클라 로컬 페이싱에 사용. */
	float GetAttackInterval() const { return AttackInterval; }

private:
	/** 발사당 데미지. Settings(AttackDamage) 단일 출처 — 생성자에서 로드. */
	float Damage = 10.f;

	/** 발사 간격(s). Settings(AttackInterval) 단일 출처. 서버 rate limit + 클라 페이싱 공용. */
	float AttackInterval = 0.25f;

	/** 히트스캔 사거리(uu). Settings(AttackRange) 단일 출처. */
	float AttackRange = 2000.f;

	/** 발사 모션 몽타주. 코스메틱 — 싱글/리슨은 서버 재생 = 화면 표시. */
	UPROPERTY()
	TObjectPtr<UAnimMontage> FireMontage;

	/** 서버 마지막 발사 시각(월드초). rate limit 기준. -1 = 미발사. */
	double LastFireTime = -1.0;
};
