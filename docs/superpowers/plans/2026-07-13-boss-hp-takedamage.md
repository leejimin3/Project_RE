# M2 #28 보스 HP + 서버권위 TakeDamage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `REBossCharacter`에 `RECharacterBase`와 동일한 Replicated `Health` + `TakeDamage` HasAuthority 패턴을 추가하고, 사망 시(`Health<=0`) `TriggerBulletPattern`을 no-op화한다.

**Architecture:** `REBossCharacter`에 Health/TakeDamage/복제를 직접 인라인(컴포넌트 추출 없음, `RECharacterBase` 미러링). `bIsDead`는 서버 전용 비복제 bool — `TriggerBulletPattern` 최상단 가드로 `REGameMode::DemoFireTimer`(변경 없음)의 반복 호출을 무력화한다.

**Tech Stack:** UE 5.8 C++, `Net/UnrealNetwork.h`(`DOREPLIFETIME`). 신규 모듈·플러그인 없음.

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `Net/UnrealNetwork.h`는 Engine 모듈, 신규 의존성 없음.
- **브랜치:** `feature/M2-boss-hp` — `dev`에서 분기(완료: 스펙 커밋 `ad0a6ee` cherry-pick 반영됨). PR base=dev.
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그**(`[[headless-runtime-probe]]`).
- 로그 접두어 `[RE]` 고정.
- YAGNI: 사망 시 `Destroy()`/디스폰, 시각처리(메시 숨김/애니메이션), `bIsDead` 복제, 인스티게이터 크로스 오염 가드 — 전부 스코프 밖 (스펙 `2026-07-13-boss-hp-takedamage-design.md` §스코프 경계).

**검증된 API (UE 5.8 실물 — 파일:라인, `RECharacterBase`가 동일 패턴 선례):**
- `ARECharacterBase::TakeDamage` — `Core/RECharacterBase.cpp:77-90` (`HasAuthority()` 가드 → `Super::TakeDamage` → `FMath::Clamp(Health - Applied, 0.f, MaxHealth)`)
- `ARECharacterBase::GetLifetimeReplicatedProps` — `Core/RECharacterBase.cpp:92-96` (`DOREPLIFETIME(ARECharacterBase, Health)`)
- `Health = MaxHealth;` 생성자 초기화 — `Core/RECharacterBase.cpp:27`
- `AActor::TakeDamage(float, FDamageEvent const&, AController*, AActor*)` — `Actor.h:3660`
- `REAutoFireComponent::Fire` 호출부 — `Core/REAutoFireComponent.cpp:70` (`Nearest->TakeDamage(Damage, FDamageEvent(), InstigatorController, GetOwner())`, `Damage=10.f`, `FireInterval=0.25f`)
- `AREBossCharacter::TriggerBulletPattern` 현재 시그니처 — `Core/REBossCharacter.h:27`, 본문 `Core/REBossCharacter.cpp:12-52`
- `REGameMode::DemoFireTimer` — `Core/REGameMode.cpp:58-66` (0.1s 루프, `DemoBoss->TriggerBulletPattern` 호출, **이번 작업에서 미변경**)

---

### Task 1: 보스 Health/TakeDamage/복제 추가

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.h`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp`

**Interfaces:**
- Consumes: 없음(신규 멤버).
- Produces: `AREBossCharacter::Health`(float, Replicated), `AREBossCharacter::MaxHealth`(float, EditDefaultsOnly, 기본 100), `AREBossCharacter::bIsDead`(bool, private, 비복제) — Task 2가 `bIsDead`를 `TriggerBulletPattern` 가드에 사용.

- [ ] **Step 1: 헤더 수정**

`Source/Project_RE/Core/REBossCharacter.h` 전체를 다음으로 교체:
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

- [ ] **Step 2: cpp 수정 — include + 생성자 + TakeDamage/복제**

`Source/Project_RE/Core/REBossCharacter.cpp` 상단 include에 `Net/UnrealNetwork.h` 추가, 생성자에 `Health = MaxHealth;` 추가, `TriggerBulletPattern` 뒤에 `TakeDamage`/`GetLifetimeReplicatedProps` 추가:

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
(기존 `TriggerBulletPattern` 본문은 Task 2에서 가드만 추가 — 이 Step에서는 건드리지 않음.)

파일 끝(`TriggerBulletPattern` 마지막 `}` 뒤)에 추가:
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

- [ ] **Step 3: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M2): add boss Health/TakeDamage server-authoritative HP (#28)"
```

---

### Task 2: 사망 시 탄막 발사 정지 (`TriggerBulletPattern` 가드)

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp:12-52`(`TriggerBulletPattern` 본문 최상단)

**Interfaces:**
- Consumes: Task 1의 `bIsDead`(private bool, 같은 클래스 내부라 접근 가능).
- Produces: 없음(런타임 동작 변경). Task 3 프로브가 "사망 후 발사 로그 중단"을 관측.

- [ ] **Step 1: 가드 추가**

`Source/Project_RE/Core/REBossCharacter.cpp`의 `TriggerBulletPattern` 함수 본문 최상단(`// TODO M5: ...` 주석 위)에 삽입:

```cpp
void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
	if (bIsDead)
	{
		return;
	}

	// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트, 총알 자체는 미전송).
	//          현재는 싱글 로컬 직접 스폰 경로.
	...
```
(`...` 이하 기존 본문 그대로 — `UREBulletSpawnSubsystem* Spawner = ...`부터 끝까지 무변경.)

- [ ] **Step 2: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 3: 커밋**

```bash
git add Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M2): stop bullet pattern trigger after boss death (#28)"
```

---

### Task 3: 검증 — headless 프로브 + PR

신규 코드 없음. `REGameMode::BeginPlay`가 이미 `DemoBoss` 스폰 후 `DemoFireTimer`(0.1s)로 `TriggerBulletPattern` 반복 호출 중이고, `RECharacterBase`에 부착된 `REAutoFireComponent`(#26, `FireInterval=0.25f`, `Damage=10.f`)가 서버 스폰 시점부터 자동으로 보스를 조준·발사한다 — 별도 셋업 없이 사망까지 자연 도달.

**Files:** 없음 (관측만)

**Interfaces:**
- Consumes: Task 1의 `[RE] Boss died (Health<=0)` 로그, Task 2의 `TriggerBulletPattern` 가드, 기존 `[RE] Boss Spiral: ...` 발사 로그(`REBossCharacter.cpp:32`).
- Produces: 이슈 #28 완료 기준 실증.

- [ ] **Step 1: headless 런타임 프로브**

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
Expected:
```
[RE] Boss died (Health<=0)
<N>
```
**합격 기준:**
1. `[RE] Boss died (Health<=0)` 정확히 1회 출력 (오토파이어 10dmg/0.25s → 100HP 기준 2.5초 내 사망, 15초면 충분).
2. 사망 로그 타임스탬프 이후 `[RE] Boss Spiral` 로그가 더 이상 찍히지 않는지 로그 파일을 직접 열어 타임스탬프 비교 확인 (grep -c는 총 개수만 세므로, 사망 시점 이후 신규 라인이 없는지는 `grep -A` 또는 파일 직접 확인으로 재검증).

- [ ] **Step 2: PR 생성**

`gh pr create` — base `dev`, head `feature/M2-boss-hp`. 이슈 #28 메타(label `networking`+`C++`, milestone M2, assignee `leejimin3`) 미러링, 본문 6개 필드 규칙 준수(`[[pr-creation-convention]]`).

---

## 완료 후

- 사망 후 액터는 `Destroy()` 안 됨 — 필드에 `bIsDead=true` 상태로 유지. 디스폰/시각처리는 후속 이슈(HP바 #29 진행 시 재검토).
- `Health` 복제(`DOREPLIFETIME`)는 존재하나 클라 시각 소비(HP바)는 **#29** 몫.
- `TriggerBulletPattern`의 `bIsDead` 가드는 `REGameMode::DemoFireTimer`를 그대로 둔 채 발사만 무력화 — 데모 타이머 자체 정리는 스코프 밖(추후 GameMode 정리 이슈에서 다룰 사안이면 그때 언급).

## Self-Review

- **Spec coverage:** 스펙 §Health/MaxHealth/bIsDead 멤버 → Task 1 Step 1, §TakeDamage 5단계 → Task 1 Step 2, §GetLifetimeReplicatedProps → Task 1 Step 2, §TriggerBulletPattern 가드 → Task 2 Step 1, §검증 1(빌드) → Task 1/2 Step 3·2, §검증 2(headless 사망+발사중단 로그) → Task 3 Step 1, §커밋 계획 3개 → Task 1/2/3 각 커밋 스텝. 갭 없음.
- **Placeholder scan:** 코드 블록 전부 완전(`TriggerBulletPattern` 본문 중략 `...`은 기존 코드 무변경 표시일 뿐, 전문은 `REBossCharacter.cpp:12-52` 기존 파일에 이미 존재 — Task 2 Step 1에서 실제 편집 시 삽입 지점만 정확히 지정했으므로 모호성 없음).
- **Type consistency:** `TakeDamage` 4인자 시그니처(`float, const FDamageEvent&, AController*, AActor*`) = `RECharacterBase.h:40-41` 및 `Actor.h:3660`과 동일. `Health`/`MaxHealth`/`bIsDead` 필드명은 스펙과 동일, `RECharacterBase`의 `Health`/`MaxHealth`(같은 타입 float)와 이름 일치하되 별개 클래스 소속이라 충돌 없음. `DOREPLIFETIME(AREBossCharacter, Health)` — 클래스명 정확히 일치.
