// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayAbilitySpecHandle.h"
#include "RECharacterBase.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;
class UREAutoFireComponent;
class UREGA_Dash;
class UREHealthBarComponent;

/**
 *  탑뷰 쿼터뷰 플레이어 폰 베이스.
 *  캐릭터 부착 SpringArm + Camera를 절대회전으로 고정한다.
 *  HP는 서버 권위(Replicated) — 데미지 적용은 TakeDamage HasAuthority 가드 경유.
 */
UCLASS()
class ARECharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ARECharacterBase();

	//~ IAbilitySystemInterface — GAS가 ASC를 찾는 진입점.
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	//~ 서버: Possess 시점 ASC ActorInfo 세팅.
	virtual void PossessedBy(AController* NewController) override;

	//~ 클라: PlayerState 복제 도착 시 ASC ActorInfo 세팅.
	virtual void OnRep_PlayerState() override;

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 서버: 대쉬 시도. Dir 저장 후 대쉬 어빌리티 활성. 활성 성공 시 true. */
	bool TryDash(FVector Dir);

	/** 대쉬 어빌리티가 읽을 목표 방향(로컬이 계산해 서버로 전달한 값). */
	FVector GetPendingDashDir() const { return PendingDashDir; }

protected:
	/** 게임플레이 어빌리티 시스템 컴포넌트. Pawn 소유, Mixed 복제. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UAbilitySystemComponent* AbilitySystemComponent;

	/** 자동사격 컴포넌트 (#26). 서버에서만 구동. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoFire", meta = (AllowPrivateAccess = "true"))
	UREAutoFireComponent* AutoFireComponent;

	/** 머리 위 HP바 (#29). 셋업은 컴포넌트가 자체 처리. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	UREHealthBarComponent* HealthBar;

	/** Health 복제 도착(클라) / 서버 직접 호출 공용 — HP바 갱신. */
	UFUNCTION()
	void OnRep_Health();

	/** 부여된 대쉬 어빌리티 스펙 핸들(서버). */
	FGameplayAbilitySpecHandle DashAbilityHandle;

	/** 대쉬 목표 방향. Server_Dash → TryDash에서 세팅, 어빌리티 ActivateAbility에서 소비. */
	FVector PendingDashDir = FVector::ForwardVector;

	/** ASC ActorInfo 초기화 공용 헬퍼 (서버/클라 양쪽에서 호출). */
	void InitASCActorInfo();

	/** 현재 체력. 서버 권위, 클라 복제. 변경 시 OnRep_Health로 HP바 갱신. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 100.f;

	/** 탑뷰 카메라 붐 (절대 하향 고정) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** 탑뷰 카메라 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* TopDownCamera;
};
