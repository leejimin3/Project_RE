// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "REBulletPatternGenerator.h"
#include "Net/UnrealNetwork.h"
#include "REHealthBarComponent.h"
#include "REGameMode.h"
#include "HAL/IConsoleManager.h"
#include "REBulletRenderSubsystem.h"                        // 라이브 카운트(ISM) 조회 (#51)
#include "Components/InstancedStaticMeshComponent.h"

/**
 *  목표 동시 탄환 수. 발사 시점에 조회하므로 재시작 없이 다음 발사부터 반영된다.
 *  발사당 탄 수 = round(N × 발사주기 / 수명) 로 역산 (REBulletPattern::MakeSpiralForLiveCount).
 *  기본 480 = 기존 하드코딩(16발 / 0.1s × 3s)과 동일 — 회귀 없음.
 */
static TAutoConsoleVariable<int32> CVarBulletCount(
	TEXT("re.Bullets.Count"),
	480,
	TEXT("목표 동시 탄환 수(steady-state). 다음 발사부터 반영."),
	ECVF_Cheat);

// #51 클로즈드루프 적분 게인. 수명 지연(수명/발사주기 ≈ 30발) 대비 크면 진동한다.
// 튜닝 노브(리빌드 없이 -ExecCmds로 스윕). 기본값은 실측으로 확정.
static TAutoConsoleVariable<float> CVarSpawnKi(
	TEXT("re.Bullets.SpawnKi"),
	0.004f,   // 실측 확정: fill-phase 안티와인드업과 함께 1000/5000 모두 ±0.5%, 100 -6%(짧은 창). 0.008은 진동.
	TEXT("스폰 적분 게인 Ki. 라이브 카운트 목표 수렴 속도/안정성."),
	ECVF_Cheat);

AREBossCharacter::AREBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 체력 초기화 — MaxHealth 조정 시 정합 유지 (RECharacterBase 동일 패턴)
	Health = MaxHealth;

	// HP바 (#29) — 보스 빨강.
	HealthBar = CreateDefaultSubobject<UREHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(RootComponent);
	HealthBar->BarColor = FLinearColor::Red;
}

void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
	if (bIsDead)
	{
		return;
	}

	// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트, 총알 자체는 미전송).
	//          현재는 싱글 로컬 직접 스폰 경로.

	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::TriggerBulletPattern: UREBulletSpawnSubsystem NULL"));
		return;
	}

	TArray<FBulletSpawnParams> Params;
	switch (Pattern)
	{
	case EBulletPattern::Spiral:
	{
		// 발사 시점 조회 — CVar 변경이 재시작/타이머 재설정 없이 다음 발사부터 반영된다.
		const int32 TargetLive = CVarBulletCount.GetValueOnGameThread();

		// 라이브 탄환 수 = ISM 인스턴스 수(렌더 프로세서가 매 프레임 엔티티 수로 동기화).
		// 관측 불가(데디서버 등 ISM 없음)면 CurrentLive=-1 → 피드포워드 폴백.
		int32 CurrentLive = -1;
		if (const UREBulletRenderSubsystem* RS = GetWorld()->GetSubsystem<UREBulletRenderSubsystem>())
		{
			if (const UInstancedStaticMeshComponent* ISM = RS->GetISM())
			{
				CurrentLive = ISM->GetInstanceCount();
			}
		}

		const float FeedFwd = TargetLive * REBulletPattern::FireIntervalSec / REBulletPattern::BulletLifetimeSec;
		int32 Count;
		if (CurrentLive >= 0 && TargetLive > 0)
		{
			// 클로즈드루프(적분 제어): 스폰율을 오차만큼 램프. 소멸률이 얼마든 라이브=목표에서 램프가 멎어 정상상태 오차 0.
			// 단, 첫 1수명 동안은 아직 탄환이 채워지는 중이라 오차가 크게 양수 → 적분하면 와인드업으로 대폭 오버슈트한다.
			// 그 구간은 피드포워드로 채우기만 하고, 채워진 뒤(정상상태 근처)부터 적분으로 소멸분을 보정한다.
			const int32 FillShots = FMath::CeilToInt(REBulletPattern::BulletLifetimeSec / REBulletPattern::FireIntervalSec);
			if (SpiralShotCount < FillShots)
			{
				SpiralSpawnRate = FeedFwd;
			}
			else
			{
				SpiralSpawnRate += CVarSpawnKi.GetValueOnGameThread() * (TargetLive - CurrentLive);
				SpiralSpawnRate = FMath::Clamp(SpiralSpawnRate, 0.f, (float)TargetLive);  // anti-windup 상한
			}
			// 소수부 이월 내림 — 소형 타깃(rate~3.3)에서 round()가 매 발사 4로 올려 +20% 오버슛하는 것 방지.
			SpiralSpawnAccum += SpiralSpawnRate;
			Count = FMath::FloorToInt(SpiralSpawnAccum);
			SpiralSpawnAccum -= Count;
		}
		else
		{
			Count = FMath::RoundToInt(FeedFwd);  // 라이브 관측 불가(데디서버 등) → 오픈루프 폴백
		}
		++SpiralShotCount;

		const REBulletPattern::FSpiralParams SP = REBulletPattern::MakeSpiralRing(Count, SpiralBaseAngleDeg);
		Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: Target=%d Live=%d Rate=%.1f -> N=%d"),
			TargetLive, CurrentLive, SpiralSpawnRate, Params.Num());
		SpiralBaseAngleDeg += SpiralRotationStepDeg;  // 다음 호출 시 회전
		break;
	}
	case EBulletPattern::Fan:
	{
		REBulletPattern::FFanParams FP;
		Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Spread=%.1f -> N=%d"), FP.SpreadDeg, Params.Num());
		break;
	}
	case EBulletPattern::Homing:
		// M1 범위 밖 — 슬롯만 유지, 미구현. 스폰 없이 종료.
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss: Homing 미구현 (M1 범위 밖)"));
		return;
	}
	Spawner->SpawnBulletBatch(Params);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities at %s"),
		(int32)Pattern, Seed, StartTime, Params.Num(), *GetActorLocation().ToString());
}

float AREBossCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                    AController* EventInstigator, AActor* DamageCauser)
{
	// #46 측정 모드: 탄환이 보스 원점(0,0,90)에 스폰돼 자기 탄에 맞아 ~3초 만에 죽는다 →
	// 발사가 끊겨 Mass 탄환이 목표 수까지 못 찬다. 프로파일링 중에는 보스를 무적으로.
	static IConsoleVariable* KeepFiring = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Profiling.KeepFiring"));
	if (KeepFiring && KeepFiring->GetInt() != 0)
	{
		return 0.f;
	}

	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
	OnRep_Health(); // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출

	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss died (Health<=0)"));
		if (AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>())
		{
			GM->EndGame(/*bVictory=*/true);
		}
	}

	return Applied;
}

void AREBossCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AREBossCharacter, Health);
}

void AREBossCharacter::OnRep_Health()
{
	if (HealthBar)
	{
		HealthBar->SetHealthPercent(MaxHealth > 0.f ? Health / MaxHealth : 0.f);
	}
}
