# 탄막 패턴 시드 RPC 인터페이스 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `AREBossCharacter::TriggerBulletPattern(Pattern, Seed, StartTime)` 인터페이스를 만들고, 싱글 경로에서 Mass 엔티티 N개를 직접 스폰하는 스켈레톤을 확립한다 (M5 RPC 전환 지점 마킹 포함).

**Architecture:** `EBulletPattern` enum(슬롯만)과 `AREBossCharacter : ACharacter`를 새로 만든다. `TriggerBulletPattern`은 `MassEntitySubsystem → EntityManager → CreateArchetype({Sim,Render}) → CreateEntity × N` 검증된 패턴으로 placeholder 엔티티를 스폰한다. 패턴별 나선/부채꼴 수학은 M1, 실제 Multicast RPC는 M5 — 둘 다 TODO 주석으로 지점만 마킹. 검증은 빌드 성공 + GameMode BeginPlay에서 보스 스폰 후 트리거 호출 → headless 런타임 로그로 스폰 카운트 관측.

**Tech Stack:** UE 5.8 C++, MassEntity / MassCore 엔진 모듈 (이미 배선됨).

## Global Constraints

- 엔진: UE 5.8 (`E:\UE_5.8`). 프로젝트: `E:\UnrealProjects\Project_RE\Project_RE.uproject`.
- MassGameplay 플러그인 당기지 않음. `MassEntity`·`MassCore` 모듈만 사용 (Build.cs 모듈 의존 변경 금지).
- include 경로 `Project_RE/Mass`, `Project_RE/Core` 는 Build.cs에 이미 등록됨 — 수정 불필요.
- 네이밍: `RE` 접두어 컨벤션. 이슈의 `ABossCharacter` → **`AREBossCharacter`**.
- 스폰 상수 `N = 16` (placeholder). Velocity=0, Lifetime=0.
- 기존 스폰 패턴 참조: `AREGameMode::BeginPlay` (`Source/Project_RE/Core/REGameMode.cpp:23-28`).
- 자동화 테스트 인프라 없음. 태스크 게이트 = **빌드 성공**. 최종 게이트 = **headless 런타임 로그 관측**.
- 빌드 커맨드 (Git Bash):
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
    -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0.

---

### Task 1: EBulletPattern enum + AREBossCharacter 스폰 스켈레톤

**Files:**
- Create: `Source/Project_RE/Mass/REBulletPattern.h`
- Create: `Source/Project_RE/Core/REBossCharacter.h`
- Create: `Source/Project_RE/Core/REBossCharacter.cpp`

**Interfaces:**
- Consumes: `FBulletSimFragment`, `FBulletRenderFragment` (`Mass/REBulletFragments.h`), `UMassEntitySubsystem`, `FMassEntityManager`, `FMassArchetypeHandle`, `FMassEntityHandle`, `ACharacter`.
- Produces:
  - `enum class EBulletPattern : uint8 { Spiral, Fan, Homing }`
  - `class AREBossCharacter : public ACharacter` — public `void TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)`.

- [ ] **Step 1: EBulletPattern 헤더 생성**

`Source/Project_RE/Mass/REBulletPattern.h`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletPattern.generated.h"

/**
 *  보스 탄막 패턴 종류. M0은 슬롯만 — 각 패턴의 발사 수학은 M1.
 */
UENUM(BlueprintType)
enum class EBulletPattern : uint8
{
	Spiral,
	Fan,
	Homing
};
```

- [ ] **Step 2: AREBossCharacter 헤더 생성**

`Source/Project_RE/Core/REBossCharacter.h`:
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
 *  HP/TakeDamage 서버 권위 로직은 M2 범위.
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
};
```

- [ ] **Step 3: AREBossCharacter cpp 생성**

`Source/Project_RE/Core/REBossCharacter.cpp`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"

namespace
{
	/** M0 placeholder 스폰 개수. 실제 패턴별 탄 수는 M1. */
	constexpr int32 BulletsPerPattern = 16;
}

AREBossCharacter::AREBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
	// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트, 총알 자체는 미전송).
	//          현재는 싱글 로컬 직접 스폰 경로.

	UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	if (!Mass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::TriggerBulletPattern: UMassEntitySubsystem NULL"));
		return;
	}

	FMassEntityManager& EM = Mass->GetMutableEntityManager();
	FMassArchetypeHandle Arch = EM.CreateArchetype(
		{ FBulletSimFragment::StaticStruct(), FBulletRenderFragment::StaticStruct() });

	switch (Pattern)
	{
	case EBulletPattern::Spiral:
		// TODO M1: 나선 — 각도 증분으로 Velocity 세팅.
		break;
	case EBulletPattern::Fan:
		// TODO M1: 부채꼴 — 중심각 기준 좌우 분산 Velocity.
		break;
	case EBulletPattern::Homing:
		// TODO M1: 호밍 — 타깃 방향 Velocity + 추적 플래그.
		break;
	}

	// M0: 패턴 무관하게 placeholder 엔티티 N개 스폰 (Velocity=0, Lifetime=0). 실제 값은 M1.
	for (int32 i = 0; i < BulletsPerPattern; ++i)
	{
		EM.CreateEntity(Arch);
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities"),
		(int32)Pattern, Seed, StartTime, BulletsPerPattern);
}
```

- [ ] **Step 4: 빌드로 컴파일 확인**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletPattern.h Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M0): add EBulletPattern enum + boss TriggerBulletPattern spawn skeleton (#5)"
```

---

### Task 2: GameMode 검증 배선 + headless 런타임 프로브

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (include 1개 추가, BeginPlay 끝에 보스 스폰 + 트리거 호출)

**Interfaces:**
- Consumes: `AREBossCharacter::TriggerBulletPattern` (Task 1), `UWorld::SpawnActor`, `EBulletPattern`.
- Produces: 런타임 로그 라인 `[RE] Boss::TriggerBulletPattern: Pattern=0 Seed=12345 Start=0.00 -> spawned 16 entities`.

- [ ] **Step 1: REGameMode.cpp 에 보스 include 추가**

`Source/Project_RE/Core/REGameMode.cpp` 상단 include 블록에서 `#include "REBulletRenderProcessor.h"` 줄 바로 뒤에 추가:
```cpp
#include "REBossCharacter.h"
```

- [ ] **Step 2: BeginPlay 끝에 보스 스폰 + 트리거 호출 추가**

`Source/Project_RE/Core/REGameMode.cpp`의 `BeginPlay` 함수 **맨 끝** (기존 processor flags 로그 라인 뒤, 함수 닫는 `}` 앞)에 추가:
```cpp
	// #5 검증: 보스 스폰 후 탄막 트리거 → 싱글 경로 스폰 카운트 실증.
	if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(
			AREBossCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator))
	{
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
	}
```

- [ ] **Step 3: 빌드로 컴파일 확인**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: headless 런타임 프로브**

PIE 없이 headless(`-game -nullrhi`)로 BeginPlay 로그 관측. Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수 — 경로 mangling 방지):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe.log &
sleep 30
grep "\[RE\]" "Saved/Logs/RE_probe.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected 로그에 포함:
```
[RE] Boss::TriggerBulletPattern: Pattern=0 Seed=12345 Start=0.00 -> spawned 16 entities
```
`spawned 16 entities` 확인 → Acceptance (싱글에서 탄막 스폰됨) 충족.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M0): probe boss bullet pattern spawn in gamemode beginplay (#5)"
```

---

## 완료 후

- 브랜치 `feature/M0-bullet-pattern-rpc` → PR (base=dev, 이슈 #5 메타 미러링, 6개 필드 전부).
- #5 완료 → **M0 마일스톤 전체 종료**. 다음: M1 (Mass 보스 탄막 스폰 + 이동).
- 남은 TODO 마커: `TODO M1`(패턴 수학), `TODO M5`(Multicast RPC) — 후속 마일스톤에서 소진.
