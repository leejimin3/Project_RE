# 스탯 노출 — DeveloperSettings Implementation Plan (M3.5 ③)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 플레이어/보스 스탯 하드코딩을 `UREStatsSettings`(UDeveloperSettings + DefaultGame.ini)로 이관 — 리빌드 없이 조절. 스펙: `docs/superpowers/specs/2026-07-17-stats-devsettings-design.md`.

**Architecture:** `Config=Game, defaultconfig` 설정 클래스 1개. 소비처는 생성자/호출 시점에 `GetDefault<UREStatsSettings>()` 조회. `REBulletPattern`의 constexpr 2개는 함수로 교체(시그니처만 변경, 호출부 3곳). 기본값 = 현재 하드코딩과 동일 — ini 미변경 시 회귀 없음.

**Tech Stack:** UE 5.8 C++, UDeveloperSettings, GConfig(DefaultGame.ini).

## Global Constraints

- **선행: M3.5 ①(좌클릭 공격)과 ②(애니메이션)가 dev에 머지되어 있어야 한다** — `UREAttackComponent` 최종 형태 기준으로 이관 (같은 파일 충돌 방지).
- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 검증 게이트 = **빌드 성공(에러 0)** + **headless 로그: 기본값 확인 → ini 변경 반영 확인 → ini 원복**.
- 스탯 8종·기본값 (스펙 확정): PlayerMaxHealth 100 / AttackDamage 10 / AttackInterval 0.25 / AttackRange 2000 / BossMaxHealth 100 / BulletSpeed 300 / BulletLifetime 3 / BossFireInterval 0.1.
- `re.Bullets.Count`·`re.Bullets.SpawnKi` CVar **존치** (프로파일링 즉석 노브 — 역할 분리, 스펙 확정).
- 한글 주석 스타일 유지. 커밋: Conventional Commits, 태스크당 1커밋, 푸터 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- 빌드 명령:
  ```
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0.
- GitHub 이슈를 만들었으면 커밋 제목 끝에 `(#N)` 미러링.

## 브랜치

`dev`에서 분기 (①·② 머지 후): `feature/M3.5-stats`.

## 파일 구조

| 파일 | 책임 |
|---|---|
| `Source/Project_RE/Core/REStatsSettings.h/.cpp` (신규) | 스탯 8종 정의 + ini 바인딩 (단일 출처) |
| `Source/Project_RE/Project_RE.Build.cs` (수정) | `DeveloperSettings` 모듈 추가 |
| `Source/Project_RE/Mass/REBulletPatternGenerator.h/.cpp` (수정) | constexpr → Settings 조회 함수, 구조체 기본값 ctor 이관 |
| `Source/Project_RE/Core/REBossCharacter.cpp` (수정) | 역산 공식 호출부 함수화 + BossMaxHealth |
| `Source/Project_RE/Core/REGameMode.cpp` (수정) | 데모 발사 타이머 주기 함수화 + `[Stats]` 로드 확인 로그 |
| `Source/Project_RE/Core/RECharacterBase.cpp` (수정) | PlayerMaxHealth |
| `Source/Project_RE/Core/REAttackComponent.h/.cpp` (수정) | 공격 스탯 3종 Settings 이관 |
| `Config/DefaultGame.ini` (수정) | 명시적 기본값 섹션 (에디터 없이 직접 편집 가능하게) |

---

### Task 0: 브랜치 생성

- [ ] **Step 1: 브랜치 생성**

```bash
git checkout dev && git pull && git checkout -b feature/M3.5-stats
```
기대: `Switched to a new branch 'feature/M3.5-stats'`

---

### Task 1: UREStatsSettings 클래스 + Build.cs + ini 섹션

**Files:**
- Create: `Source/Project_RE/Core/REStatsSettings.h`
- Create: `Source/Project_RE/Core/REStatsSettings.cpp`
- Modify: `Source/Project_RE/Project_RE.Build.cs`
- Modify: `Config/DefaultGame.ini`

**Interfaces:**
- Consumes: `UDeveloperSettings` (엔진 `DeveloperSettings` 모듈).
- Produces: `UREStatsSettings` — Task 2~4가 `GetDefault<UREStatsSettings>()`로 소비. 프로퍼티명: `PlayerMaxHealth, AttackDamage, AttackInterval, AttackRange, BossMaxHealth, BulletSpeed, BulletLifetime, BossFireInterval` (전부 float).

- [ ] **Step 1: Build.cs 모듈 추가**

`Project_RE.Build.cs`의 `PublicDependencyModuleNames`에 `"NavigationSystem"` 뒤 추가:
```csharp
			"NavigationSystem",
			"DeveloperSettings"
```

- [ ] **Step 2: REStatsSettings.h 작성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "REStatsSettings.generated.h"

/**
 *  플레이어/보스 스탯 단일 출처 (M3.5 ③).
 *  에디터 Project Settings > Game > RE Stats 에서 편집 → DefaultGame.ini 저장. 리빌드 불필요.
 *  값 변경은 재시작 시 반영 (소비처가 생성자/스폰 시점에 조회).
 *  기본값 = 이관 전 하드코딩과 동일 (ini 미변경 시 회귀 없음).
 *  탄환 수(re.Bullets.Count)·스폰 게인은 프로파일링 즉석 노브라 CVar 유지 — 여기 없음.
 */
UCLASS(Config = Game, defaultconfig, meta = (DisplayName = "RE Stats"))
class UREStatsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Project Settings 배치 카테고리 — Game 섹션. */
	virtual FName GetCategoryName() const override { return FName("Game"); }

	/** 플레이어 최대 체력. */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float PlayerMaxHealth = 100.f;

	/** 좌클릭 공격 발사당 데미지. */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float AttackDamage = 10.f;

	/** 좌클릭 공격 발사 간격(s). 서버 rate limit + 클라 페이싱 공용. */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float AttackInterval = 0.25f;

	/** 좌클릭 공격 히트스캔 사거리(uu). */
	UPROPERTY(EditAnywhere, Config, Category = "Player")
	float AttackRange = 2000.f;

	/** 보스 최대 체력. */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BossMaxHealth = 100.f;

	/** 보스 탄막 탄속(uu/s). */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BulletSpeed = 300.f;

	/** 보스 탄막 탄 수명(s) = "탄막 지속시간". 클로즈드루프 역산 공식에도 사용. */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BulletLifetime = 3.f;

	/** 보스 탄막 발사 주기(s). */
	UPROPERTY(EditAnywhere, Config, Category = "Boss")
	float BossFireInterval = 0.1f;
};
```

- [ ] **Step 3: REStatsSettings.cpp 작성** (generated 링크용 빈 구현)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REStatsSettings.h"
```

- [ ] **Step 4: DefaultGame.ini에 명시적 섹션 추가** (파일 끝에 append — 에디터 없이 직접 편집하는 진입점)

```ini

[/Script/Project_RE.REStatsSettings]
PlayerMaxHealth=100.0
AttackDamage=10.0
AttackInterval=0.25
AttackRange=2000.0
BossMaxHealth=100.0
BulletSpeed=300.0
BulletLifetime=3.0
BossFireInterval=0.1
```

- [ ] **Step 5: 빌드**

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REStatsSettings.h Source/Project_RE/Core/REStatsSettings.cpp Source/Project_RE/Project_RE.Build.cs Config/DefaultGame.ini
git commit -m "feat(M3.5): UREStatsSettings — 스탯 8종 DeveloperSettings+ini 정의

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: REBulletPattern — constexpr → Settings 조회 함수

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletPatternGenerator.h`
- Modify: `Source/Project_RE/Mass/REBulletPatternGenerator.cpp`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp` (호출부 2줄)
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (호출부 1줄)

**Interfaces:**
- Consumes: `UREStatsSettings` (Task 1).
- Produces: `float REBulletPattern::FireIntervalSec()`, `float REBulletPattern::BulletLifetimeSec()` (구 constexpr 동명 상수 대체). `FSpiralParams()`/`FFanParams()` 기본 생성자가 Speed/Lifetime을 Settings로 초기화.

- [ ] **Step 1: REBulletPatternGenerator.h — 상수·구조체 교체**

기존 블록:
```cpp
	/** 발사 주기(s). GameMode 발사 타이머와 역산 공식의 단일 출처. */
	constexpr float FireIntervalSec   = 0.1f;
	/** 탄 수명(s). FSpiralParams::Lifetime 기본값과 역산 공식의 단일 출처. */
	constexpr float BulletLifetimeSec = 3.f;
```
를 다음으로 교체:
```cpp
	/** 발사 주기(s). Settings(BossFireInterval) 단일 출처 — GameMode 타이머·역산 공식 공용. */
	float FireIntervalSec();
	/** 탄 수명(s). Settings(BulletLifetime) 단일 출처. */
	float BulletLifetimeSec();
```

`FSpiralParams` 교체:
```cpp
	struct FSpiralParams
	{
		FSpiralParams();             // Speed/Lifetime을 Settings에서 초기화
		int32 Count        = 16;
		float BaseAngleDeg = 0.f;    // 이번 발사 시작각 (Boss가 누적해 전달)
		float AngleStepDeg = 22.5f;  // 탄 간 각 간격 (기본 360/16 = 균등 링)
		float Speed        = 300.f;  // uu/s — 생성자가 Settings로 덮어씀
		float Lifetime     = 3.f;    // s — 생성자가 Settings로 덮어씀
	};
```

`FFanParams` 교체:
```cpp
	struct FFanParams
	{
		FFanParams();                // Speed/Lifetime을 Settings에서 초기화
		int32 Count          = 16;
		float CenterAngleDeg = 0.f;  // 부채꼴 중심 방향
		float SpreadDeg      = 90.f; // 전체 벌어짐 각
		float Speed          = 300.f;
		float Lifetime       = 3.f;
	};
```
(헤더 상단 주석의 "엔진/액터 의존 없는 순수 함수" 문구에 추가: "Settings(CDO) 조회만 예외 — 월드 불필요, headless 검증 유지.")

- [ ] **Step 2: REBulletPatternGenerator.cpp — 구현 추가**

include 추가:
```cpp
#include "REStatsSettings.h"
```

`namespace REBulletPattern` 블록 안, 기존 함수들 위에 추가:
```cpp
	float FireIntervalSec()
	{
		return GetDefault<UREStatsSettings>()->BossFireInterval;
	}

	float BulletLifetimeSec()
	{
		return GetDefault<UREStatsSettings>()->BulletLifetime;
	}

	FSpiralParams::FSpiralParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}

	FFanParams::FFanParams()
	{
		const UREStatsSettings* S = GetDefault<UREStatsSettings>();
		Speed    = S->BulletSpeed;
		Lifetime = S->BulletLifetime;
	}
```

- [ ] **Step 3: 호출부 함수화 (3곳)**

`Source/Project_RE/Core/REBossCharacter.cpp`:
```cpp
		const float FeedFwd = TargetLive * REBulletPattern::FireIntervalSec() / REBulletPattern::BulletLifetimeSec();
```
```cpp
			const int32 FillShots = FMath::CeilToInt(REBulletPattern::BulletLifetimeSec() / REBulletPattern::FireIntervalSec());
```

`Source/Project_RE/Core/REGameMode.cpp` (데모 발사 타이머):
```cpp
		GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, REBulletPattern::FireIntervalSec(), /*bLoop=*/true);
```

- [ ] **Step 4: 빌드**

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0. (constexpr 잔존 참조 있으면 컴파일 에러로 드러남 — 전량 함수화 확인.)

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletPatternGenerator.cpp Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M3.5): 탄막 주기/수명/탄속을 Settings 조회로 이관

constexpr 2종 → 함수. FSpiralParams/FFanParams 기본값도 Settings ctor로.
탄막 지속시간(BulletLifetime) ini 조절이 역산 공식까지 일관 반영.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: 캐릭터 HP + 공격 스탯 이관

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp`
- Modify: `Source/Project_RE/Core/REAttackComponent.h`
- Modify: `Source/Project_RE/Core/REAttackComponent.cpp`

**Interfaces:**
- Consumes: `UREStatsSettings` (Task 1).
- Produces: 없음 (기존 멤버 초기화 경로만 변경 — 외부 시그니처 불변, `GetAttackInterval()` 유지).

- [ ] **Step 1: RECharacterBase.cpp — PlayerMaxHealth**

include 추가:
```cpp
#include "REStatsSettings.h"
```

생성자의 기존:
```cpp
	// 체력 초기화 — MaxHealth 조정 시 정합 유지
	Health = MaxHealth;
```
를 다음으로 교체:
```cpp
	// 체력 초기화 — Settings 단일 출처 (M3.5 ③).
	MaxHealth = GetDefault<UREStatsSettings>()->PlayerMaxHealth;
	Health = MaxHealth;
```

- [ ] **Step 2: REBossCharacter.cpp — BossMaxHealth**

include 추가:
```cpp
#include "REStatsSettings.h"
```

생성자의 기존:
```cpp
	// 체력 초기화 — MaxHealth 조정 시 정합 유지 (RECharacterBase 동일 패턴)
	Health = MaxHealth;
```
를 다음으로 교체:
```cpp
	// 체력 초기화 — Settings 단일 출처 (M3.5 ③, RECharacterBase 동일 패턴).
	MaxHealth = GetDefault<UREStatsSettings>()->BossMaxHealth;
	Health = MaxHealth;
```

- [ ] **Step 3: REAttackComponent — 공격 스탯 3종 이관**

`REAttackComponent.h`의 스탯 멤버 3개 블록을 다음으로 교체 (UPROPERTY 제거 — Settings가 단일 출처, 에디터 편집 진입점은 Project Settings):
```cpp
	/** 발사당 데미지. Settings(AttackDamage) 단일 출처 — 생성자에서 로드. */
	float Damage = 10.f;

	/** 발사 간격(s). Settings(AttackInterval) 단일 출처. 서버 rate limit + 클라 페이싱 공용. */
	float AttackInterval = 0.25f;

	/** 히트스캔 사거리(uu). Settings(AttackRange) 단일 출처. */
	float AttackRange = 2000.f;
```

`REAttackComponent.cpp` include 추가:
```cpp
#include "REStatsSettings.h"
```

생성자에 추가:
```cpp
	// 공격 스탯 — Settings 단일 출처 (M3.5 ③).
	const UREStatsSettings* Stats = GetDefault<UREStatsSettings>();
	Damage         = Stats->AttackDamage;
	AttackInterval = Stats->AttackInterval;
	AttackRange    = Stats->AttackRange;
```

- [ ] **Step 4: 빌드**

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REAttackComponent.h Source/Project_RE/Core/REAttackComponent.cpp
git commit -m "feat(M3.5): HP·공격 스탯을 Settings 조회로 이관

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: `[Stats]` 로드 확인 로그 + ini 반영 게이트

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (로그 1블록 — 커밋 대상, 상시 프로브)

**Interfaces:**
- Consumes: `UREStatsSettings` (Task 1), `[RE] Boss Spiral` 로그(기존).
- Produces: `[Stats]` 로그 — 게이트 판정용.

- [ ] **Step 1: REGameMode.cpp — BeginPlay 로드 확인 로그**

include 추가:
```cpp
#include "REStatsSettings.h"
```

`BeginPlay`의 Mass 스모크 테스트 블록 앞에 추가:
```cpp
	// M3.5 ③: 스탯 로드 확인 — ini 반영 검증 프로브 (재시작 반영 원칙의 관측점).
	{
		const UREStatsSettings* Stats = GetDefault<UREStatsSettings>();
		UE_LOG(LogTemp, Log, TEXT("[Stats] Dmg=%.1f AtkInt=%.2f Range=%.0f PHP=%.0f BHP=%.0f BSpd=%.0f BLife=%.1f BInt=%.2f"),
			Stats->AttackDamage, Stats->AttackInterval, Stats->AttackRange, Stats->PlayerMaxHealth,
			Stats->BossMaxHealth, Stats->BulletSpeed, Stats->BulletLifetime, Stats->BossFireInterval);
	}
```

- [ ] **Step 2: 빌드**

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 3: 게이트 A — 기본값 회귀 없음**

```bash
MSYS_NO_PATHCONV=1 timeout 30 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[Stats\]|Boss Spiral|\[Attack\] hit"
```
기대:
```
[Stats] Dmg=10.0 AtkInt=0.25 Range=2000 PHP=100 BHP=100 BSpd=300 BLife=3.0 BInt=0.10
[RE] Boss Spiral: Target=480 Live=0 Rate=16.0 -> N=16      ← 첫 발사, FeedFwd 480×0.1/3=16 (기존 동일)
[Attack] hit boss, applied=10.0
```

- [ ] **Step 4: 게이트 B — ini 변경 반영** (BulletLifetime 3→1)

`Config/DefaultGame.ini`에서 `BulletLifetime=3.0` → `BulletLifetime=1.0`으로 임시 수정 후 동일 명령 재실행.

기대:
```
[Stats] ... BLife=1.0 ...
[RE] Boss Spiral: Target=480 Live=0 Rate=48.0 -> N=48      ← FeedFwd 480×0.1/1=48 — 역산 공식까지 반영 증거
```
판정: `BLife=1.0` + `Rate=48.0` 관측 → ini→Settings→역산 공식 전 경로 반영. **게이트 통과.**
(스펙의 "발사 정지 후 lifetime+ε 잔탄 0" 검증은 headless 로그로 관측점이 없어 FeedFwd 반영으로 대체 — 수명 소멸 자체는 SimProcessor 기존 검증(#13) 커버.)

- [ ] **Step 5: ini 원복**

`BulletLifetime=1.0` → `BulletLifetime=3.0` 원복 후:
```bash
git diff Config/DefaultGame.ini
```
기대: Task 1에서 추가한 섹션 그대로 (임시 변경 흔적 없음).

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M3.5): 스탯 로드 확인 [Stats] 로그 프로브

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## 완료 기준
- Task 1~4 커밋 완료 (4커밋).
- 최종 빌드 `Result: Succeeded`, 에러 0.
- 게이트 A: 기본값에서 기존 동작 동일 (`Rate=16.0`, `applied=10.0`).
- 게이트 B: ini 변경이 `[Stats]` + `Rate=48.0`으로 반영.
- ini 원복 완료, `grep -rn "constexpr float" Source/Project_RE/Mass/REBulletPatternGenerator.h` → 0건.
- 프로파일링 무회귀: `re.Bullets.Count`/`SpawnKi` CVar 코드 미변경 (`git diff dev -- Source/Project_RE/Core/REBossCharacter.cpp`에 CVar 정의부 변경 없음).

## PR
- base=`dev`, 제목 `feat(M3.5): 스탯 노출 — UREStatsSettings + DefaultGame.ini`.
- 이슈를 만들었으면 메타 미러링.
- 본문: 목적/변경/검증(게이트 A·B 로그)/스코프 경계(CVar 존치 근거, 런타임 핫리로드 미지원 — 재시작 반영).
