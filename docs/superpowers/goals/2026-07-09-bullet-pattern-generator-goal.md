# 구현 목표: M1 #16 — 패턴 제너레이터 (나선/부채꼴)

## 컨텍스트

UE 5.8 C++ 탄막(bullet-hell) 프로젝트. MassEntity ECS로 보스 탄막 스폰·이동. **이슈 #16**(마일스톤 M1: Mass 보스 탄막 스폰 + 이동(싱글)).

이 goal이 하는 것: `EBulletPattern`별 발사 수학(Spiral/Fan)을 엔진 의존 없는 순수 수학 유닛으로 구현하고, `AREBossCharacter::TriggerBulletPattern`의 switch를 실배선한다. 보스 위치 시작 + 각도별 방향 × Speed = Velocity를 `FBulletSpawnParams` 배열로 만들어 `SpawnBulletBatch`(#14)로 넘긴다. #15 SimProcessor가 이 Velocity를 소비해 탄이 전개된다.

**스코프 밖(손대지 말 것):** Homing 수학(슬롯만), 발사 타이머/연사, 플레이어 조준, ISM 렌더(#17), 이동/수명 감소(#15 완료분).

설계 스펙: `docs/superpowers/specs/2026-07-09-bullet-pattern-generator-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-09-bullet-pattern-generator.md`
(참고 가능. 단 **아래 코드가 최종 정본** — spec/plan과 어긋나면 이 goal을 따른다.)

## 브랜치

`dev`에서 분기하지 않음 — 현재 브랜치 `feature/M1-bullet-archetype-spawn`(#14/#15 후속 작업 계속)에서 이어서 커밋. PR base=`dev`.

## 전역 제약

- 엔진 빌드 커맨드:
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
- **Build.cs / .uproject 변경 금지** — 순수 C++ + 기존 헤더만. MassGameplay 플러그인은 #15에서 이미 활성(SimProcessor 구동용).
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그 관측**.
- 로그 접두어 `[RE]` 고정. 클래스/네임스페이스/타입명 아래 코드와 동일.
- YAGNI: Homing 수학, 발사 타이머/연사, 플레이어 조준, ISM 렌더(#17) 전부 스코프 밖.

## 검증된 API (실물 확인됨)

- `FMath::DegreesToRadians` / `RadiansToDegrees` / `Cos` / `Sin` / `Atan2` — 표준 (`CoreMinimal.h`).
- `FVector::Size()` — 속도 크기 검증용.
- `FBulletSpawnParams { FVector Location; FVector Velocity; float Lifetime; }` — `Source/Project_RE/Mass/REBulletSpawnSubsystem.h` (#14 산출). UStruct 아님, 집합 초기화 순서 = Location, Velocity, Lifetime.
- `UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams>)` — #14. `TArray` 암시 변환 OK.
- headless 프로브 방식: `-game -nullrhi -unattended`, `MSYS_NO_PATHCONV=1` 필수, 로그는 `Saved/Logs/*.log`.

## 기존 파일 현황 (변경 대상)

- `Source/Project_RE/Core/REBossCharacter.h` — `AREBossCharacter : ACharacter`. `public: TriggerBulletPattern(EBulletPattern, int32, float)` 선언만. private 멤버 없음.
- `Source/Project_RE/Core/REBossCharacter.cpp` — 익명 namespace에 `constexpr int32 BulletsPerPattern = 16;`. `TriggerBulletPattern`은 switch에 `// TODO M1(#16)` 주석만 있고, switch 밖에서 `Velocity=0/Lifetime=0`으로 16발 채워 `SpawnBulletBatch` 호출 + 요약 로그.
- `Source/Project_RE/Core/REGameMode.cpp` — `BeginPlay`가 Mass 스모크 → Processor flags 로그 → Boss 스폰 후 `Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f)` 1회 → `// #15 프로브` 탄환 스폰. `Tick`에 #15 readback. include에 `REBossCharacter.h`, `REBulletSpawnSubsystem.h`, `Mass/EntityFragments.h` 이미 존재.
- `Source/Project_RE/Mass/REBulletSpawnSubsystem.h` — `FBulletSpawnParams`, `SpawnBullet`/`SpawnBulletBatch` 정의처.

================================================================
## TASK 1: 패턴 제너레이터 신규 유닛
================================================================

### 1-1. `Source/Project_RE/Mass/REBulletPatternGenerator.h` (신규)

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

### 1-2. `Source/Project_RE/Mass/REBulletPatternGenerator.cpp` (신규)

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

### 1-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-4. 커밋

```bash
git add Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletPatternGenerator.cpp
git commit -m "feat(M1): add bullet pattern generator (spiral/fan math) (#16)"
```

================================================================
## TASK 2: Boss switch 실배선 + BaseAngle 누적
================================================================

### 2-1. `Source/Project_RE/Core/REBossCharacter.h` (수정)

클래스 끝(`TriggerBulletPattern` 선언 뒤)에 private 블록 추가. 수정 후 클래스 전문:

```cpp
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

private:
	/** Spiral 호출마다 누적되는 시작각. 연속 트리거 시 링이 회전한다. */
	float SpiralBaseAngleDeg = 0.f;
	/** Spiral 호출당 BaseAngle 증가량(deg). */
	static constexpr float SpiralRotationStepDeg = 15.f;
};
```

### 2-2. `Source/Project_RE/Core/REBossCharacter.cpp` (수정)

익명 namespace의 `constexpr int32 BulletsPerPattern = 16;` **삭제**(더 이상 참조 없음). include에 `#include "REBulletPatternGenerator.h"` 추가. 파일 전문:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "REBulletPatternGenerator.h"

AREBossCharacter::AREBossCharacter()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
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

### 2-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-4. 커밋

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M1): wire boss TriggerBulletPattern to pattern generator (#16)"
```

================================================================
## TASK 3: GameMode 프로브 배선 + headless 검증
================================================================

### 3-1. `Source/Project_RE/Core/REGameMode.cpp` (수정)

세 곳 편집. **include 추가** — 상단 include 블록에:
```cpp
#include "REBulletPatternGenerator.h"
```

**Boss Spiral 2회 트리거** — 기존 Boss 스폰 블록의 단일 트리거 라인을 2회로 교체. 교체 후 블록:
```cpp
	if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(
			AREBossCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, BossSpawnParams))
	{
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);  // #16 프로브: BaseAngle 누적 확인
	}
```

**제너레이터 수학 단위 프로브** — `BeginPlay()` 맨 끝(#15 프로브 블록 뒤)에 추가:
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

### 3-2. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 3-3. headless 런타임 프로브 (Acceptance)

Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe16.log &
sleep 30
grep -E "\[RE\] (SpiralProbe|FanProbe|Boss Spiral)" "Saved/Logs/RE_probe16.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대 로그:
```
[RE] Boss Spiral: BaseAngle=0.0 -> N=16
[RE] Boss Spiral: BaseAngle=15.0 -> N=16          ← 2번째 호출, 회전 누적
[RE] SpiralProbe: N=16 |V0|=300.0 ang0=0.0 ang1=22.5
[RE] FanProbe: N=16 ang_first=-45.0 ang_last=45.0
```
**합격 기준 3개 (전부 관측되어야 통과):**
1. **Spiral 각도/속도:** `SpiralProbe`에서 `|V0|≈300`, `ang0≈0`, `ang1≈22.5`(= AngleStep).
2. **Fan 대칭:** `FanProbe`에서 `ang_first≈-45`, `ang_last≈+45`(= ±Spread/2).
3. **BaseAngle 누적:** `Boss Spiral` 로그 2줄이 `BaseAngle=0.0` → `15.0`(= SpiralRotationStepDeg)로 증가.

### 3-4. 커밋

```bash
git add Source/Project_RE/Core/REGameMode.cpp
git commit -m "test(M1): add pattern generator headless probe in GameMode (#16)"
```

## 완료 후

**PR 생성** (base=`dev`, head=`feature/M1-bullet-archetype-spawn`). 이슈 #16 메타 미러링:
- **labels:** `mass-entity`, `C++`
- **milestone:** `M1: Mass 보스 탄막 스폰 + 이동 (싱글)` (#2)
- **assignee:** `leejimin3`
- **project:** `Project_RE 개발 로드맵` (Todo)
- Reviewer 생략. 본문 6개 필드 전부 채움(요약/변경/검증/스코프밖/이슈링크 등). 본문에 `Closes #16`.

**남은 의도된 TODO 마커(후속 이슈 몫, 지금 건드리지 말 것):**
- `// TODO M5:` Multicast RPC (Boss).
- `// #16 프로브` / `// #15 프로브` (GameMode) — M1 마감까지 회귀 검증용 존치. #17 데모 씬에서 제거.

## 하지 말 것 (스코프 밖)

- **Homing 수학** — 슬롯만, `Homing` case는 early-return + 경고 로그. 타깃 추적 구현 금지(후속).
- **발사 타이머/연사 루프** — 단발 트리거만. Spiral 나선 애니메이션(연속 회전)은 타이머 도입하는 후속 마일스톤 몫(BaseAngle 누적은 이미 구현됨).
- **플레이어 조준** — Fan `CenterAngleDeg` 동적화 금지. M1은 고정 중심각(0).
- **ISM 인스턴스/InstanceIndex/렌더** — #17.
- **이동/수명 감소** — #15 완료분. SimProcessor 재수정 금지.
- **Build.cs / .uproject 변경** — 순수 C++, 불필요.
