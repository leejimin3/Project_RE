# M1 #16 패턴 제너레이터 (나선/부채꼴) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `EBulletPattern`별 발사 수학(Spiral/Fan)을 순수 수학 유닛으로 구현하고, `AREBossCharacter::TriggerBulletPattern`의 switch를 실배선한다. 보스 위치 시작 + 각도별 방향 × Speed = Velocity를 `FBulletSpawnParams` 배열로 만들어 `SpawnBulletBatch`(#14)로 넘긴다. #15 SimProcessor가 이 Velocity를 소비해 전개. 헤드리스 프로브로 각도/속도 수학 + Spiral BaseAngle 누적을 실증.

**Architecture:** 엔진/액터 의존 없는 static 함수 `REBulletPattern::GenerateSpiral/GenerateFan(Origin, Params) -> TArray<FBulletSpawnParams>`. Boss는 switch에서 패턴별 파라미터 구조체를 채워 제너레이터 호출 → 결과를 `SpawnBulletBatch`. Spiral 회전 상태(`SpiralBaseAngleDeg`)는 Boss 멤버로 두어 호출마다 누적(제너레이터는 무상태 유지). 검증은 `AREGameMode`(기존 프로브 하네스) BeginPlay에서 제너레이터를 직접 호출해 각도/속도를 readback 로깅 + Boss Spiral 2회 트리거로 BaseAngle 누적 관측.

**Tech Stack:** UE 5.8 C++, `FMath`/`FVector`만 사용 (엔진 Mass API 신규 없음). `FBulletSpawnParams`/`SpawnBulletBatch`는 #14 산출. 신규 모듈·플러그인 없음.

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — 순수 C++ + 기존 헤더만. MassGameplay 플러그인은 #15에서 이미 활성(SimProcessor 구동용).
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그 관측** (`[[headless-runtime-probe]]`).
- 로그 접두어 `[RE]` 고정. 클래스/네임스페이스/타입명 스펙과 동일.
- 브랜치: `feature/M1-bullet-archetype-spawn` (현재, #14/#15 후속). PR base=dev.
- YAGNI: Homing 수학, 발사 타이머/연사, 플레이어 조준, ISM 렌더(#17) 전부 스코프 밖.

**사용 API (전부 기존/표준 — 신규 엔진 API 없음):**
- `FMath::DegreesToRadians` / `RadiansToDegrees` / `Cos` / `Sin` / `Atan2` — 표준.
- `FBulletSpawnParams { FVector Location; FVector Velocity; float Lifetime; }` — `REBulletSpawnSubsystem.h` (#14).
- `UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams>)` — #14.
- `FVector::Size()` — 속도 크기 검증용.

---

### Task 1: 패턴 제너레이터 신규 유닛

**Files:**
- Create: `Source/Project_RE/Mass/REBulletPatternGenerator.h`
- Create: `Source/Project_RE/Mass/REBulletPatternGenerator.cpp`

**Interfaces:**
- Consumes: `FBulletSpawnParams`(`REBulletSpawnSubsystem.h`).
- Produces: `REBulletPattern::GenerateSpiral/GenerateFan`. Boss(Task 2)가 호출.

- [ ] **Step 1: 헤더 작성** — `REBulletPatternGenerator.h`

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
	struct FSpiralParams
	{
		int32 Count        = 16;
		float BaseAngleDeg = 0.f;    // 이번 발사 시작각 (Boss가 누적해 전달)
		float AngleStepDeg = 22.5f;  // 탄 간 각 간격 (기본 360/16 = 균등 링)
		float Speed        = 300.f;  // uu/s
		float Lifetime     = 3.f;    // s
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
}
```

- [ ] **Step 2: 구현 작성** — `REBulletPatternGenerator.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletPatternGenerator.h"

namespace
{
	/** 각도(deg) → 수평면(XY, Z up) 단위벡터. */
	FVector DirFromDeg(float Deg)
	{
		const float R = FMath::DegreesToRadians(Deg);
		return FVector(FMath::Cos(R), FMath::Sin(R), 0.f);
	}
}

namespace REBulletPattern
{
	TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		Out.Reserve(P.Count);
		for (int32 i = 0; i < P.Count; ++i)
		{
			const float Angle = P.BaseAngleDeg + i * P.AngleStepDeg;
			Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime });
		}
		return Out;
	}

	TArray<FBulletSpawnParams> GenerateFan(const FVector& Origin, const FFanParams& P)
	{
		TArray<FBulletSpawnParams> Out;
		Out.Reserve(P.Count);
		const float Start = P.CenterAngleDeg - P.SpreadDeg * 0.5f;
		const float Step  = (P.Count > 1) ? P.SpreadDeg / (P.Count - 1) : 0.f;
		for (int32 i = 0; i < P.Count; ++i)
		{
			const float Angle = Start + i * Step;
			Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime });
		}
		return Out;
	}
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
git add Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletPatternGenerator.cpp
git commit -m "feat(M1): add bullet pattern generator (spiral/fan math) (#16)"
```

---

### Task 2: Boss switch 실배선 + BaseAngle 누적

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.h` (BaseAngle 멤버)
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp` (switch 배선, Velocity=0 루프 삭제)

**Interfaces:**
- Consumes: Task 1 제너레이터, `SpawnBulletBatch`(#14).
- Produces: 실 Velocity 탄환 → #15 SimProcessor가 전개.

- [ ] **Step 1: `REBossCharacter.h` — 나선 누적 상태 멤버 추가**

`AREBossCharacter` 클래스 `public:` 블록 뒤에 추가:
```cpp
private:
	/** Spiral 호출마다 누적되는 시작각. 연속 트리거 시 링이 회전한다. */
	float SpiralBaseAngleDeg = 0.f;
	/** Spiral 호출당 BaseAngle 증가량(deg). */
	static constexpr float SpiralRotationStepDeg = 15.f;
```

- [ ] **Step 2: `REBossCharacter.cpp` — include + switch 실배선**

상단 include에 추가:
```cpp
#include "REBulletPatternGenerator.h"
```

`switch (Pattern)` 블록 + 그 뒤 "Velocity=0 루프" 전체(현재 라인 29~49)를 아래로 교체:
```cpp
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
```

기존 요약 로그 라인(`Boss::TriggerBulletPattern: Pattern=%d ...`)은 유지하되 `BulletsPerPattern` → `Params.Num()` 로 교체:
```cpp
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities at %s"),
		(int32)Pattern, Seed, StartTime, Params.Num(), *GetActorLocation().ToString());
```

익명 namespace의 `constexpr int32 BulletsPerPattern = 16;` (라인 6~10) 삭제 — 제너레이터 기본 `Count`로 이관됨(더 이상 참조 없음).

- [ ] **Step 3: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M1): wire boss TriggerBulletPattern to pattern generator (#16)"
```

---

### Task 3: GameMode 프로브 배선 + headless 검증

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (제너레이터 단위 로그 + Boss Spiral 2회 트리거)

**Interfaces:**
- Consumes: Task 1 제너레이터, Task 2 Boss 배선.
- Produces: 없음(검증 스캐폴딩). headless 로그로 각도/속도 + BaseAngle 누적 실증.

- [ ] **Step 1: `REGameMode.cpp` — include 추가**

상단 include에 추가:
```cpp
#include "REBulletPatternGenerator.h"
```

- [ ] **Step 2: `REGameMode.cpp` — Boss Spiral 2회 트리거 (BaseAngle 누적 관측)**

기존 Boss 스폰 블록(라인 48~52)의 단일 트리거를 2회로:
```cpp
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);  // #16 프로브: BaseAngle 누적 확인
```

- [ ] **Step 3: `REGameMode.cpp` — 제너레이터 수학 단위 프로브**

`BeginPlay()` 끝(#15 프로브 블록 뒤)에 추가:
```cpp
	// #16 프로브: 패턴 제너레이터 수학 단위 검증 (순수 함수, 프레임 무관).
	{
		using namespace REBulletPattern;
		const TArray<FBulletSpawnParams> Sp = GenerateSpiral(FVector::ZeroVector, FSpiralParams{});
		const float SA0 = FMath::RadiansToDegrees(FMath::Atan2(Sp[0].Velocity.Y, Sp[0].Velocity.X));
		const float SA1 = FMath::RadiansToDegrees(FMath::Atan2(Sp[1].Velocity.Y, Sp[1].Velocity.X));
		UE_LOG(LogTemp, Log, TEXT("[RE] SpiralProbe: N=%d |V0|=%.1f ang0=%.1f ang1=%.1f"),
			Sp.Num(), Sp[0].Velocity.Size(), SA0, SA1);

		const TArray<FBulletSpawnParams> Fn = GenerateFan(FVector::ZeroVector, FFanParams{});
		const float FA0 = FMath::RadiansToDegrees(FMath::Atan2(Fn[0].Velocity.Y, Fn[0].Velocity.X));
		const float FAL = FMath::RadiansToDegrees(FMath::Atan2(Fn.Last().Velocity.Y, Fn.Last().Velocity.X));
		UE_LOG(LogTemp, Log, TEXT("[RE] FanProbe: N=%d ang_first=%.1f ang_last=%.1f"), Fn.Num(), FA0, FAL);
	}
```

- [ ] **Step 4: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: headless 런타임 프로브 (Acceptance)**

PIE 없이 headless. Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe16.log &
sleep 30
grep -E "\[RE\] (SpiralProbe|FanProbe|Boss Spiral)" "Saved/Logs/RE_probe16.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected 로그:
```
[RE] Boss Spiral: BaseAngle=0.0 -> N=16
[RE] Boss Spiral: BaseAngle=15.0 -> N=16          ← 2번째 호출, 회전 누적
[RE] SpiralProbe: N=16 |V0|=300.0 ang0=0.0 ang1=22.5
[RE] FanProbe: N=16 ang_first=-45.0 ang_last=45.0
```
**합격 기준 3개:**
1. **Spiral 각도/속도:** `SpiralProbe`에서 `|V0|≈300`, `ang0≈0`, `ang1≈22.5`(= AngleStep).
2. **Fan 대칭:** `FanProbe`에서 `ang_first≈-45`, `ang_last≈+45`(= ±Spread/2).
3. **BaseAngle 누적:** `Boss Spiral` 로그 2줄이 `BaseAngle=0.0` → `15.0`(= SpiralRotationStepDeg)로 증가.

셋 다 관측되면 패턴 수학 + 회전 누적 검증 완료.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.cpp
git commit -m "test(M1): add pattern generator headless probe in GameMode (#16)"
```

---

## 완료 후

- #16 완료 → 후속 M1: #17(ISM 렌더 + 데모 영상 — 갱신된 FTransformFragment를 인스턴스 메시로 그림).
- **프로브 스캐폴딩 처리:** GameMode의 `// #16 프로브`(Boss 2회 트리거 + 제너레이터 단위 로그)와 `// #15 프로브`(ProbeBullet)는 마커 유지. #17 데모 씬 구성 시 GameMode 프로브 경로 제거(실제 보스 발사 루프로 대체). 지금 제거하면 회귀 검증 수단이 사라지므로 M1 마감까지 존치.
- **후속 마일스톤 훅:** Spiral 나선 애니메이션(연속 회전)은 발사 타이머 도입 시 자연히 나타남(BaseAngle 누적 이미 구현). Homing 수학, Fan CenterAngle 동적 조준은 후속.

## Self-Review

- **Spec coverage:** 스펙 §컴포넌트1(제너레이터 h/cpp)→T1, §컴포넌트2(Boss switch + BaseAngle 멤버)→T2, §검증(제너레이터 단위 + BaseAngle 누적)→T3. Homing early-return→T2 Step2. 갭 없음.
- **Placeholder scan:** 코드 블록 전부 완전. 잔존 `// #16 프로브`/`// #15 프로브` 마커는 스펙이 명시한 의도된 검증 스캐폴딩(작업 누락 아님).
- **Type consistency:** `GenerateSpiral/Fan(const FVector&, const FSpiralParams&/FFanParams&) -> TArray<FBulletSpawnParams>` 선언(T1) ↔ 호출(T2 Boss, T3 프로브) 시그니처 일치. `FBulletSpawnParams` 집합 초기화 `{ Origin, Velocity, Lifetime }` = 멤버 순서(Location, Velocity, Lifetime) 일치. `SpawnBulletBatch(TConstArrayView<FBulletSpawnParams>)` ← `TArray` 암시 변환 OK.
- **Math sanity:** `DirFromDeg(0)=(1,0,0)` → ang0=atan2(0,1)=0°. Spiral ang1 = i=1 → 22.5°. Fan Count=16 → Step=90/15=6°, Start=-45 → 첫=-45, 끝=-45+15*6=+45. `|V|=Speed*|unit|=300`. 전부 기대 로그와 일치.
- **0-나눗셈:** Fan `Count>1` 가드로 `Step` 분모 보호. Count=1이면 Step=0 → 중심각 1발.
- **Boss 상태 소유:** `SpiralBaseAngleDeg` Boss 멤버 → 제너레이터 무상태 유지. 프로브 2회 트리거로 0→15 누적 관측. `SpiralRotationStepDeg`는 `static constexpr`(인스턴스별 상태 아님).
- **Include 검증:** `REBulletPatternGenerator.h`가 `REBulletSpawnSubsystem.h` include → `FBulletSpawnParams` 가시. Boss/GameMode cpp는 `REBulletPatternGenerator.h`만 추가. `FMath`/`FVector`는 `CoreMinimal.h`(기존). 신규 모듈 의존 없음.
- **삭제 안전성:** `BulletsPerPattern` 익명 namespace 상수는 T2에서 유일 참조처(스폰 루프 + 요약 로그) 둘 다 교체되므로 삭제해도 orphan 없음.
