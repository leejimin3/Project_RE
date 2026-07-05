// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "REBulletPattern.h"
#include "REBossCharacter.generated.h"

/**
 *  보스 폰. 탄막 패턴 발사 진입점을 가진다.
 *  ACharacter 직접 상속 — ARECharacterBase는 카메라 붐 달린 플레이어 폰이라 부적합.
 *  HP/TakeDamage 서버 권위 로직은 M2 범위.
 */
UCLASS()
class AREBossCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AREBossCharacter();

	/**
	 *  탄막 패턴 발사. M0 싱글: MassEntitySubsystem에 placeholder 엔티티 N개 직접 스폰.
	 *  Seed/StartTime은 M5 데디에서 서버→클라 동일 시드 시뮬용 — M0에서는 저장/미사용.
	 */
	void TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime);
};
