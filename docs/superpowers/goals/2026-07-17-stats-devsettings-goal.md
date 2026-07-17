# 구현 목표: M3.5 ③ — 스탯 노출 (UREStatsSettings + DefaultGame.ini)

## 컨텍스트
UE 5.8 C++ 탑뷰 탄막(bullet-hell) 프로젝트 `Project_RE`. M4 데디 서버 전 중간점검 3건 중 세 번째(마지막).
이 goal: 플레이어/보스 스탯 하드코딩 8종을 `UREStatsSettings`(UDeveloperSettings, Config=Game)로 이관 — 에디터 Project Settings/DefaultGame.ini에서 리빌드 없이 조절. "보스 탄막 지속시간" 조절이 BulletLifetime으로 달성됨(클로즈드루프 역산 공식까지 일관 반영).
**선행: M3.5 ①(좌클릭 공격)과 ②(애니메이션)가 dev에 머지되어 있어야 한다** — `UREAttackComponent` 최종 형태 기준 이관 (같은 파일 충돌 방지).
스코프 밖(손대지 말 것): `re.Bullets.Count`/`re.Bullets.SpawnKi` CVar 변경, 런타임 핫리로드.
설계 스펙: docs/superpowers/specs/2026-07-17-stats-devsettings-design.md
상세 플랜: docs/superpowers/plans/2026-07-17-stats-devsettings.md
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치
`dev`에서 분기 (①·② 머지 후): `feature/M3.5-stats`

## 전역 제약
- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 검증 게이트 = **빌드 성공(에러 0)** + **headless 로그: 기본값 확인(게이트 A) → ini 변경 반영 확인(게이트 B) → ini 원복**.
- 스탯 8종·기본값 (스펙 확정): PlayerMaxHealth 100 / AttackDamage 10 / AttackInterval 0.25 / AttackRange 2000 / BossMaxHealth 100 / BulletSpeed 300 / BulletLifetime 3 / BossFireInterval 0.1. 기본값 = 현재 하드코딩과 동일 — ini 미변경 시 회귀 없음.
- `re.Bullets.Count`·`re.Bullets.SpawnKi` CVar **존치** (프로파일링 즉석 노브 — 역할 분리, 스펙 확정).
- 한글 주석 스타일 유지. 커밋: Conventional Commits, 태스크당 1커밋, 푸터 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- 빌드 명령:
  ```
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0.
- GitHub 이슈 미생성 상태 — 커밋 제목에 `(#N)` 생략. 이슈가 생기면 미러링.

## 검증된 API (실물 확인됨)
- `UDeveloperSettings` — 엔진 `DeveloperSettings` 모듈, `Engine/DeveloperSettings.h`. `Config=Game, defaultconfig` UCLASS 지정자 + `GetCategoryName()` override 패턴.
- `GetDefault<T>()` — CDO 조회, config 적용된 값. 생성자/런타임 어디서나 호출 가능.
- `REBulletPattern::FireIntervalSec`/`BulletLifetimeSec` 사용처 실측 3곳: `REBossCharacter.cpp`(FeedFwd 계산 1곳 + FillShots 1곳), `REGameMode.cpp`(DemoFireTimer SetTimer 1곳).
- `FSpiralParams{}`/`FFanParams{}` 기본 생성 사용처: `REGameMode.cpp` SpiralProbe/FanProbe(단위 검증), `REBossCharacter.cpp` Fan 케이스. `MakeSpiralRing`은 `FSpiralParams P;` 기본 생성 후 필드 세팅.
- `Config/DefaultGame.ini` 실존. 섹션명 규칙 `[/Script/<모듈명>.<클래스명(U 제외)>]` → `[/Script/Project_RE.REStatsSettings]`.
- `[RE] Boss Spiral: Target=%d Live=%d Rate=%.1f -> N=%d` 로그 — `REBossCharacter.cpp` 실물 (게이트 B 관측점).

## 기존 파일 현황 (변경 대상)
- `Source/Project_RE/Project_RE.Build.cs`: `PublicDependencyModuleNames` 마지막 항목 `"NavigationSystem"`. `DeveloperSettings` 없음.
- `Source/Project_RE/Mass/REBulletPatternGenerator.h`: `namespace REBulletPattern` — `constexpr float FireIntervalSec = 0.1f;`, `constexpr float BulletLifetimeSec = 3.f;`, `FSpiralParams`(Lifetime 기본값이 `BulletLifetimeSec` 참조)/`FFanParams` 구조체, `GenerateSpiral/GenerateFan/MakeSpiralRing` 선언.
- `Source/Project_RE/Core/RECharacterBase.cpp`: 생성자 `Health = MaxHealth;` (MaxHealth UPROPERTY 기본 100).
- `Source/Project_RE/Core/REBossCharacter.cpp`: 생성자 동일 패턴 + TriggerBulletPattern의 FeedFwd/FillShots 계산.
- `Source/Project_RE/Core/REAttackComponent.h/.cpp` (①·② 산출물): `Damage/AttackInterval/AttackRange` UPROPERTY EditDefaultsOnly + `FireMontage` 멤버(② 추가). 생성자에 몽타주 로드 블록 존재.
- `Source/Project_RE/Core/REGameMode.cpp`: `BeginPlay` 시작부 Mass 스모크 테스트 블록. DemoFireTimer가 `REBulletPattern::FireIntervalSec` 사용.

================================================================
## TASK 1: UREStatsSettings 클래스 + Build.cs + ini 섹션
================================================================

### 1-1. Source/Project_RE/Project_RE.Build.cs (수정)

`PublicDependencyModuleNames`의 `"NavigationSystem"` 뒤에 추가:
```csharp
			"NavigationSystem",
			"DeveloperSettings"
```

### 1-2. Source/Project_RE/Core/REStatsSettings.h (신규)

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

### 1-3. Source/Project_RE/Core/REStatsSettings.cpp (신규 — generated 링크용)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REStatsSettings.h"
```

### 1-4. Config/DefaultGame.ini (수정 — 파일 끝에 append)

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

### 1-5. 빌드 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-6. 커밋

```bash
git add Source/Project_RE/Core/REStatsSettings.h Source/Project_RE/Core/REStatsSettings.cpp Source/Project_RE/Project_RE.Build.cs Config/DefaultGame.ini
git commit -m "feat(M3.5): UREStatsSettings — 스탯 8종 DeveloperSettings+ini 정의

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 2: REBulletPattern — constexpr → Settings 조회 함수
================================================================

### 2-1. Source/Project_RE/Mass/REBulletPatternGenerator.h (수정)

기존:
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

`FSpiralParams` 전체 교체:
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

`FFanParams` 전체 교체:
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

헤더 상단 네임스페이스 주석의 "엔진/액터 의존 없는 순수 함수" 문구에 추가: "Settings(CDO) 조회만 예외 — 월드 불필요, headless 검증 유지."

### 2-2. Source/Project_RE/Mass/REBulletPatternGenerator.cpp (수정)

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

### 2-3. 호출부 함수화 (3곳)

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

### 2-4. 빌드 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0. (constexpr 잔존 참조 있으면 컴파일 에러로 드러남 — 전량 함수화 확인.)

### 2-5. 커밋

```bash
git add Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletPatternGenerator.cpp Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M3.5): 탄막 주기/수명/탄속을 Settings 조회로 이관

constexpr 2종 → 함수. FSpiralParams/FFanParams 기본값도 Settings ctor로.
탄막 지속시간(BulletLifetime) ini 조절이 역산 공식까지 일관 반영.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 3: 캐릭터 HP + 공격 스탯 이관
================================================================

### 3-1. Source/Project_RE/Core/RECharacterBase.cpp (수정)

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

### 3-2. Source/Project_RE/Core/REBossCharacter.cpp (수정)

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

### 3-3. Source/Project_RE/Core/REAttackComponent.h (수정)

스탯 멤버 3개(UPROPERTY 블록 포함)를 다음으로 교체 (UPROPERTY 제거 — Settings가 단일 출처, 에디터 편집 진입점은 Project Settings):
```cpp
	/** 발사당 데미지. Settings(AttackDamage) 단일 출처 — 생성자에서 로드. */
	float Damage = 10.f;

	/** 발사 간격(s). Settings(AttackInterval) 단일 출처. 서버 rate limit + 클라 페이싱 공용. */
	float AttackInterval = 0.25f;

	/** 히트스캔 사거리(uu). Settings(AttackRange) 단일 출처. */
	float AttackRange = 2000.f;
```
(`FireMontage`·`LastFireTime` 멤버는 그대로 유지.)

### 3-4. Source/Project_RE/Core/REAttackComponent.cpp (수정)

include 추가:
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

### 3-5. 빌드 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 3-6. 커밋

```bash
git add Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REAttackComponent.h Source/Project_RE/Core/REAttackComponent.cpp
git commit -m "feat(M3.5): HP·공격 스탯을 Settings 조회로 이관

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 4: [Stats] 로드 확인 로그 + ini 반영 게이트
================================================================

### 4-1. Source/Project_RE/Core/REGameMode.cpp (수정 — 커밋 대상, 상시 프로브)

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

### 4-2. 빌드 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 4-3. 게이트 A — 기본값 회귀 없음

```bash
MSYS_NO_PATHCONV=1 timeout 30 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[Stats\]|Boss Spiral|\[Attack\] hit"
```
기대:
```
[Stats] Dmg=10.0 AtkInt=0.25 Range=2000 PHP=100 BHP=100 BSpd=300 BLife=3.0 BInt=0.10
[RE] Boss Spiral: Target=480 Live=0 Rate=16.0 -> N=16
[Attack] hit boss, applied=10.0
```
(첫 발사 FeedFwd = 480×0.1/3 = 16 — 기존 동일 = 회귀 없음.)

### 4-4. 게이트 B — ini 변경 반영 (BulletLifetime 3→1)

`Config/DefaultGame.ini`에서 `BulletLifetime=3.0` → `BulletLifetime=1.0` 임시 수정 후 4-3 동일 명령 재실행.

기대:
```
[Stats] ... BLife=1.0 ...
[RE] Boss Spiral: Target=480 Live=0 Rate=48.0 -> N=48
```
판정: `BLife=1.0` + `Rate=48.0`(FeedFwd 480×0.1/1) 관측 → ini→Settings→역산 공식 전 경로 반영. **게이트 통과.**

### 4-5. ini 원복

`BulletLifetime=1.0` → `BulletLifetime=3.0` 원복 후:
```bash
git diff Config/DefaultGame.ini
```
기대: TASK 1에서 추가한 섹션 그대로 (임시 변경 흔적 없음).

### 4-6. 커밋

```bash
git add Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M3.5): 스탯 로드 확인 [Stats] 로그 프로브

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

## 완료 후
- 최종 확인: `grep -rn "constexpr float" Source/Project_RE/Mass/REBulletPatternGenerator.h` → 0건. `git diff dev -- Source/Project_RE/Core/REBossCharacter.cpp`에 CVar 정의부(`CVarBulletCount`/`CVarSpawnKi`) 변경 없음.
- PR: base=`dev`, 제목 `feat(M3.5): 스탯 노출 — UREStatsSettings + DefaultGame.ini`. 이슈를 만들었으면 메타 미러링. 본문: 목적/변경/검증(게이트 A·B 로그)/스코프 경계(CVar 존치 근거, 재시작 반영). 푸터 `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- 남은 의도된 TODO: 없음.

## 하지 말 것 (스코프 밖)
- `re.Bullets.Count`/`re.Bullets.SpawnKi` CVar 제거·수정 (프로파일링 즉석 노브 — 존치 확정).
- 런타임 핫리로드/CVar 브릿지 (재시작 반영이 스펙).
- `scripts/profile.ps1` 수정.
- 스탯 추가 노출 (8종 확정 — 대쉬 거리/쿨다운 등은 요청 시 후속).
- HP UPROPERTY(`MaxHealth`) 선언 자체 제거 금지 — 초기화 경로만 Settings로 (복제/HP바 로직 무변경).
