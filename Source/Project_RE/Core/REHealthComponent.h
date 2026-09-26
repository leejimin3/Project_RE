// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "REHealthComponent.generated.h"

/**
 *  서버 권위 체력 (#29/#73/#85). 플레이어와 보스가 같이 쓴다.
 *
 *  전에는 ARECharacterBase 와 AREBossCharacter 가 체력·최대체력·사망 가드·OnRep·HP바 갱신을
 *  각자 들고 있었다(보스 주석: "RECharacterBase 동일 패턴"). 한쪽만 고치면 어긋나는 구조였고,
 *  실제로 사망 판정이 서버 전용 플래그에 묶여 클라에서 틀린 답을 냈다.
 *
 *  Health 만 복제한다. MaxHealth 는 서버·클라가 생성자에서 같은 ini 를 읽으므로 비복제다.
 *  HP바는 소유 액터의 UREHealthBarComponent 를 찾아 갱신한다 — 없으면(데디 등) 건너뛴다.
 */
UCLASS()
class UREHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UREHealthComponent();

	/** 소유 액터 생성자에서 1회. 체력을 가득 채운다. */
	void Init(float InMaxHealth);

	/**
	 *  서버 전용 — 체력을 깎고 HP바를 갱신한다. **이번 호출로 사망했으면 true** (정확히 1회).
	 *  사망 후에도 뜬 탄환이 계속 때리므로 두 번째 이후는 false 다.
	 */
	bool ApplyDamage(float Amount);

	/** 복제되는 Health 로 판정 — 서버·클라가 같은 답을 낸다. */
	bool IsAlive() const { return Health > 0.f; }
	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** 현재 체력. 서버 권위, 클라 복제. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. 비복제 (#73). */
	UPROPERTY(VisibleAnywhere, Category = "Stats")
	float MaxHealth = 100.f;

	/** 사망 처리 1회 가드. 서버 전용·비복제. */
	bool bDeathHandled = false;

	/** Health 복제 도착(클라) / 서버 직접 호출 공용 — HP바 갱신. */
	UFUNCTION()
	void OnRep_Health();
};
