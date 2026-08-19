// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "REAttackComponent.generated.h"

class UAnimSequence;

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

	/**
	 *  발사 후 이동이 잠기는 시간(s). 사격 모션이 끝날 때까지다.
	 *  모션 애셋이 없는 환경(.gitignore 대상 경로)에서는 발사 간격으로 대체한다 —
	 *  0을 돌려주면 그 환경만 이동 규칙이 달라져 버린다.
	 */
	float GetFireLockSec() const;

private:
	/** 발사당 데미지. Settings(AttackDamage) 단일 출처 — 생성자에서 로드. */
	float Damage = 10.f;

	/** 발사 간격(s). Settings(AttackInterval) 단일 출처. 서버 rate limit + 클라 페이싱 공용. */
	float AttackInterval = 0.25f;

	/** 히트스캔 사거리(uu). Settings(AttackRange) 단일 출처. */
	float AttackRange = 2000.f;

	/**
	 *  발사 모션(AnimSequence — ABP DefaultSlot에 다이나믹 몽타주로 재생, MM_Dash와 같은 방식).
	 *  코스메틱 — 재생은 오너 캐릭터의 Multicast_PlayFire가 전 클라에 전달(M4 #74).
	 *
	 *  MM_Pistol_Fire_Montage를 쓰지 않는 이유는 #132 참조 — 그 몽타주가 든 애님이 애디티브라
	 *  애디티브 슬롯(Arms) 없이는 화면에 아무 변화가 없다. 비-애디티브인 DryFire로 갈아탔다.
	 */
	UPROPERTY()
	TObjectPtr<UAnimSequence> FireAnim;

	/** 서버 마지막 발사 시각(월드초). rate limit 기준. -1 = 미발사. */
	double LastFireTime = -1.0;
};
