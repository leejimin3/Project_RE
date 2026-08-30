// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHeadlessProbeComponent.h"
#include "Core/REPlayerController.h"
#include "Core/RECharacterBase.h"
#include "Core/REBossCharacter.h"
#include "Core/REGameMode.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Project_RE.h"                              // LogRENet

void UREHeadlessProbeComponent::StartProbes()
{
#if !UE_BUILD_SHIPPING
	RunMoveProbe();
	RunFireProbe();
	RunDashProbe();
#endif
}

#if !UE_BUILD_SHIPPING

namespace
{
	/**
	 *  소유 컨트롤러. 부류 2 — 이 컴포넌트는 AREPlayerController::BeginPlay 에서만
	 *  만들어지므로 소유자가 그 타입이 아닌 상황은 프로그래머 실수뿐이다.
	 */
	AREPlayerController* OwningPC(const UActorComponent* Self)
	{
		AREPlayerController* PC = Cast<AREPlayerController>(Self->GetOwner());
		ensureMsgf(PC, TEXT("[RE] HeadlessProbe: 소유자가 AREPlayerController 가 아니다"));
		return PC;
	}
}

void UREHeadlessProbeComponent::RunMoveProbe()
{
	AREPlayerController* PC = OwningPC(this);
	if (!PC)
	{
		return;
	}

	// 폰 possess 완료(BeginPlay 직후 possess 타이밍 여유) 후 1.0s에 자기이동 1회 + 오프메시 거부 1회.
	FTimerDelegate MoveDel = FTimerDelegate::CreateLambda([this, PC]()
	{
		APawn* P = PC->GetPawn();
		if (!P)
		{
			UE_LOG(LogRENet, Warning, TEXT("[Move] probe: no pawn"));
			return;
		}
		// 시작점에서 +Y 500 만큼 떨어진 목표(nav 위 예상).
		// +Y인 이유(#56): 보스가 +X 600에 있음 — +X 목표는 탄막 정면 진입(사망)이고,
		// 대쉬 프로브(+X)도 보스 캡슐에 막혀 거리 게이트가 무효화됨. 이동을 +Y로 빼면
		// 대쉬(+X)가 Y≈450에서 발사돼 보스와 안 겹침.
		ProbeTarget = P->GetActorLocation() + FVector(0.f, 500.f, 0.f);
		UE_LOG(LogRENet, Log, TEXT("[Move] probe start: pawn=%s target=%s"),
			*P->GetActorLocation().ToString(), *ProbeTarget.ToString());

		// 정상 이동 요청.
		PC->Server_RequestMove(ProbeTarget);

		// 오프메시 거부 검증: 맵 밖 좌표 1회.
		PC->Server_RequestMove(FVector(100000.f, 100000.f, 0.f));

		// 0.5s마다 목표까지 거리 로그(수렴 관측), 5s간.
		FTimerDelegate LogDel = FTimerDelegate::CreateLambda([this, PC]()
		{
			if (APawn* Pn = PC->GetPawn())
			{
				const float Dist = FVector::Dist2D(Pn->GetActorLocation(), ProbeTarget);
				UE_LOG(LogRENet, Log, TEXT("[Move] probe dist=%.1f loc=%s"),
					Dist, *Pn->GetActorLocation().ToString());
			}
		});
		PC->GetWorldTimerManager().SetTimer(ProbeLogTimer, LogDel, 0.5f, /*bLoop=*/true);
	});
	PC->GetWorldTimerManager().SetTimer(ProbeMoveTimer, MoveDel, 1.0f, /*bLoop=*/false);
}

void UREHeadlessProbeComponent::RunFireProbe()
{
	AREPlayerController* PC = OwningPC(this);
	if (!PC)
	{
		return;
	}

	// t=1.5s: 보스 방향 발사 1회(hit 기대) + 즉시 재발사(rate limit 차단 기대).
	// 이동 프로브(1.0s, +Y 이동)와 대쉬 프로브(2.0s, +X) 사이 — 서로 간섭 없음.
	// 발사 성공 시 StopMovement가 이동 프로브를 끊지만 dist 로그는 계속 나옴(수렴만 중단) — 게이트 아님.
	FTimerDelegate FireDel = FTimerDelegate::CreateLambda([PC]()
	{
		APawn* P = PC->GetPawn();
		if (!P)
		{
			UE_LOG(LogRENet, Warning, TEXT("[Attack] probe: no pawn"));
			return;
		}
		AREBossCharacter* Boss = nullptr;
		for (TActorIterator<AREBossCharacter> It(PC->GetWorld()); It; ++It)
		{
			Boss = *It;
			break;
		}
		if (!Boss)
		{
			UE_LOG(LogRENet, Warning, TEXT("[Attack] probe: no boss"));
			return;
		}
		const FVector Dir = (Boss->GetActorLocation() - P->GetActorLocation()).GetSafeNormal2D();
		UE_LOG(LogRENet, Log, TEXT("[Attack] probe fire dir=%s"), *Dir.ToString());
		PC->Server_RequestFire(Dir);   // 1발 — hit boss 기대
		PC->Server_RequestFire(Dir);   // 즉시 재발사 — rate-limited 기대

		// 발사 직후 이동 요청 — 사격 모션 락에 막혀야 한다. 대쉬의 "immediate retry" 게이트와 같은 취지다.
		// 막히면 `[Move] rejected: fire lock` 이 찍히고, 막히지 않으면 폰이 +Y로 걸어가
		// 이어지는 `[Move] probe dist` 가 흔들려 그것으로도 드러난다.
		const FVector LockProbeTarget = P->GetActorLocation() + FVector(0.f, 200.f, 0.f);
		UE_LOG(LogRENet, Log, TEXT("[Move] fire lock probe: 이동 요청 (거절 기대)"));
		PC->Server_RequestMove(LockProbeTarget);
	});
	PC->GetWorldTimerManager().SetTimer(ProbeFireTimer, FireDel, 1.5f, false);
}

void UREHeadlessProbeComponent::RunDashProbe()
{
	AREPlayerController* PC = OwningPC(this);
	if (!PC)
	{
		return;
	}

	// t=2.0s: 대쉬 1회 + 즉시 재시도(쿨다운 차단 확인). 이후 거리 측정, 2.1s 후 재활성, 종료.
	FTimerDelegate DashDel = FTimerDelegate::CreateLambda([this, PC]()
	{
		ARECharacterBase* Char = Cast<ARECharacterBase>(PC->GetPawn());
		if (!Char)
		{
			UE_LOG(LogRENet, Warning, TEXT("[Dash] probe: no pawn"));
			return;
		}
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Char);
		ProbeDashStart = Char->GetActorLocation();

		// 1) 정상 대쉬(+X 방향).
		const bool bFirst = Char->TryDash(FVector::ForwardVector);
		const bool bDashingTag = ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing);
		UE_LOG(LogRENet, Log, TEXT("[Dash] activate ok=%d State.Dashing=%d"), bFirst, bDashingTag);

		// 2) 즉시 재시도 → 쿨다운 차단 기대.
		const bool bBlocked = Char->TryDash(FVector::ForwardVector);
		UE_LOG(LogRENet, Log, TEXT("[Dash] immediate retry activated=%d (0=blocked by cooldown, 기대 0)"), bBlocked);

		// 3) 0.3s 후 이동거리 측정(RootMotion 완료 뒤).
		FTimerHandle DistTimer;
		FTimerDelegate DistDel = FTimerDelegate::CreateLambda([this, PC]()
		{
			if (APawn* Pn = PC->GetPawn())
			{
				const float Dist = FVector::Dist2D(Pn->GetActorLocation(), ProbeDashStart);
				UE_LOG(LogRENet, Log, TEXT("[Dash] dist=%.1f (기대 ~600)"), Dist);
			}
		});
		PC->GetWorldTimerManager().SetTimer(DistTimer, DistDel, 0.3f, false);

		// 4) 2.1s 후(쿨다운 만료) 재활성 → 성공 기대. 그 뒤 종료.
		FTimerHandle ReTimer;
		FTimerDelegate ReDel = FTimerDelegate::CreateLambda([PC]()
		{
			bool bReactivated = false;
			if (ARECharacterBase* Pn = Cast<ARECharacterBase>(PC->GetPawn()))
			{
				bReactivated = Pn->TryDash(FVector::ForwardVector);
			}
			UE_LOG(LogRENet, Log, TEXT("[Dash] re-activate ok=%d (기대 1, 쿨다운 만료)"), bReactivated);
			UE_LOG(LogRENet, Log, TEXT("[Dash] probe done"));
			// 종료 결정은 GameMode가 한다 (#87). 이 PC는 자기 프로브만 알아서,
			// 먼저 끝난 하나가 서버를 내리면 뒤 클라의 프로브가 시작조차 못 한다.
			if (AREGameMode* GM = PC->GetWorld() ? PC->GetWorld()->GetAuthGameMode<AREGameMode>() : nullptr)
			{
				GM->NotifyProbeComplete();
			}
		});
		PC->GetWorldTimerManager().SetTimer(ReTimer, ReDel, 2.1f, false);
	});
	PC->GetWorldTimerManager().SetTimer(ProbeDashTimer, DashDel, 2.0f, false);
}

#endif   // !UE_BUILD_SHIPPING
