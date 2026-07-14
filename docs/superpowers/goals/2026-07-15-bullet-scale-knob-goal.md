# 구현 목표: [M3 #43] 탄막 스케일 노브 — CVar `re.Bullets.Count`

## 컨텍스트

UE 5.8 C++ 탑뷰 탄막 프로젝트. MassEntity로 탄환을 시뮬레이션한다. 이슈 **#43**, 마일스톤 **M3: 스케일 업 + 프로파일링**.

**이 goal이 하는 것:** 콘솔 변수 `re.Bullets.Count N` 하나로 동시 탄환 수를 런타임에 100/1000/5000으로 바꿀 수 있게 한다 (재시작 없이 다음 발사부터 반영). M3 성능 측정 전체의 전제 조건이다.

**지금은 동시 탄환 수를 바꿀 방법이 없다.** 하드코딩 3개(발사당 16발 / 발사주기 0.1s / 수명 3s)의 곱으로 약 480발에 우연히 고정돼 있다. 동시 탄환 = `발사당_탄수 / 발사주기 × 수명` 이므로, 발사주기·수명을 상수로 고정하고 **발사당 탄 수만 역산**한다: `Count = round(N × 0.1 / 3)`.

**스코프 밖 (후속 이슈 — 절대 손대지 말 것):**
- 실제 CPU 시간 측정·프로파일링 → **#46**
- 스폰 성능 최적화 (`REBulletSpawnSubsystem.cpp:49-55`의 per-entity `CreateEntity` 루프) → **#46. 측정 전 최적화 금지.**
- 프로파일링 하네스, `Mass/*Processor.*`, `Config/`, `scripts/` → **#44 소유 (다른 세션이 병렬 작업 중)**
- 신규 `Baseline/` 폴더 (Actor 베이스라인) → **#45 소유**

설계 스펙: `docs/superpowers/specs/2026-07-15-bullet-scale-knob-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-15-bullet-scale-knob.md`
(참고 가능. 단 **아래 코드가 최종 정본.** 어긋나면 이 문서를 따른다.)

## 브랜치

`dev`에서 분기: **`feature/M3-bullet-scale-knob`**
worktree: `E:/UnrealProjects/Project_RE-scale-knob` (이미 생성됨, 여기서 작업)

## 전역 제약

- **파일 소유권 (병렬 작업 — 위반 시 다른 세션과 충돌).** 수정 가능한 파일은 아래 **3쌍뿐**:
  - `Source/Project_RE/Core/REGameMode.h/.cpp`
  - `Source/Project_RE/Core/REBossCharacter.h/.cpp`
  - `Source/Project_RE/Mass/REBulletPatternGenerator.h/.cpp`

  `Mass/REBulletSimProcessor.cpp`, `Mass/REBulletRenderProcessor.cpp`, `Mass/REBulletHitProcessor.cpp`, `Config/`, `scripts/` → **#44 소유. 읽기만 하라. 절대 수정 금지.**
  신규 `Baseline/` 폴더 → **#45 소유. 생성 금지.**
- **자동 테스트 인프라 없음.** 이 프로젝트에 유닛테스트 프레임워크는 없다. 검증 게이트는 **(a) 에디터 빌드 성공, (b) headless 프로브 로그 관측** 두 가지다. **새 테스트 프레임워크를 도입하지 마라.**
- **성능 최적화 금지.** N=5000에서 스폰이 느린 건 이번 이슈의 **측정 대상**이지 수정 대상이 아니다.
- **BeginPlay의 기존 M0/M1 프로브 코드는 미접촉.** (`REGameMode.cpp`의 초기 트리거 2회, SpiralProbe/FanProbe, SimProbe + `Tick()`) — TASK 1의 TEMP 블록만 예외이며 TASK 1에서 반드시 제거한다.
- 커밋: Conventional Commits, 끝에 `(#43)`.

### 빌드 커맨드 (모든 게이트에서 사용)

Git Bash. `MSYS_NO_PATHCONV=1` 없으면 경로가 mangling된다:
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REEditor Win64 Development \
  -Project="E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" \
  -WaitMutex -NoHotReload
```
기대 출력: `Result: Succeeded`, 에러 0.

### 헤드리스 프로브 커맨드 (템플릿)

```bash
cd /e/UnrealProjects/Project_RE-scale-knob
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="<CVAR 명령>" -log=<로그파일명>.log &
sleep <초>
grep -E "<패턴>" "Saved/Logs/<로그파일명>.log" | tail -12
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

## 검증된 API (실물 확인됨 — 추론하지 마라)

| 심볼 | 시그니처 / 값 | 헤더 |
|---|---|---|
| `TAutoConsoleVariable<int32>` | `(TEXT(name), int32 Default, TEXT(help), uint32 Flags)` | `CoreMinimal.h` 경유 가시. 링크 에러 시 `HAL/IConsoleManager.h` 추가 |
| `.GetValueOnGameThread()` | → `int32` | 위와 동일 |
| `ECVF_Cheat` | CVar 플래그 | 위와 동일 |
| `FMath::RoundToInt` | `(float)` → `int32` | `CoreMinimal.h` |
| `FMath::Max` | `(int32, int32)` → `int32` | `CoreMinimal.h` |
| `REBulletPattern::FSpiralParams` | `{ int32 Count; float BaseAngleDeg; float AngleStepDeg; float Speed; float Lifetime; }` | `Mass/REBulletPatternGenerator.h` |
| `REBulletPattern::GenerateSpiral` | `(const FVector& Origin, const FSpiralParams& P)` → `TArray<FBulletSpawnParams>` | 위와 동일 |
| `FTimerManager::SetTimer` | `(FTimerHandle&, FTimerDelegate, float Rate, bool bLoop)` | `TimerManager.h` (이미 include됨) |
| RenderProbe 로그 (관측 대상) | `[RE] RenderProbe: live=%d ISM.Count=%d` — 30틱마다 1회 | `Mass/REBulletRenderProcessor.cpp:71-76` (**#44 소유, 읽기만**) |

`live=%d`는 Mass 쿼리로 센 **실제 엔티티 수**다 (ISM 인스턴스 아님). 그대로 재사용한다 — **새 카운터를 만들지 마라.**

## 기존 파일 현황 (변경 대상)

**`Source/Project_RE/Mass/REBulletPatternGenerator.h`** — `namespace REBulletPattern` 안에 `FSpiralParams`(`Count=16`, `AngleStepDeg=22.5f`, `Speed=300.f`, `Lifetime=3.f`), `FFanParams`, `GenerateSpiral()`, `GenerateFan()` 선언. 엔진/액터 의존 없는 순수 함수 철학 — **유지하라.**

**`Source/Project_RE/Mass/REBulletPatternGenerator.cpp`** — 익명 네임스페이스의 `DirFromDeg(float)`, 그리고 `GenerateSpiral`/`GenerateFan` 구현.

**`Source/Project_RE/Core/REBossCharacter.cpp`** — `TriggerBulletPattern(EBulletPattern, int32 Seed, float StartTime)`. `bIsDead` 가드 → `switch(Pattern)` → `Spiral` 케이스가 `FSpiralParams SP;` 기본값에 `BaseAngleDeg`만 덮어씀 → `SpawnBulletBatch(Params)`. Fan/Homing 케이스도 존재.

**`Source/Project_RE/Core/REGameMode.cpp`** — `BeginPlay`가 Boss 스폰 후 `TriggerBulletPattern` 2회 직접 호출 + `SetTimer(DemoFireTimer, FireDel, 0.1f, true)` 루프. `REBulletPatternGenerator.h`를 **이미 include 중**(`:12`). 그 외 M0/M1 프로브 블록들(SimProbe, SpiralProbe/FanProbe) 존재 — **미접촉.**

================================================================
## TASK 1: 역산 순수 함수 `MakeSpiralForLiveCount`
================================================================

발사주기·수명 상수를 제너레이터 헤더로 단일 출처화하고, 목표 동시 탄환 수 N → Spiral 파라미터 역산 함수를 추가한다. CVar는 아직 없다(TASK 2). 이 태스크만으로 빌드가 통과해야 한다.

### 1-1. `Source/Project_RE/Mass/REBulletPatternGenerator.h` (수정)

교체 후 **파일 전문**:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletSpawnSubsystem.h"   // FBulletSpawnParams

/**
 *  보스 탄막 패턴 발사 수학. 엔진/액터 의존 없는 순수 함수 → headless 단위 검증 가능.
 *  Spiral BaseAngle 누적 등 회전 상태는 호출자(Boss)가 소유. 제너레이터는 무상태.
 *  Homing은 M1 범위 밖 — 슬롯만.
 */
namespace REBulletPattern
{
	/** 발사 주기(s). GameMode 발사 타이머와 역산 공식의 단일 출처. */
	constexpr float FireIntervalSec   = 0.1f;
	/** 탄 수명(s). FSpiralParams::Lifetime 기본값과 역산 공식의 단일 출처. */
	constexpr float BulletLifetimeSec = 3.f;

	struct FSpiralParams
	{
		int32 Count        = 16;
		float BaseAngleDeg = 0.f;    // 이번 발사 시작각 (Boss가 누적해 전달)
		float AngleStepDeg = 22.5f;  // 탄 간 각 간격 (기본 360/16 = 균등 링)
		float Speed        = 300.f;  // uu/s
		float Lifetime     = BulletLifetimeSec;  // s
	};

	struct FFanParams
	{
		int32 Count          = 16;
		float CenterAngleDeg = 0.f;   // 부채꼴 중심 방향
		float SpreadDeg      = 90.f;  // 전체 벌어짐 각
		float Speed          = 300.f;
		float Lifetime       = 3.f;
	};

	/** 나선 팔 1개: 각도 = BaseAngle + i*AngleStep, i=0..Count-1. */
	TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P);

	/** 부채꼴: CenterAngle 기준 -Spread/2 .. +Spread/2 를 Count 등분 동시 발사. */
	TArray<FBulletSpawnParams> GenerateFan(const FVector& Origin, const FFanParams& P);

	/**
	 *  목표 동시 탄환 수 N → 균등 링 Spiral 파라미터.
	 *  steady-state 동시 탄환 = 발사당_탄수 / 발사주기 × 수명 이므로
	 *  Count = round(N × FireIntervalSec / BulletLifetimeSec), AngleStep = 360/Count.
	 *  ceil이 아니라 round인 이유: ceil(100/30)=4 → live≈120 (+20%)으로 ±10% 허용치를 넘는다.
	 *  Speed/Lifetime은 FSpiralParams 기본값 유지.
	 */
	FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg);
}
```

### 1-2. `Source/Project_RE/Mass/REBulletPatternGenerator.cpp` (수정)

`GenerateFan` 함수 다음, `namespace REBulletPattern` 닫는 `}` **앞**에 추가:

```cpp

	FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg)
	{
		FSpiralParams P;
		// Max(1,...): TargetLive<=0 (CVar 사용자 입력) 시 360/0 나눗셈 방지.
		P.Count        = FMath::Max(1, FMath::RoundToInt(TargetLive * FireIntervalSec / BulletLifetimeSec));
		P.AngleStepDeg = 360.f / P.Count;   // Count 무관 균등 링
		P.BaseAngleDeg = BaseAngleDeg;
		return P;
	}
```

`GenerateSpiral`/`GenerateFan`/`DirFromDeg`는 **건드리지 않는다.**

### 1-3. 임시 검증 로그 (TEMP — 1-5에서 반드시 제거)

테스트 프레임워크가 없으므로 **headless 로그가 테스트다.** `Source/Project_RE/Core/REGameMode.cpp`의 `BeginPlay` **맨 끝** (기존 `#16 프로브` 블록 `}` 다음, 함수 닫는 `}` 앞)에 추가:

```cpp
	// [TEMP #43 Task1] 역산 함수 검증 — TASK 1 종료 시 제거.
	{
		using namespace REBulletPattern;
		for (const int32 N : { 100, 480, 1000, 5000 })
		{
			const FSpiralParams P = MakeSpiralForLiveCount(N, 0.f);
			UE_LOG(LogTemp, Log, TEXT("[RE] ScaleMath: N=%d -> Count=%d Step=%.3f Ring=%.1f Live=%.0f"),
				N, P.Count, P.AngleStepDeg, P.Count * P.AngleStepDeg,
				P.Count / FireIntervalSec * BulletLifetimeSec);
		}
	}
```

### 1-4. 빌드/검증 게이트

**빌드:** 위 "빌드 커맨드" → `Result: Succeeded`

**프로브:**
```bash
cd /e/UnrealProjects/Project_RE-scale-knob
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_scalemath.log &
sleep 30
grep "ScaleMath" "Saved/Logs/RE_scalemath.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

**기대 출력 (정확히 4줄):**
```
[RE] ScaleMath: N=100 -> Count=3 Step=120.000 Ring=360.0 Live=90
[RE] ScaleMath: N=480 -> Count=16 Step=22.500 Ring=360.0 Live=480
[RE] ScaleMath: N=1000 -> Count=33 Step=10.909 Ring=360.0 Live=990
[RE] ScaleMath: N=5000 -> Count=167 Step=2.156 Ring=360.0 Live=5010
```

**판정:**
- **N=480 → Count=16, Step=22.5°** — 기존 하드코딩과 정확히 일치해야 한다. 다르면 회귀다.
- **Ring = 360.0 4줄 전부** — 링이 닫힌다는 뜻 (완료조건 "균등 링").
- 오차: N=100 −10%(경계), N=1000 −1%, N=5000 +0.2% — 전부 ±10% 안.

Count가 다르면 `FMath::RoundToInt` 인자의 정수/부동소수 승격을 확인하라 (`TargetLive * FireIntervalSec`는 `int32 * float` → `float`이 맞다). `CeilToInt`를 쓰면 N=100이 Count=4(Live=120, **+20%**)가 되어 완료조건을 못 지킨다 — `RoundToInt`가 맞다.

### 1-5. TEMP 블록 제거 + 빌드 재확인

1-3에서 넣은 `// [TEMP #43 Task1]` 블록 **전체를 삭제**한다. 역산은 TASK 2에서 Boss가 실제로 호출하므로 이 로그는 더 필요 없다.

빌드 재실행 → `Result: Succeeded` (`REGameMode.cpp`가 원본 상태로 복귀했으므로 에러 0)

### 1-6. 커밋

```bash
git add Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletPatternGenerator.cpp
git status --short   # REGameMode.cpp가 목록에 없어야 한다 (TEMP 블록 완전 제거 확인)
git commit -m "feat(M3): derive spiral count from target live bullet count (#43)"
```

`git status --short`에 `REGameMode.cpp`가 보이면 TEMP 블록이 남은 것이다. 제거하고 다시 하라.

================================================================
## TASK 2: CVar `re.Bullets.Count` 배선
================================================================

CVar를 선언하고 발사 시점에 읽어 역산 함수에 넘긴다. GameMode 타이머의 `0.1f` 리터럴을 상수 참조로 바꾼다.

### 2-1. `Source/Project_RE/Core/REBossCharacter.cpp` — CVar 선언 (수정)

include 블록 다음, `AREBossCharacter::AREBossCharacter()` **앞**에 추가:

```cpp
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
```

링크 에러가 나면 include에 `#include "HAL/IConsoleManager.h"`를 추가하라.

### 2-2. `Source/Project_RE/Core/REBossCharacter.cpp` — Spiral 케이스 (수정)

`TriggerBulletPattern`의 `case EBulletPattern::Spiral:` 블록을 아래로 **통째 교체**:

```cpp
	case EBulletPattern::Spiral:
	{
		// 발사 시점 조회 — CVar 변경이 재시작/타이머 재설정 없이 다음 발사부터 반영된다.
		const int32 TargetLive = CVarBulletCount.GetValueOnGameThread();
		const REBulletPattern::FSpiralParams SP =
			REBulletPattern::MakeSpiralForLiveCount(TargetLive, SpiralBaseAngleDeg);
		Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: Target=%d BaseAngle=%.1f -> N=%d"),
			TargetLive, SpiralBaseAngleDeg, Params.Num());
		SpiralBaseAngleDeg += SpiralRotationStepDeg;  // 다음 호출 시 회전
		break;
	}
```

**건드리지 않는다:** 함수 시그니처, `bIsDead` 가드, `Fan`/`Homing` 케이스, `SpawnBulletBatch` 호출, 하단 요약 `UE_LOG`, `TakeDamage`, `OnRep_Health`.

### 2-3. `Source/Project_RE/Core/REGameMode.cpp` — 타이머 상수화 (수정, 1줄)

`BeginPlay`의 `SetTimer` 줄(현재 `:67`)을 교체.

기존:
```cpp
			GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, 0.1f, /*bLoop=*/true);
```
교체 후:
```cpp
			GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, REBulletPattern::FireIntervalSec, /*bLoop=*/true);
```

`REBulletPatternGenerator.h`는 `REGameMode.cpp:12`에서 **이미 include 중**이다. BeginPlay의 다른 코드(프로브, 초기 트리거 2회, SimProbe)는 **미접촉**.

### 2-4. 빌드/검증 게이트

**빌드:** 위 "빌드 커맨드" → `Result: Succeeded`, 에러 0.

**기본값 회귀 확인 (CVar 미지정 = 기존 동작):**
```bash
cd /e/UnrealProjects/Project_RE-scale-knob
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_default.log &
sleep 30
grep "Boss Spiral" "Saved/Logs/RE_default.log" | head -3
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

**기대 출력:**
```
[RE] Boss Spiral: Target=480 BaseAngle=0.0 -> N=16
[RE] Boss Spiral: Target=480 BaseAngle=15.0 -> N=16
```
`Target=480 -> N=16` — 기본값이 기존 하드코딩 16발과 정확히 같다. 회귀 없음.

### 2-5. 커밋

```bash
git add Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M3): add re.Bullets.Count CVar to scale bullet volume (#43)"
```

================================================================
## TASK 3: 헤드리스 프로브로 100/1000/5000 실측
================================================================

CVar를 `-ExecCmds`로 주입해 3개 수치에서 steady-state `live=`가 목표에 수렴하는지 관측한다. **코드 변경 없음.** 관측 대상은 `Mass/REBulletRenderProcessor.cpp:71-76`의 기존 `RenderProbe` 로그 — **#44 소유, 읽기만, 수정 금지.**

### 3-1. N=100 프로브

```bash
cd /e/UnrealProjects/Project_RE-scale-knob
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.Bullets.Count 100" -log=RE_scale100.log &
sleep 40
grep -E "RenderProbe|Boss Spiral" "Saved/Logs/RE_scale100.log" | tail -12
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

**기대:** `Target=100 ... -> N=3`, 그리고 3초(수명) 경과 후 `live=`가 **~90에서 평평하게 안정**. 목표 100 대비 −10% — 완료조건 경계 안이다.

`live`는 배치 단위로 30~31개 배치가 공존하므로 `Count×30 ~ Count×31` (여기선 90~93) 범위에서 1~2 진동할 수 있다. **이산 스폰의 정상 동작이지 버그가 아니다.**

**`RenderProbe` 로그가 한 줄도 없으면:** `-nullrhi`에서 ISM이 없어 `RenderProcessor::Execute`가 early-return한 것이다 (`REBulletRenderProcessor.cpp:39-42`). 그 경우 `-nullrhi`를 빼고 `-windowed -ResX=640 -ResY=480`으로 바꿔 실RHI로 재실행하라. RenderProcessor는 `Standalone` 넷모드에서 여전히 돌므로 로그가 나온다. 3-1~3-3 전부 같은 방식으로 폴백하고, 폴백 사실을 PR 본문에 기록하라.

### 3-2. N=1000 프로브

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.Bullets.Count 1000" -log=RE_scale1000.log &
sleep 40
grep -E "RenderProbe|Boss Spiral" "Saved/Logs/RE_scale1000.log" | tail -12
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

**기대:** `Target=1000 ... -> N=33`, steady-state `live=` ≈ **990** (목표 1000의 −1%, ±10% 이내 ✅)

### 3-3. N=5000 프로브

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.Bullets.Count 5000" -log=RE_scale5000.log &
sleep 60
grep -E "RenderProbe|Boss Spiral" "Saved/Logs/RE_scale5000.log" | tail -12
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

**기대:** `Target=5000 ... -> N=167`, steady-state `live=` ≈ **5010** (+0.2%, ±10% 이내 ✅)

프레임레이트가 떨어져 steady-state 도달이 늦어질 수 있다 (`sleep 60`으로 여유를 뒀다). **느린 건 정상이다 — 이게 #46이 측정할 대상이다. 여기서 최적화하지 마라.**

### 3-4. 실측 오차 판정

| N | 기대 Count | 기대 live | 허용 범위 (±10%) |
|---|---|---|---|
| 100 | 3 | ~90 | 90 ~ 110 |
| 1000 | 33 | ~990 | 900 ~ 1100 |
| 5000 | 167 | ~5010 | 4500 ~ 5500 |

**N=100이 90 미만이면 실패다** (허용 하한이 정확히 90). 배치 공존 수가 30이 아니라 29라는 뜻 — `REBulletSimProcessor.cpp`의 수명 판정이 `>=`인지 `>`인지, `Lifetime`이 정확히 3.0s로 전달되는지 로그로 확인하라. **그 파일은 #44 소유라 수정 금지** — 읽어서 원인만 파악하고, 수정이 필요하면 `MakeSpiralForLiveCount`(내 소유)에서 조정하거나 사용자에게 보고하라.

**N=5000이 크게 미달하면** (예: 3000대) 스폰이 프레임 예산을 못 따라간 것이다 — `SpawnBulletBatch`의 per-entity `CreateEntity` 병목. **#46이 측정할 대상이지 여기서 고칠 게 아니다.** 수치를 기록하고 사용자에게 보고하라.

### 3-5. PIE 런타임 반영 확인 (완료조건 "재시작 없이")

에디터 PIE 실행 중 `` ` `` 콘솔에 `re.Bullets.Count 1000` 입력 → **재시작 없이** 다음 로그 줄부터 `Target=1000 -> N=33`으로 바뀌는지 관측. 코드상 `GetValueOnGameThread()`가 발사 시점 호출이므로 통과해야 한다.

### 3-6. 커밋 (코드 변경 없으면 생략)

TASK 3은 관측만 한다. `-nullrhi` 폴백이나 3-4 판정으로 코드를 고쳤다면 그 변경만 커밋:
```bash
git add Source/Project_RE/Mass/REBulletPatternGenerator.cpp
git commit -m "fix(M3): <실제 고친 내용> (#43)"
```
고친 게 없으면 커밋하지 마라. 측정 결과는 PR 본문에 넣는다 (아래).

## 완료 후

### 완료조건 최종 체크 (이슈 #43) — 로그 증거 없이 체크하지 마라

- [ ] `re.Bullets.Count 100` → live 100±10% 안정 (3-1 로그, ~90)
- [ ] `re.Bullets.Count 1000` → live 1000±10% 안정 (3-2 로그, ~990)
- [ ] `re.Bullets.Count 5000` → live 5000±10% 안정 (3-3 로그, ~5010)
- [ ] CVar 변경이 재시작 없이 다음 발사부터 반영 (3-5 PIE 관측)
- [ ] 링 패턴이 Count와 무관하게 균등 — `AngleStep = 360/Count` (1-4의 `Ring=360.0` 4줄)
- [ ] 헤드리스 프로브로 3개 수치 로그 관측 (3-1~3-3)

### PR

- **base: `dev`** (main 아님)
- 이슈 #43의 메타를 **전부 미러링**: label(`C++`, `mass-entity`), milestone(`M3: 스케일 업 + 프로파일링`), assignee(`leejimin3`), project(`Project_RE 개발 로드맵`)
- Reviewer 생략
- **PR 본문에 측정 결과를 반드시 포함** (goal/spec 문서에 따로 기록하지 않는다):
  - 3개 수치 각각의 `[RE] Boss Spiral: Target=... -> N=...` 로그 1줄
  - 3개 수치 각각의 steady-state `[RE] RenderProbe: live=...` 로그 2~3줄
  - 목표 대비 오차율 표 (N / Count / 실측 live / 오차%)
  - `-nullrhi`에서 폴백했다면 그 사실
  - N=5000 체감 프레임 저하 (있다면) — **정성 기록만. 수치 측정은 #46 범위.**
- **설계 이탈 1건을 PR 본문에 명시:** 이슈 본문은 `발사당_탄수 = ceil(N/30)`을 지정했으나 **`round`로 구현**했다. `ceil(100/30)=4` → live≈120 = **+20%** 로 이슈 자신의 완료조건(±10%)을 N=100에서 위반하기 때문이다. `round`면 N=100→3(−10%), N=480→16(0%), N=1000→33(−1%), N=5000→167(+0.2%) — 4개 전부 ±10% 안.

### 남은 의도된 TODO (후속 이슈 몫 — 지우지 마라)

- `REBossCharacter.cpp`의 `// TODO M5: Multicast_TriggerPattern RPC로 교체` — M5 데디 몫.
- `REGameMode.cpp` BeginPlay의 M0/M1 프로브 코드 — 정리는 별도 판단, 이번 스코프 아님.

## 하지 말 것 (스코프 밖)

- ❌ `REBulletSpawnSubsystem::SpawnBulletBatch`의 per-entity `CreateEntity` 루프 최적화 → **#46.** N=5000에서 느린 건 측정 결과지 버그가 아니다.
- ❌ CPU 시간·프로파일링 수치 측정 → **#46.**
- ❌ `Mass/REBulletSimProcessor.cpp`, `Mass/REBulletRenderProcessor.cpp`, `Mass/REBulletHitProcessor.cpp`, `Config/`, `scripts/` 수정 → **#44 소유.** (읽기만)
- ❌ 신규 `Baseline/` 폴더 → **#45 소유.**
- ❌ Fan/Homing 패턴 스케일링 — Spiral 하나로 측정 충분.
- ❌ 탄환 수에 따른 게임 밸런스 조정 (5000발이면 즉사) — 측정용 모드지 플레이용이 아님.
- ❌ 발사주기/수명을 별도 CVar로 노출 — 산출물은 **CVar 하나**(`re.Bullets.Count`)다.
- ❌ `REGameMode.cpp` BeginPlay의 기존 프로브 코드 정리 — **미접촉 결정.** (TASK 1의 TEMP 블록만 예외, 1-5에서 제거)
- ❌ 새 테스트 프레임워크 도입 — 게이트는 빌드 + headless 로그 두 가지뿐.
