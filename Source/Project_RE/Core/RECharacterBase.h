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
class UREAttackComponent;
class UREGA_Dash;
class UREHealthBarComponent;
class UAnimMontage;
class UAnimSequence;
class UNiagaraSystem;

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

	/** 생존 여부 (#85 보스 타깃 선택). bIsDead는 서버 전용이라 서버에서만 의미 있다. */
	bool IsAlive() const { return !bIsDead; }

	/** 현재/최대 체력 (#100 HUD). Health 는 복제되므로 클라에서도 읽을 수 있다. */
	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }

	/**
	 *  서버 패스팔로잉의 현재 목표 (#112). 오너 클라만 받는다.
	 *
	 *  데디에서 원격 클라의 폰은 RemoteRole == ROLE_AutonomousProxy 라, 서버는 틱에서
	 *  PerformMovement 를 부르지 않는다. 패스팔로잉이 거기 얹혀 있어 서버가 스스로 전진하지
	 *  못하고, 클라 ServerMove 가 올 때만 찔끔 움직인다(실측 62uu/s, 설계 600uu/s 의 1/10).
	 *
	 *  목표를 오너 클라에 알려주면 클라가 그 방향으로 AddMovementInput 을 넣는다. 그러면
	 *  클라 CMC 에 실제 입력이 생겨 ServerMove 가 가속을 싣고 오고, 서버는 정상 속도로 전진한다.
	 *  덤으로 클라 예측이 "브레이크"에서 "전진"으로 바뀌어 보정도 줄어든다.
	 *
	 *  bHasMoveTarget 이 false 면 목표 없음(도달·중단). 서버 권위는 그대로다 — 클라 예측은
	 *  직선이고 최종 위치는 서버가 정한다.
	 */
	UPROPERTY(Replicated)
	FVector_NetQuantize MoveTarget = FVector::ZeroVector;

	UPROPERTY(Replicated)
	bool bHasMoveTarget = false;

	/** 서버에서 목표 설정/해제. 패스팔로잉 시작·중단 지점에서 부른다. */
	void SetMoveTarget(const FVector& InTarget);
	void ClearMoveTarget();

	bool HasMoveTarget() const { return bHasMoveTarget; }
	FVector GetMoveTarget() const { return MoveTarget; }

	/**
	 *  전 클라 발사 모션 재생 (M4 #74). 코스메틱 전용 — 판정·데미지·rate limit과 무관.
	 *
	 *  RPC 배치 근거(이슈 #74 b안): UREAttackComponent는 복제 설정이 없다(SetIsReplicatedByDefault 미호출).
	 *  컴포넌트에 Multicast를 두려면 컴포넌트 복제를 새로 켜야 하고, 그러면 코스메틱 한 줄 때문에
	 *  이 컴포넌트가 통째로 복제 대상이 된다. 캐릭터는 이미 복제 액터이고 메시도 여기 있으므로
	 *  RPC를 캐릭터에 두고 컴포넌트가 오너를 호출한다.
	 *
	 *  신뢰성 근거: Unreliable. 코스메틱이라 연사 중 1발 드랍이 판정/데미지에 영향이 없고,
	 *  Reliable이면 연사가 신뢰 큐를 점유해 실제 게임플레이 RPC를 밀어낼 수 있다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayFireMontage(UAnimMontage* Montage);

	/**
	 *  대쉬 모션을 모든 인스턴스(서버 자신 + 전 클라)에 재생. 코스메틱 전용.
	 *  Unreliable — 드랍돼도 이동/판정(서버 RootMotion)과 무관하다.
	 *  서버 권위 코드(UREGA_Dash::ActivateAbility)에서만 호출한다.
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayDashMontage();

protected:
	/** 게임플레이 어빌리티 시스템 컴포넌트. Pawn 소유, Mixed 복제. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UAbilitySystemComponent* AbilitySystemComponent;

	/** 수동공격 컴포넌트 (M3.5 ①). 서버에서 Server_RequestFire 경유로만 발사. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attack", meta = (AllowPrivateAccess = "true"))
	UREAttackComponent* AttackComponent;

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

	/** 대쉬 모션(AnimSequence — ABP DefaultSlot에 다이나믹 몽타주로 재생). 코스메틱. */
	UPROPERTY()
	TObjectPtr<UAnimSequence> DashAnim;

	/**
	 *  대쉬 잔상 VFX(NS_Dash_Ghost). 코스메틱 — DashAnim과 같은 자리에서 같은 이유로 산다(#116).
	 *  유료 애셋이라 .gitignore 대상이다. 없는 환경에서는 null로 남고 VFX만 생략된다.
	 */
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> DashVfx;

	/** ASC ActorInfo 초기화 공용 헬퍼 (서버/클라 양쪽에서 호출). */
	void InitASCActorInfo();

	/** 현재 체력. 서버 권위, 클라 복제. 변경 시 OnRep_Health로 HP바 갱신. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. 비복제 — 서버/클라 모두 생성자에서 같은 ini(UREStatsSettings)를 읽고 런타임 변경 코드가 없다 (#73). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 100.f;

	/** 사망 여부. 서버 전용 — 클라 시각처리는 스코프 밖이라 비복제. (AREBossCharacter 동일 패턴) */
	bool bIsDead = false;

	/** 탑뷰 카메라 붐 (절대 하향 고정) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** 탑뷰 카메라 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* TopDownCamera;
};
