# 플레이어 자동사격 (라인트레이스) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 플레이어에 서버 권위 자동사격(주기 타이머 → 최근접 보스 라인트레이스 → `TakeDamage`)을 추가한다. 이슈 **#26**.

**Architecture:** 신규 `UREAutoFireComponent`(UActorComponent)가 서버에서만 타이머를 돌려 최근접 `AREBossCharacter`를 라인트레이스 조준·발사한다. 입력이 없으므로 RPC 없음 — 조준·트레이스·데미지 전부 서버 계산. 데미지는 엔진 기본 `AActor::TakeDamage` 호출까지가 #26 계약이고, 보스 HP 차감은 #28이 override로 받는다.

**Tech Stack:** UE 5.8 C++, TimerManager, LineTraceSingleByChannel(ECC_Pawn), TActorIterator, DrawDebugLine.

## Global Constraints

- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 자동 테스트 인프라 없음 → 검증 게이트 = **에디터 빌드 성공(에러 0)** + **headless 런타임 프로브 로그 관측**.
- 튜닝값: `FireInterval = 0.25s`, `Damage = 10.f` (`EditDefaultsOnly`).
- 사거리 무제한, 타겟 = 최근접 보스 단일 기준 (스펙 확정).
- **#25 세션 작업 영역(`Abilities/`, `REPlayerController.h/.cpp`) 일절 미접촉.** `RECharacterBase`는 부착 코드(멤버 1개 + 생성자 1줄)만.
- 보스 Health/TakeDamage override → **#28 스코프, 구현 금지**. 피격판정(#27)·HP바(#29)·이펙트/사운드 스코프 밖.
- 한글 주석 스타일 유지. uasset 없이 코드 정의 (프로젝트 규약).
- 커밋: Conventional Commits, 태스크당 1커밋. 푸터 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- 빌드 명령:
  ```
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0. (에디터 열려 있으면 종료 후 실행 — 파일락 회피.)

## 브랜치

`dev`에서 분기: `feature/M2-autofire`. #26은 #25와 코드 독립 (앵커는 #23에 이미 존재). 단 현 워킹트리는 #25 세션 사용중 — 경로 택일:
- **A. #25 머지 후 같은 트리**: `git checkout dev && git pull && git checkout -b feature/M2-autofire`
- **B. 지금 병렬 (worktree)**: `git worktree add ../Project_RE-autofire -b feature/M2-autofire dev` — 이후 전 작업을 새 디렉토리 기준. untracked 문서 3개 복사 필요 (goal 문서 TASK 0 참고).

(이미 존재하면 체크아웃만.)

## 파일 구조

| 파일 | 책임 |
|---|---|
| `Source/Project_RE/Core/REAutoFireComponent.h/.cpp` (신규) | 서버 전용 자동사격: 타이머 → 최근접 보스 탐색 → 라인트레이스 → TakeDamage + 디버그 라인 |
| `Source/Project_RE/Core/RECharacterBase.h/.cpp` (수정) | 컴포넌트 부착만 (멤버 1개 + CreateDefaultSubobject 1줄) |

---

### Task 0: 브랜치 생성 + 설계 문서 커밋

**Files:**
- Commit: `docs/superpowers/specs/2026-07-13-autofire-linetrace-design.md` (이미 작성됨, untracked)
- Commit: `docs/superpowers/plans/2026-07-13-autofire-linetrace.md` (이 파일)

- [ ] **Step 1: 브랜치 생성**

```bash
git checkout dev && git pull && git checkout -b feature/M2-autofire
```
기대: `Switched to a new branch 'feature/M2-autofire'`. (untracked 문서 2개는 브랜치 전환에 그대로 따라옴.)

- [ ] **Step 2: 문서 커밋**

```bash
git add docs/superpowers/specs/2026-07-13-autofire-linetrace-design.md docs/superpowers/plans/2026-07-13-autofire-linetrace.md
git commit -m "docs(M2): autofire design spec + implementation plan (#26)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 1: REAutoFireComponent

**Files:**
- Create: `Source/Project_RE/Core/REAutoFireComponent.h`
- Create: `Source/Project_RE/Core/REAutoFireComponent.cpp`

**Interfaces:**
- Consumes: `AREBossCharacter` (기존, `Core/REBossCharacter.h`) — 타겟 클래스. `AActor::TakeDamage` (엔진 기본).
- Produces: `class UREAutoFireComponent : public UActorComponent` — Task 2가 `CreateDefaultSubobject<UREAutoFireComponent>(TEXT("AutoFire"))`로 부착. 외부 호출 API 없음(BeginPlay 자가 구동).

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/Core/REAutoFireComponent.h`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "REAutoFireComponent.generated.h"

/**
 *  플레이어 자동사격 컴포넌트 (#26).
 *  서버 전용 타이머로 최근접 AREBossCharacter를 라인트레이스 조준·주기 발사.
 *  입력 없음 → RPC 없음: 조준·트레이스·데미지 전부 서버 계산 (치팅 표면 0).
 *  데미지는 AActor::TakeDamage 호출까지 — 보스 HP 차감은 #28이 override로 수신 (계약).
 */
UCLASS()
class UREAutoFireComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UREAutoFireComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 발사 1회: 최근접 보스 탐색 → 라인트레이스 → 히트 시 TakeDamage. */
	void Fire();

	/** 발사 간격(s). */
	UPROPERTY(EditDefaultsOnly, Category = "AutoFire")
	float FireInterval = 0.25f;

	/** 발사당 데미지. */
	UPROPERTY(EditDefaultsOnly, Category = "AutoFire")
	float Damage = 10.f;

	FTimerHandle FireTimer;
};
```

- [ ] **Step 2: cpp 작성**

`Source/Project_RE/Core/REAutoFireComponent.cpp`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REAutoFireComponent.h"
#include "REBossCharacter.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

UREAutoFireComponent::UREAutoFireComponent()
{
	// 타이머 구동 — 틱 불필요.
	PrimaryComponentTick.bCanEverTick = false;
}

void UREAutoFireComponent::BeginPlay()
{
	Super::BeginPlay();

	// 서버 전용 — 자동사격엔 클라 입력이 없어 RPC 불필요. 클라에선 타이머 자체를 안 돈다.
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &UREAutoFireComponent::Fire, FireInterval, /*bLoop=*/true);
}

void UREAutoFireComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(FireTimer);
	Super::EndPlay(EndPlayReason);
}

void UREAutoFireComponent::Fire()
{
	// 최근접 보스 탐색 — 보스 1~2마리 전제, 매 발사 전체 스캔(캐싱 불필요).
	AREBossCharacter* Nearest = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	const FVector OwnerLoc = GetOwner()->GetActorLocation();
	for (TActorIterator<AREBossCharacter> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(OwnerLoc, It->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Nearest = *It;
		}
	}
	if (!Nearest)
	{
		// 보스 없는 맵 — 발사 스킵 (스팸 방지 Verbose).
		UE_LOG(LogTemp, Verbose, TEXT("[AutoFire] no boss found"));
		return;
	}

	// 총구 높이(Z+50)에서 보스 캡슐 중심으로 트레이스. 자기 자신 무시.
	const FVector Start = OwnerLoc + FVector(0.f, 0.f, 50.f);
	const FVector End = Nearest->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(REAutoFire), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

	const bool bHitBoss = bBlockingHit && Hit.GetActor() == Nearest;
	if (bHitBoss)
	{
		// 서버 권위 데미지 — AActor::TakeDamage 진입점 호출까지가 #26. HP 차감은 #28의 override.
		APawn* OwnerPawn = Cast<APawn>(GetOwner());
		AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
		const float Applied = Nearest->TakeDamage(Damage, FDamageEvent(), InstigatorController, GetOwner());
		UE_LOG(LogTemp, Log, TEXT("[AutoFire] hit boss, applied=%.1f"), Applied);
	}
	else
	{
		// 장애물에 막힘(1) 또는 노히트(0) — 탑뷰 개활지에선 드묾.
		UE_LOG(LogTemp, Log, TEXT("[AutoFire] miss (blocked=%d)"), bBlockingHit ? 1 : 0);
	}

#if ENABLE_DRAW_DEBUG
	// 개발 확인용 트레이스 라인 — 히트=빨강, 미스=초록. Shipping 자동 제외.
	DrawDebugLine(GetWorld(), Start, bBlockingHit ? Hit.ImpactPoint : End,
		bHitBoss ? FColor::Red : FColor::Green, false, 0.2f, 0, 1.f);
#endif
}
```

- [ ] **Step 3: 빌드 검증**

Run:
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0. (이 시점엔 컴포넌트가 어디에도 부착 안 됨 — 컴파일만 확인.)

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/REAutoFireComponent.h Source/Project_RE/Core/REAutoFireComponent.cpp
git commit -m "feat(M2): add REAutoFireComponent server-authoritative linetrace fire (#26)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: 플레이어 캐릭터에 부착

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Consumes: `UREAutoFireComponent` (Task 1).
- Produces: `ARECharacterBase::AutoFireComponent` (`UPROPERTY VisibleAnywhere`) — 에디터 노출용. 다른 태스크가 호출하는 API 없음.

> **주의:** #25 세션이 이 파일을 수정했다. dev 머지 후 라인 번호가 아래 예시와 다를 수 있음 — **앵커 텍스트 기준**으로 삽입 위치를 찾을 것. 부착 외 어떤 것도 변경 금지 (Surgical).

- [ ] **Step 1: RECharacterBase.h — 전방선언 + 멤버 추가**

전방선언 블록(파일 상단 `class UAbilitySystemComponent;` 부근)에 추가:
```cpp
class UREAutoFireComponent;
```

`protected:` 섹션의 `AbilitySystemComponent` UPROPERTY 선언 **뒤에** 추가:
```cpp
	/** 자동사격 컴포넌트 (#26). 서버에서만 구동. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoFire", meta = (AllowPrivateAccess = "true"))
	UREAutoFireComponent* AutoFireComponent;
```

- [ ] **Step 2: RECharacterBase.cpp — include + 생성**

include 블록의 `#include "AbilitySystemComponent.h"` **뒤에** 추가:
```cpp
#include "REAutoFireComponent.h"
```

생성자에서 ASC 생성부(`AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(...)` 블록) **뒤에** 추가:
```cpp
	// 자동사격 부착 — 컴포넌트가 BeginPlay에서 서버 여부를 스스로 판단.
	AutoFireComponent = CreateDefaultSubobject<UREAutoFireComponent>(TEXT("AutoFire"));
```

- [ ] **Step 3: 빌드 검증**

Run:
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

정적 확인:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "AutoFire" Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp
```
기대: 헤더 2건(전방선언 제외 멤버+카테고리), cpp 1~2건(include+생성) — 그 외 파일 변경 없음.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp
git commit -m "feat(M2): attach AutoFireComponent to player character (#26)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Headless 프로브 로그 관측 (신규 코드 없음)

**Files:** 없음 — Task 1의 `[AutoFire]` 로그가 곧 프로브. `REGameMode::BeginPlay`가 DemoBoss를 (0,0,90)에 스폰하므로 별도 셋업 없이 자동사격이 자연 발동한다.

**Interfaces:**
- Consumes: `[AutoFire]` 접두 로그 (Task 1), `REGameMode` DemoBoss 스폰 (기존).
- Produces: 검증 게이트 통과 판정.

- [ ] **Step 1: headless 실행 + 로그 관측**

Run (MSYS_NO_PATHCONV 필수 — 맵 경로 `/Game/...` 변형 방지. dev에 #25 대쉬 프로브가 있으면 ~4초에 자체 종료하고, 없으면 timeout이 종료):
```bash
MSYS_NO_PATHCONV=1 timeout 30 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[AutoFire\]"
```
기대 로그 (0.25s 간격 반복):
```
[AutoFire] hit boss, applied=10.0
[AutoFire] hit boss, applied=10.0
...
```
판정:
- `hit boss, applied=10.0` 반복 관측 → 타이머·조준·트레이스·TakeDamage 전달 정상. **게이트 통과.**
- `miss (blocked=...)`만 반복 → 트레이스 시작 높이/장애물 문제. Start의 Z 오프셋(50) 조정 후 Task 1 Step 3~4 재실행(수정 커밋은 `fix(M2): adjust autofire trace height (#26)`).
- `[AutoFire]` 로그 전무 → BeginPlay 타이머 미가동 의심. `HasAuthority` 경로·컴포넌트 부착(Task 2) 재확인.

주의: `applied` 값은 엔진 기본 `AActor::TakeDamage` 리턴(=DamageAmount 그대로). **보스 HP 감소 확인은 #28 검증 몫** — 여기선 데미지 "전달"까지만.

- [ ] **Step 2: (실 PIE, 선택) 디버그 라인 육안 확인**

에디터 PIE에서 플레이어→보스 빨간 트레이스 라인 0.25s 간격 관측. headless(-nullrhi)에선 미검증 — 렌더 경로라 육안만.

- [ ] **Step 3: 커밋 (코드 변경 발생 시에만)**

Step 1 판정에서 튜닝/수정이 없었으면 커밋 없음 — Task 3은 관측 게이트.

---

## 완료 기준
- Task 0~2 커밋 완료 (문서 1 + 코드 2).
- 최종 빌드 `Result: Succeeded`, 에러 0.
- headless에서 `[AutoFire] hit boss, applied=10.0` 반복 관측.
- 스코프 밖 미변경 확인: `git diff dev --stat`에 `Abilities/`·`REPlayerController` 없음, `RECharacterBase` 변경 = 부착 코드만.

## PR
- base=`dev`, 이슈 #26 메타(label C++, milestone M2, assignee, project) 미러링.
- 본문: 목적/변경/검증(프로브 로그 캡처)/스코프 경계(보스 HP #28·피격 #27·HP바 #29).
