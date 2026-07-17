// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGA_Dash.h"
#include "REGE_DashCooldown.h"
#include "REGameplayTags.h"
#include "Core/RECharacterBase.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionConstantForce.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

UREGA_Dash::UREGA_Dash()
{
	// 서버권위 실행 — 클라 예측 미사용(M4에서 LocalPredicted 전환 검토).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	// 인스턴스 필요(멤버 상태·RootMotion 태스크 보유).
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;

	// 쿨다운 GE 연결 — CommitAbility가 이 GE를 적용.
	CooldownGameplayEffectClass = UREGE_DashCooldown::StaticClass();

	// 활성 동안 소유자에 State.Dashing 부여(#27 무적판정 계약). EndAbility 시 자동 해제.
	ActivationOwnedTags.AddTag(RETag_State_Dashing);

	// 대쉬 모션 (M3.5 ②) — 실패해도 크래시 없이 진행.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> DashAnimAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Dash.MM_Dash"));
	if (DashAnimAsset.Succeeded())
	{
		DashAnim = DashAnimAsset.Object;
	}
}

void UREGA_Dash::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                 const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData)
{
	// 쿨다운/코스트 커밋 — 쿨다운 중이면 실패 → 대쉬 취소.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
		return;
	}

	// 아바타에서 대쉬 방향 획득(로컬이 계산해 서버 TryDash로 넣어둔 값).
	ARECharacterBase* Char = Cast<ARECharacterBase>(GetAvatarActorFromActorInfo());
	if (!Char)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const FVector Dir = Char->GetPendingDashDir();

	UE_LOG(LogTemp, Log, TEXT("[Dash] activate ok dir=%s"), *Dir.ToString());

	// 시간형 RootMotion — 고정속도×고정시간 → 마찰 무관 고정거리. 종료 시 속도 클리어.
	UAbilityTask_ApplyRootMotionConstantForce* Task =
		UAbilityTask_ApplyRootMotionConstantForce::ApplyRootMotionConstantForce(
			this,
			FName("Dash"),
			Dir,
			DashStrength,
			DashDuration,
			/*bIsAdditive=*/false,
			/*StrengthOverTime=*/nullptr,
			ERootMotionFinishVelocityMode::SetVelocity,
			/*SetVelocityOnFinish=*/FVector::ZeroVector,
			/*ClampVelocityOnFinish=*/0.f,
			/*bEnableGravity=*/false);

	Task->OnFinish.AddDynamic(this, &UREGA_Dash::OnDashFinished);
	Task->ReadyForActivation();

	// 대쉬 모션 — 코스메틱, RootMotion 이동(위 태스크)과 독립.
	// TODO M4: 데디 원격 클라 표시용 Multicast 검토.
	if (DashAnim)
	{
		if (UAnimInstance* AnimInst = Char->GetMesh() ? Char->GetMesh()->GetAnimInstance() : nullptr)
		{
			// MM_Dash는 루트모션 포함 AnimSequence(bEnableRootMotion — 애셋 자체 플래그, 공용이라 미변경).
			// UAnimMontage::bEnableRootMotionTranslation/Rotation은 4.5부터 PostLoad 동기화 전용 deprecated
			// 필드라 런타임 생성 다이나믹 몽타주에는 효과 없음 — 실제 추출은 AnimSequence 플래그가 좌우한다.
			// 기본 RootMotionMode(RootMotionFromMontagesOnly)에서 그 루트모션이 CharacterMovement에 그대로 먹혀
			// ApplyRootMotionConstantForce와 충돌, 대쉬 거리가 짧아지는 회귀(680→405)가 났다.
			// 몽타주 재생 구간만 IgnoreRootMotion(추출은 하되 적용은 안 함)으로 전환해 "코스메틱 독립"을 실제로 성립시킨다.
			AnimInst->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);

			// PlaySlotAnimationAsDynamicMontage는 float가 아니라 UAnimMontage*를 반환 — 길이는 GetPlayLength()로 조회.
			UAnimMontage* PlayedMontage = AnimInst->PlaySlotAnimationAsDynamicMontage(
				DashAnim, FName("DefaultSlot"), /*BlendInTime=*/0.1f, /*BlendOutTime=*/0.1f);
			const float Len = PlayedMontage ? PlayedMontage->GetPlayLength() : 0.f;
			UE_LOG(LogTemp, Log, TEXT("[Dash] anim len=%.2f"), Len);

			// 몽타주 재생이 끝나면 기본 모드로 복귀.
			FTimerHandle RestoreRootMotionTimer;
			GetWorld()->GetTimerManager().SetTimer(RestoreRootMotionTimer, FTimerDelegate::CreateLambda([AnimInst]()
			{
				if (IsValid(AnimInst))
				{
					AnimInst->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
				}
			}), Len > 0.f ? Len : DashDuration, false);
		}
	}
}

void UREGA_Dash::OnDashFinished()
{
	// RootMotion 종료 → 어빌리티 종료(State.Dashing 자동 해제).
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
}
