# 구현 목표: [M2 #26] — 플레이어 자동사격 (라인트레이스, 서버권위)

## 컨텍스트
UE 5.8 C++ 탑뷰 탄막 프로젝트 (`E:\UnrealProjects\Project_RE`). 이슈 **#26**, 마일스톤 **M2: 플레이어 게임루프**.
이 goal: 신규 `UREAutoFireComponent`가 서버 전용 타이머로 최근접 `AREBossCharacter`를 라인트레이스 조준·주기 발사, 히트 시 `AActor::TakeDamage` 호출. 입력 없음 → RPC 없음.
**스코프 밖 후속 이슈(손대지 말 것): #27 탄막 피격판정, #28 보스 HP+TakeDamage override, #29 HP바.**
설계 스펙: docs/superpowers/specs/2026-07-13-autofire-linetrace-design.md
상세 플랜: docs/superpowers/plans/2026-07-13-autofire-linetrace.md
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치
`dev`에서 분기: `feature/M2-autofire`. **#26은 #25와 코드 독립** (신규 파일 2개 + RECharacterBase 부착 3줄, 앵커는 #23에 이미 존재). 단 워킹트리를 #25 세션이 쓰는 중이면 checkout 금지 — 경로 택일:

**A. #25 머지 후 같은 트리에서** (#25 세션 종료 후):
```bash
git checkout dev && git pull && git checkout -b feature/M2-autofire
```
**B. 지금 병렬 — git worktree** (별도 디렉토리, #25 무간섭. 비용: UE 풀 빌드 1회):
```bash
git worktree add ../Project_RE-autofire -b feature/M2-autofire dev
```
이후 모든 작업(빌드 -Project 경로 포함)을 `E:\UnrealProjects\Project_RE-autofire` 기준으로 수행.
(브랜치 이미 존재하면 체크아웃만. 나중 dev 머지 시 RECharacterBase hunk 겹침은 trivial 충돌 — 부착 3줄 유지로 해소.)

## 전역 제약
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

## 검증된 API (실물 확인됨)
- `AREBossCharacter` — `Source/Project_RE/Core/REBossCharacter.h`. `ACharacter` 직상속, HP 없음(#28 몫). 캡슐 기본 Pawn 프로파일 → `ECC_Pawn` 트레이스에 블로킹.
- `AActor::TakeDamage(float, const FDamageEvent&, AController*, AActor*)` → `float` — 엔진 기본 구현 존재(모든 액터). 보스는 override 없음 → 리턴 = DamageAmount 그대로. **이 진입점 호출까지가 #26 계약.**
- `FDamageEvent` — `Engine/DamageEvents.h` (UE5에서 분리된 헤더 — `GameFramework/Actor.h`만으론 불완전 타입).
- `TActorIterator<T>` — `EngineUtils.h`.
- `AREGameMode::BeginPlay`(`Source/Project_RE/Core/REGameMode.cpp`)가 DemoBoss를 `(0,0,90)`에 스폰 → headless 실행 시 자동사격 자연 발동, 별도 셋업 불필요.
- headless 실행: `MSYS_NO_PATHCONV=1` 필수 (Git Bash가 `/Game/...` 맵 경로를 윈도 경로로 변형하는 것 방지).

## 기존 파일 현황 (변경 대상)
- `Source/Project_RE/Core/RECharacterBase.h` — `ARECharacterBase : ACharacter, IAbilitySystemInterface`. 전방선언 블록에 `USpringArmComponent/UCameraComponent/UAbilitySystemComponent/UREGA_Dash` 있음. `protected:`에 `AbilitySystemComponent` UPROPERTY, `Health/MaxHealth`, 카메라 멤버.
- `Source/Project_RE/Core/RECharacterBase.cpp` — 생성자에서 `AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"))` 블록 존재(삽입 앵커). `TakeDamage` HasAuthority 가드 구현 존재 — **변경 금지**.
- **주의: #25가 이 파일들을 수정했음. 라인 번호가 아닌 앵커 텍스트 기준으로 삽입 위치를 찾을 것.**

================================================================
## TASK 0: 브랜치 생성 + 설계 문서 커밋
================================================================
### 0-1. 브랜치
위 "브랜치" 섹션의 A 또는 B 경로 수행.
- A(같은 트리): untracked 문서는 브랜치 전환에 따라옴.
- B(worktree): untracked 문서 3개(spec/plan/goal)는 원본 트리에만 있음 → 커밋 전 복사:
```bash
mkdir -p ../Project_RE-autofire/docs/superpowers/{specs,plans,goals}
cp docs/superpowers/specs/2026-07-13-autofire-linetrace-design.md ../Project_RE-autofire/docs/superpowers/specs/
cp docs/superpowers/plans/2026-07-13-autofire-linetrace.md ../Project_RE-autofire/docs/superpowers/plans/
cp docs/superpowers/goals/2026-07-13-autofire-linetrace-goal.md ../Project_RE-autofire/docs/superpowers/goals/
```

### 0-2. 커밋
```bash
git add docs/superpowers/specs/2026-07-13-autofire-linetrace-design.md docs/superpowers/plans/2026-07-13-autofire-linetrace.md docs/superpowers/goals/2026-07-13-autofire-linetrace-goal.md
git commit -m "docs(M2): autofire design spec + implementation plan (#26)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 1: REAutoFireComponent
================================================================
### 1-1. `Source/Project_RE/Core/REAutoFireComponent.h` (신규)
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

### 1-2. `Source/Project_RE/Core/REAutoFireComponent.cpp` (신규)
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

### 1-3. 빌드 게이트
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0. (이 시점엔 컴포넌트가 어디에도 부착 안 됨 — 컴파일만 확인.)

### 1-4. 커밋
```bash
git add Source/Project_RE/Core/REAutoFireComponent.h Source/Project_RE/Core/REAutoFireComponent.cpp
git commit -m "feat(M2): add REAutoFireComponent server-authoritative linetrace fire (#26)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 2: 플레이어 캐릭터에 부착
================================================================
> **주의:** #25가 `RECharacterBase.h/.cpp`를 수정했다. 라인 번호가 아닌 **앵커 텍스트 기준**으로 삽입. 부착 외 어떤 것도 변경 금지 (Surgical).

### 2-1. `Source/Project_RE/Core/RECharacterBase.h` (수정)
전방선언 블록(`class UAbilitySystemComponent;` 부근)에 추가:
```cpp
class UREAutoFireComponent;
```
`protected:` 섹션의 `AbilitySystemComponent` UPROPERTY 선언 **뒤에** 추가:
```cpp
	/** 자동사격 컴포넌트 (#26). 서버에서만 구동. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoFire", meta = (AllowPrivateAccess = "true"))
	UREAutoFireComponent* AutoFireComponent;
```

### 2-2. `Source/Project_RE/Core/RECharacterBase.cpp` (수정)
include 블록의 `#include "AbilitySystemComponent.h"` **뒤에** 추가:
```cpp
#include "REAutoFireComponent.h"
```
생성자의 ASC 생성부(`AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(...)` 블록) **뒤에** 추가:
```cpp
	// 자동사격 부착 — 컴포넌트가 BeginPlay에서 서버 여부를 스스로 판단.
	AutoFireComponent = CreateDefaultSubobject<UREAutoFireComponent>(TEXT("AutoFire"));
```

### 2-3. 빌드/검증 게이트
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

정적 확인:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "AutoFire" Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp
```
기대: 헤더 = 전방선언+멤버+카테고리, cpp = include+생성 — 그 외 파일 변경 없음.

### 2-4. 커밋
```bash
git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp
git commit -m "feat(M2): attach AutoFireComponent to player character (#26)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 3: Headless 프로브 로그 관측 (신규 코드 없음)
================================================================
### 3-1. headless 실행 + 로그 관측
Task 1의 `[AutoFire]` 로그가 곧 프로브. `REGameMode::BeginPlay`가 DemoBoss를 (0,0,90)에 스폰 → 자동 발동.

Run (dev에 #25 대쉬 프로브가 있으면 ~4초에 자체 종료, 없으면 timeout이 종료):
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
- `miss (blocked=...)`만 반복 → 트레이스 시작 높이/장애물 문제. Start의 Z 오프셋(50) 조정 후 TASK 1 게이트 재실행 (수정 커밋: `fix(M2): adjust autofire trace height (#26)`).
- `[AutoFire]` 로그 전무 → BeginPlay 타이머 미가동 의심. `HasAuthority` 경로·컴포넌트 부착(TASK 2) 재확인.

주의: `applied` 값은 엔진 기본 `AActor::TakeDamage` 리턴(=DamageAmount 그대로). **보스 HP 감소 확인은 #28 검증 몫** — 여기선 데미지 "전달"까지만.

### 3-2. (실 PIE, 선택) 디버그 라인 육안 확인
에디터 PIE에서 플레이어→보스 빨간 트레이스 라인 0.25s 간격 관측. headless(-nullrhi)에선 미검증 — 렌더 경로라 육안만.

### 3-3. 커밋
코드 변경 발생 시에만 (튜닝 fix 커밋). 변경 없으면 커밋 없음 — TASK 3은 관측 게이트.

## 완료 후
- PR: base=`dev`, head=`feature/M2-autofire`.
- 이슈 #26 메타 미러링: label `C++`, milestone `M2: 플레이어 게임루프`, assignee `leejimin3`, project 동일.
- PR 본문 6개 필드 전부: 목적/변경/검증(프로브 로그 캡처)/스코프 경계(보스 HP #28·피격 #27·HP바 #29)/기타 규약 필드.
- 남은 의도된 마커: 없음 (컴포넌트에 TODO 없음 — 후속은 이슈로 추적).

## 하지 말 것 (스코프 밖)
- 보스 `Health`/`TakeDamage` override → **#28**. 이번엔 엔진 기본 `TakeDamage` 호출까지만.
- 탄막 피격 판정 → **#27**. HP바 → **#29**.
- 발사 이펙트/사운드/애니메이션 → 스코프 밖.
- `Abilities/`, `REPlayerController.h/.cpp` → **일절 미접촉** (#25 작업 영역).
- 사거리 제한/타겟 우선순위 로직 → 없음 (최근접 단일 기준만).
- 기존 이동/HP/카메라/GAS 셋업 → 변경 금지 (Surgical).
