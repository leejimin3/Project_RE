// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "REBulletPattern.h"
#include "REBossCharacter.generated.h"

class UREHealthBarComponent;

/**
 *  보스 폰. 탄막 패턴 발사 진입점을 가진다.
 *  ACharacter 직접 상속 — ARECharacterBase는 카메라 붐 달린 플레이어 폰이라 부적합.
 *  HP는 서버 권위(Replicated) — 데미지 적용은 TakeDamage HasAuthority 가드 경유 (RECharacterBase 동일 패턴).
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

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 현재 체력. 서버 권위, 클라 복제. 변경 시 OnRep_Health로 HP바 갱신. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 100.f;

	/** 머리 위 HP바 (#29). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	UREHealthBarComponent* HealthBar;

	/** Health 복제 도착(클라) / 서버 직접 호출 공용 — HP바 갱신. */
	UFUNCTION()
	void OnRep_Health();

private:
	/** Spiral 호출마다 누적되는 시작각. 연속 트리거 시 링이 회전한다. */
	float SpiralBaseAngleDeg = 0.f;
	/** Spiral 호출당 BaseAngle 증가량(deg). */
	static constexpr float SpiralRotationStepDeg = 15.f;

	/**
	 *  클로즈드루프 스폰율(발사당 탄 수). 적분 제어 — 라이브 카운트가 목표에 못 미치면 램프업.
	 *  Mass는 히트 프로세서가 원점 근처 탄을 소멸시켜 피드포워드로는 목표 미달(#51).
	 *  -1 = 미초기화(첫 발사에 피드포워드 값으로 시딩).
	 */
	float SpiralSpawnRate = -1.f;
	/** 소수부 누산 — 발사당 정수 탄 수로 내림하되 소수부를 이월해 소형 타깃 양자화 오버슛 방지. */
	float SpiralSpawnAccum = 0.f;
	/** 누적 발사 횟수 — 첫 1수명(≈수명/발사주기 발) 동안은 적분 정지(피드포워드로 채우기)해 와인드업 방지. */
	int32 SpiralShotCount = 0;

	/** 사망 여부. 서버 전용 — 클라 시각처리는 스코프 밖이라 비복제. */
	bool bIsDead = false;
};
