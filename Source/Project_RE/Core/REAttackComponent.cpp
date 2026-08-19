// Copyright Epic Games, Inc. All Rights Reserved.

#include "REAttackComponent.h"
#include "REBossCharacter.h"
#include "RECharacterBase.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"
#include "REStatsSettings.h"

UREAttackComponent::UREAttackComponent()
{
	// 호출 구동(Server RPC 경유) — 틱/타이머 불필요.
	PrimaryComponentTick.bCanEverTick = false;

	// 공격 스탯 — Settings 단일 출처 (M3.5 ③).
	const UREStatsSettings* Stats = GetDefault<UREStatsSettings>();
	Damage         = Stats->AttackDamage;
	AttackInterval = Stats->AttackInterval;
	AttackRange    = Stats->AttackRange;

	// 발사 모션 (M3.5 ②, #132에서 교체) — 실패해도 크래시 없이 진행(모션만 생략).
	//
	// MM_Pistol_Fire_Montage(및 그것이 감싸던 MM_Pistol_Fire)를 버리고 MM_Pistol_DryFire를 쓴다.
	// 전자는 **애디티브**(AAT_ROTATION_OFFSET_MESH_SPACE, 실측)라 애디티브 슬롯을 통해서만
	// 포즈가 적용된다 — 그 슬롯이 몽타주의 `Arms`였고 ABP_Unarmed에는 그 노드가 없다.
	// 일반 슬롯으로 재생하면 델타만 남아 화면상 아무 변화가 없다(실측으로 확인).
	// MM_Pistol_DryFire는 AAT_NONE 전신 격발 모션이라 DefaultSlot에서 그대로 보인다.
	// 루트모션도 없어 대쉬처럼 IgnoreRootMotion으로 감쌀 필요가 없다(실측).
	static ConstructorHelpers::FObjectFinder<UAnimSequence> FireAnimAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_DryFire.MM_Pistol_DryFire"));
	if (FireAnimAsset.Succeeded())
	{
		FireAnim = FireAnimAsset.Object;
	}
}

bool UREAttackComponent::FireInDirection(const FVector& Dir)
{
	// 서버 전용 — 판정·데미지는 서버 권위 (호출자가 Server RPC지만 방어적 재가드).
	if (!GetOwner()->HasAuthority())
	{
		return false;
	}

	// rate limit — 클라 페이싱과 별개로 서버가 재검증. 0.9배: 프레임/네트워크 지터 허용 오차.
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastFireTime >= 0.0 && Now - LastFireTime < AttackInterval * 0.9)
	{
		UE_LOG(LogTemp, Log, TEXT("[Attack] rate-limited (dt=%.2f)"), Now - LastFireTime);
		return false;
	}
	LastFireTime = Now;

	// 총구 높이(Z+20)에서 Dir 방향으로 사거리만큼 수평 트레이스. 자기 자신 무시.
	// Z+50이면 보스 캡슐(중심 90, HalfHeight 88 → 상단 178)을 스치듯 넘어가 미스 — 20으로 하향.
	const FVector Start = GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	const FVector End = Start + Dir * AttackRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(REAttack), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

	// 발사 모션 + 이펙트 — 코스메틱, 판정과 무관하게 발사 자체에 재생 (M4 #74, 이펙트 #120).
	// 서버 메시에 직접 재생하면 데디에선 아무도 못 본다 — 오너 캐릭터의 Multicast로 위임한다.
	// 여기서 직접 재생하지 않으므로 리슨서버 이중 재생 경로가 없다(Multicast가 서버에서도 실행되어 1회).
	// 배치/신뢰성 근거는 ARECharacterBase::Multicast_PlayFire 주석 참조.
	//
	// 트레이스 **뒤**에서 부른다 — 빔의 끝점과 임팩트 지점이 판정 결과이기 때문이다.
	// 판정 자체는 위 트레이스가 이미 끝냈으므로 순서를 옮겨도 결과가 바뀌지 않는다.
	if (ARECharacterBase* OwnerChar = Cast<ARECharacterBase>(GetOwner()))
	{
		OwnerChar->Multicast_PlayFire(FireAnim, bBlockingHit ? Hit.ImpactPoint : End, bBlockingHit);
	}

	AREBossCharacter* Boss = bBlockingHit ? Cast<AREBossCharacter>(Hit.GetActor()) : nullptr;
	if (Boss)
	{
		// 서버 권위 데미지 — 보스 HP 차감은 AREBossCharacter::TakeDamage override가 수신.
		APawn* OwnerPawn = Cast<APawn>(GetOwner());
		AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
		const float Applied = Boss->TakeDamage(Damage, FDamageEvent(), InstigatorController, GetOwner());
		UE_LOG(LogTemp, Log, TEXT("[Attack] hit boss, applied=%.1f"), Applied);
	}
	else
	{
		// 방향이 빗나감(0) 또는 다른 것에 막힘(1) — 유저 조준 실패는 정상 케이스.
		UE_LOG(LogTemp, Log, TEXT("[Attack] miss (blocked=%d)"), bBlockingHit ? 1 : 0);
	}

#if ENABLE_DRAW_DEBUG
	// 개발 확인용 트레이스 라인 — 히트=빨강, 미스=초록. Shipping 자동 제외.
	DrawDebugLine(GetWorld(), Start, bBlockingHit ? Hit.ImpactPoint : End,
		Boss ? FColor::Red : FColor::Green, false, 0.2f, 0, 1.f);
#endif

	return true;
}
