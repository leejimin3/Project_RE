# 구현 목표: [M2 #28] 보스 HP + 서버권위 TakeDamage

## 컨텍스트
UE 5.8 C++ 탑뷰 탄막 프로젝트. 이슈 **#28**, 마일스톤 **M2: 플레이어 게임루프**.
`REBossCharacter`에 `RECharacterBase`와 동일한 Replicated `Health` + `TakeDamage` HasAuthority 패턴을 추가하고, 사망 시(`Health<=0`) `TriggerBulletPattern`을 no-op화한다. `#26` `REAutoFireComponent`가 이미 `HitActor->TakeDamage(10)`을 호출중 — 엔진 기본 `TakeDamage`만 받던 계약을 이번에 override로 완성한다.

스코프 밖(후속 이슈, 손대지 말 것): 사망 후 `Destroy()`/디스폰(HP바 #29 진행 시 재검토), 시각처리(메시 숨김/애니메이션/이펙트), `bIsDead` 복제, 탄막 대 플레이어 피격 판정(#27, 별도), `REAutoFireComponent`/`REGameMode` 로직 자체(계약 소비만), 인스티게이터 크로스 오염 가드.

설계 스펙: `docs/superpowers/specs/2026-07-13-boss-hp-takedamage-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-13-boss-hp-takedamage.md`
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치
`dev`에서 분기: `feature/M2-boss-hp` (이미 생성됨, 스펙+플랜 문서 커밋 완료 — 새 세션은 이 브랜치에서 계속 작업)

## 전역 제약
- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `Net/UnrealNetwork.h`는 Engine 모듈, 신규 의존성 없음.
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그**.
- 로그 접두어 `[RE]` 고정.
- YAGNI: 사망 시 `Destroy()`/디스폰, 시각처리, `bIsDead` 복제, 인스티게이터 크로스 오염 가드 — 전부 스코프 밖.

## 검증된 API (실물 확인됨)
- `ARECharacterBase::TakeDamage` — `Core/RECharacterBase.cpp:77-90` (`HasAuthority()` 가드 → `Super::TakeDamage` → `FMath::Clamp(Health - Applied, 0.f, MaxHealth)`)
- `ARECharacterBase::GetLifetimeReplicatedProps` — `Core/RECharacterBase.cpp:92-96` (`DOREPLIFETIME(ARECharacterBase, Health)`)
- `Health = MaxHealth;` 생성자 초기화 — `Core/RECharacterBase.cpp:27`
- `AActor::TakeDamage(float, FDamageEvent const&, AController*, AActor*)` — `Actor.h:3660`
- `REAutoFireComponent::Fire` 호출부 — `Core/REAutoFireComponent.cpp:70` (`Nearest->TakeDamage(Damage, FDamageEvent(), InstigatorController, GetOwner())`, `Damage=10.f`, `FireInterval=0.25f`)
- `AREBossCharacter::TriggerBulletPattern` 현재 시그니처 — `Core/REBossCharacter.h:27`, 본문 `Core/REBossCharacter.cpp:12-52`
- `REGameMode::DemoFireTimer` — `Core/REGameMode.cpp:58-66` (0.1s 루프, `DemoBoss->TriggerBulletPattern` 호출, **이번 작업에서 미변경**)

## 기존 파일 현황 (변경 대상)
`Source/Project_RE/Core/REBossCharacter.h` — `AREBossCharacter : public ACharacter`. 멤버: 생성자, `TriggerBulletPattern(EBulletPattern, int32, int32)`(3번째 인자는 실제로 `float StartTime`), private `SpiralBaseAngleDeg`/`SpiralRotationStepDeg`. Health/TakeDamage 없음(주석 "M2 범위"만).

`Source/Project_RE/Core/REBossCharacter.cpp` — 생성자는 `PrimaryActorTick.bCanEverTick = false;`만. `TriggerBulletPattern` 본문: Spawner null 체크 → Pattern switch(Spiral/Fan/Homing) → `Spawner->SpawnBulletBatch(Params)` → 로그.

================================================================
## TASK 1: 보스 Health/TakeDamage/복제 추가
================================================================
### 1-1. `Source/Project_RE/Core/REBossCharacter.h` (전체 교체)
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "REBulletPattern.h"
#include "REBossCharacter.generated.h"

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

private:
	/** Spiral 호출마다 누적되는 시작각. 연속 트리거 시 링이 회전한다. */
	float SpiralBaseAngleDeg = 0.f;
	/** Spiral 호출당 BaseAngle 증가량(deg). */
	static constexpr float SpiralRotationStepDeg = 15.f;

	/** 현재 체력. 서버 권위, 클라 복제. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 100.f;

	/** 사망 여부. 서버 전용 — 클라 시각처리는 스코프 밖이라 비복제. */
	bool bIsDead = false;
};
```

### 1-2. `Source/Project_RE/Core/REBossCharacter.cpp` (include+생성자 수정, TakeDamage/복제 함수 추가)
파일 상단(include~생성자)을 다음으로 교체:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "REBulletPatternGenerator.h"
#include "Net/UnrealNetwork.h"

AREBossCharacter::AREBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	// 체력 초기화 — MaxHealth 조정 시 정합 유지 (RECharacterBase 동일 패턴)
	Health = MaxHealth;
}
```
(`TriggerBulletPattern` 함수 본문은 이 스텝에서 무변경 — TASK 2에서 가드만 추가.)

파일 맨 끝(`TriggerBulletPattern` 마지막 `}` 뒤)에 아래 추가:
```cpp

float AREBossCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                    AController* EventInstigator, AActor* DamageCauser)
{
	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);

	if (Health <= 0.f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss died (Health<=0)"));
	}

	return Applied;
}

void AREBossCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AREBossCharacter, Health);
}
```

### 1-3. 빌드/검증 게이트
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대 출력: `Result: Succeeded`, 에러 0.

### 1-4. 커밋
```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M2): add boss Health/TakeDamage server-authoritative HP (#28)"
```

================================================================
## TASK 2: 사망 시 탄막 발사 정지 (`TriggerBulletPattern` 가드)
================================================================
### 2-1. `Source/Project_RE/Core/REBossCharacter.cpp` — `TriggerBulletPattern` 본문 최상단에 가드 삽입
함수 시작부(`// TODO M5: ...` 주석 바로 위)에 `if (bIsDead) { return; }` 삽입 — 함수 전체는 다음과 같이 됨:
```cpp
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
		REBulletPattern::FSpiralParams SP;
		SP.BaseAngleDeg = SpiralBaseAngleDeg;
		Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: BaseAngle=%.1f -> N=%d"), SpiralBaseAngleDeg, Params.Num());
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
```

### 2-2. 빌드/검증 게이트
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대 출력: `Result: Succeeded`, 에러 0.

### 2-3. 커밋
```bash
git add Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M2): stop bullet pattern trigger after boss death (#28)"
```

================================================================
## TASK 3: 검증 — headless 프로브
================================================================
신규 코드 없음. `REGameMode::BeginPlay`가 이미 `DemoBoss` 스폰 후 `DemoFireTimer`(0.1s)로 `TriggerBulletPattern` 반복 호출 중이고, `RECharacterBase`에 부착된 `REAutoFireComponent`(#26, `FireInterval=0.25f`, `Damage=10.f`)가 서버 스폰 시점부터 자동으로 보스를 조준·발사한다 — 별도 셋업 없이 사망까지 자연 도달.

### 3-1. headless 런타임 프로브
Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe28.log &
sleep 15
grep "\[RE\] Boss died" "Saved/Logs/RE_probe28.log"
grep -c "\[RE\] Boss Spiral" "Saved/Logs/RE_probe28.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대 출력:
```
[RE] Boss died (Health<=0)
<N>
```
**합격 기준:**
1. `[RE] Boss died (Health<=0)` 정확히 1회 출력 (오토파이어 10dmg/0.25s → 100HP 기준 2.5초 내 사망, 15초면 충분).
2. 사망 로그 타임스탬프 이후 `[RE] Boss Spiral` 로그가 더 이상 찍히지 않는지 로그 파일을 직접 열어(또는 `grep -A`) 타임스탬프 비교 확인 — `grep -c`는 총 개수만 세므로 별도 확인 필요.

미관측 시(사망 로그 안 찍힘): 15초 부족 가능성 → `sleep 30`으로 재시도. 그래도 안 되면 `REGameMode::BeginPlay` 순서(보스 스폰 → 플레이어 폰 스폰 시점) 확인.

================================================================
## 완료 후
================================================================
PR: `gh pr create` — base `dev`, head `feature/M2-boss-hp`. 이슈 #28 메타 미러링 — label `networking`+`C++`, milestone `M2: 플레이어 게임루프`, assignee `leejimin3`. 본문 6개 필드 규칙 준수(PR 생성 규칙 메모리 참고 — 이슈 메타 미러링, Reviewer 생략).

남은 의도된 TODO: `TriggerBulletPattern` 상단 `// TODO M5: Multicast_TriggerPattern RPC...` 주석은 무변경 유지(M5 몫). `AREBossCharacter::TakeDamage`에 M5용 추가 TODO 없음 — 사망 후 디스폰은 별도 이슈에서.

## 하지 말 것 (스코프 밖)
- 보스 사망 후 `Destroy()`/디스폰 — 액터 유지, `bIsDead` 플래그만.
- 사망 시각처리(메시 숨김/애니메이션/이펙트).
- `bIsDead` 복제 — 서버 전용 비복제로 유지.
- HP바 UI — **#29**.
- 탄막 대 플레이어 피격 판정 — **#27** (별도 스펙/플랜).
- `REAutoFireComponent`, `REGameMode` 로직 자체 수정 — 계약 소비만, 일절 미접촉.
- 인스티게이터 크로스 오염 가드(피격 주체 검증 등) — 없음. `RECharacterBase` 동일 패턴 미러링만.
