# Actor 탄환 베이스라인 (Mass vs Actor 비교군) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 최적화하지 않은 순진한 액터 기반 탄환 경로를 만들어 Mass 경로의 공정한 비교군으로 세운다 (이슈 #45).

**Architecture:** `AREBulletActor`가 자기 Tick에서 이동·자멸한다(Mass의 Sim+Render 프로세서를 액터 1개로 접은 것). `UREActorBulletSpawner`(UWorldSubsystem)가 `OnWorldBeginPlay`에서 0.1초 반복 타이머를 걸고 자립 구동하며, CVar `re.ActorBullets.Count`(동시 유지 목표 탄 수)를 발사당 탄 수로 역산해 `REBulletPattern::GenerateSpiral`(Mass와 공유하는 순수 함수)로 스폰한다. `AREGameMode`는 건드리지 않는다.

**Tech Stack:** UE 5.8 C++, `UWorldSubsystem`, `FTimerManager`, `TAutoConsoleVariable`, `UStaticMeshComponent`.

**Spec:** `docs/superpowers/specs/2026-07-15-actor-bullet-baseline-design.md`

## Global Constraints

- 워크트리: `E:\UnrealProjects\Project_RE-actor-baseline` (브랜치 `feature/M3-actor-baseline`, `dev`에서 분기)
- **`Source/Project_RE/Core/REGameMode.cpp/.h` 를 절대 수정하지 않는다.** #43 세션이 같은 파일을 뜯고 있다 — 정면충돌한다.
- **`Source/Project_RE/Mass/**` 를 수정하지 않는다.** #44 소유. `Mass/REBulletPatternGenerator.h` 는 읽기 전용 참조만.
- 소유 파일: `Source/Project_RE/Baseline/**` (전부 신규), `Source/Project_RE/Project_RE.Build.cs` (이 파일은 #45만 건드린다).
- **액터 경로를 최적화하지 않는다.** 오브젝트 풀링·HISM·Tick 그룹 튜닝 전부 금지 — 순진한 구현이어야 비교가 공정하다.
- 빌드 커맨드 (Git Bash):
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
    -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
- 헤드리스 프로브 (Git Bash, `MSYS_NO_PATHCONV=1` 필수 — 경로 mangling 방지):
  ```bash
  MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
    -game -nullrhi -unattended -nosplash -stdout -NoSound \
    -ExecCmds="re.ActorBullets.Count 100" -log=RE_probe45.log &
  sleep 30
  grep "\[RE\] ActorBulletProbe" "Saved/Logs/RE_probe45.log"
  "/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
  ```
- Mass 경로는 이 브랜치에서 끌 수 없다 (스위치가 #44 소유). 프로브 로그에 Mass의 `RenderProbe`/`SimProbe` 줄이 섞여 나오는 건 정상이다 — `ActorBulletProbe` 줄만 본다.
- Mass 기준값 (전부 `FSpiralParams` 기본값 + `REGameMode` 데모 타이머): 속도 300 uu/s, 수명 3 s, 발사 주기 0.1 s, 회전 스텝 15°, 원점 (0,0,90), 메시 `/Engine/BasicShapes/Sphere.Sphere`, 스케일 0.5, 머티리얼 `/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial` + MID `Color`=Red, 콜리전 NoCollision.

---

## File Structure

| 파일 | 책임 |
|---|---|
| `Source/Project_RE/Baseline/REBulletActor.h` / `.cpp` (신규) | 탄환 액터 1발. 메시 보유, 자기 Tick에서 이동 + 수명 소진 시 자멸. |
| `Source/Project_RE/Baseline/REActorBulletSpawner.h` / `.cpp` (신규) | 자립 구동 스포너 서브시스템. CVar 읽고 0.1s마다 Spiral 스폰, 공유 MID 소유, 프로브 로그. |
| `Source/Project_RE/Project_RE.Build.cs` (수정) | `PublicIncludePaths` 에 `"Project_RE/Baseline"` 추가. |

---

### Task 1: `AREBulletActor` — 탄환 액터

**Files:**
- Create: `Source/Project_RE/Baseline/REBulletActor.h`
- Create: `Source/Project_RE/Baseline/REBulletActor.cpp`
- Modify: `Source/Project_RE/Project_RE.Build.cs` (`PublicIncludePaths` 목록에 한 줄 추가)

**Interfaces:**
- Consumes: 없음 (엔진만)
- Produces: `AREBulletActor::Init(const FVector& InVelocity, float InLifetime, UMaterialInterface* InMaterial)` — Task 2의 스포너가 스폰 직후 호출한다.

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/Baseline/REBulletActor.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "REBulletActor.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;

/**
 *  액터 기반 탄환 1발 — Mass 경로(#15 SimProcessor + #17 RenderProcessor)의 비교군.
 *  일부러 최적화하지 않는다: 풀링 없음, 개별 Tick, 액터마다 메시 컴포넌트 1개.
 *  "액터로 짜면 보통 이렇게 짠다"의 정직한 버전이어야 Mass와의 비교가 공정하다 (#45).
 *  측정 전용 — 게임 코드가 이 클래스에 의존하면 안 된다.
 */
UCLASS()
class AREBulletActor : public AActor
{
	GENERATED_BODY()

public:
	AREBulletActor();

	/** 스폰 직후 스포너가 호출. 머티리얼은 스포너가 만든 공유 MID (액터마다 MID 만들면 불공정한 추가 비용). */
	void Init(const FVector& InVelocity, float InLifetime, UMaterialInterface* InMaterial);

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh = nullptr;

	/** Mass의 FBulletSimFragment::Velocity 대응 (uu/s). */
	FVector Velocity = FVector::ZeroVector;

	/** Mass의 FBulletSimFragment::Lifetime 대응 (s). 0 이하면 자멸. */
	float Lifetime = 0.f;
};
```

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/Baseline/REBulletActor.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Mass와 동일 — REBulletRenderProcessor.cpp:14 BulletScale. */
	constexpr float BulletScale = 0.5f;
}

AREBulletActor::AREBulletActor()
{
	// 개별 Tick은 액터 경로의 본질적 비용이다. 이게 비교의 요점이라 끄지 않는다.
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// Mass ISM도 NoCollision(REBulletRenderSubsystem.cpp:30, #34). 콜리전 켜면 액터 쪽에
	// 불공정한 추가 비용이 붙어 비교가 오염된다.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(BulletScale));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}
}

void AREBulletActor::Init(const FVector& InVelocity, float InLifetime, UMaterialInterface* InMaterial)
{
	Velocity = InVelocity;
	Lifetime = InLifetime;
	if (InMaterial)
	{
		Mesh->SetMaterial(0, InMaterial);
	}
}

void AREBulletActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Mass SimProcessor 대응: Transform += V*dt, Lifetime -= dt, 소진 시 파괴.
	AddActorWorldOffset(Velocity * DeltaSeconds);

	Lifetime -= DeltaSeconds;
	if (Lifetime <= 0.f)
	{
		Destroy();
	}
}
```

- [ ] **Step 3: Build.cs 에 Baseline 인클루드 경로 추가**

`Source/Project_RE/Project_RE.Build.cs` 의 `PublicIncludePaths.AddRange` 목록에서 `"Project_RE/Core",` 아래 줄에 한 줄 추가한다. 수정 후 그 부분:

```csharp
		PublicIncludePaths.AddRange(new string[] {
			"Project_RE",
			"Project_RE/Baseline",
			"Project_RE/Core",
			"Project_RE/Mass",
			"Project_RE/UI",
```

다른 줄은 건드리지 않는다.

- [ ] **Step 4: 빌드 게이트**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Build succeeded`, 에러 0. (액터는 아직 아무도 스폰하지 않으므로 런타임 변화 없음.)

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Baseline/REBulletActor.h Source/Project_RE/Baseline/REBulletActor.cpp Source/Project_RE/Project_RE.Build.cs
git commit -m "feat(M3): add naive AREBulletActor for Mass-vs-Actor baseline (#45)"
```

---

### Task 2: `UREActorBulletSpawner` — 자립 스포너 + CVar

**Files:**
- Create: `Source/Project_RE/Baseline/REActorBulletSpawner.h`
- Create: `Source/Project_RE/Baseline/REActorBulletSpawner.cpp`

**Interfaces:**
- Consumes: `AREBulletActor::Init(const FVector&, float, UMaterialInterface*)` (Task 1). `REBulletPattern::GenerateSpiral(const FVector& Origin, const FSpiralParams& P) -> TArray<FBulletSpawnParams>` (읽기 전용, `Mass/REBulletPatternGenerator.h`). `FBulletSpawnParams` 필드: `Location`, `Velocity`, `Lifetime` (`Mass/REBulletSpawnSubsystem.h`). `FSpiralParams` 필드: `Count`, `BaseAngleDeg`, `AngleStepDeg`, `Speed`(기본 300), `Lifetime`(기본 3).
- Produces: CVar `re.ActorBullets.Count` (int32, 기본 0) + 로그 `[RE] ActorBulletProbe: live=%d target=%d` — Task 3/4가 이 로그로 검증한다.

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/Baseline/REActorBulletSpawner.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "REActorBulletSpawner.generated.h"

class UMaterialInstanceDynamic;

/**
 *  액터 탄환 스포너 (#45 비교군). CVar re.ActorBullets.Count 로만 켜진다 — 기본 0 = 평상시 영향 0.
 *
 *  자립 구동: OnWorldBeginPlay에서 자기 타이머(0.1s)를 건다. GameMode에 의존하지 않는다
 *  (REBulletRenderSubsystem과 같은 패턴). Mass 데모도 0.1s 타이머라 발사 메커니즘까지 대칭이다.
 *
 *  Count는 "동시 유지 목표 탄환 수"다. 발사당 탄 수는 Count / (Lifetime/Interval) 로 역산한다.
 */
UCLASS()
class UREActorBulletSpawner : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	/** 0.1초마다 호출. CVar가 0이면 즉시 return (no-op). */
	void Fire();

	/** 전 탄환이 공유하는 빨강 MID. 액터마다 MID를 만들면 액터 경로에 불공정한 추가 비용이 붙는다. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SharedMID = nullptr;

	FTimerHandle FireTimer;

	/** Spiral 시작각 누적 — Boss의 SpiralRotationStepDeg(15°)와 동일하게 링을 회전시킨다. */
	float BaseAngleDeg = 0.f;

	/** 발사당 탄 수 소수부 누산 (Count/30 이 정수가 아닐 때 반올림 오차 누적 방지). */
	float PerShotAccum = 0.f;

	/** 프로브 로그 주기 카운터 (10회 = 1초마다 1줄). */
	int32 FireCount = 0;
};
```

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/Baseline/REActorBulletSpawner.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REActorBulletSpawner.h"
#include "REBulletActor.h"
#include "REBulletPatternGenerator.h"   // Mass와 공유하는 순수 함수 — 읽기 전용 참조 (복붙 금지)
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"                // TActorIterator
#include "TimerManager.h"

namespace
{
	/** 동시 유지 목표 탄환 수. 0 = 액터 경로 비활성 (기본). Mass 기본 부하와 맞추려면 480. */
	static TAutoConsoleVariable<int32> CVarActorBulletCount(
		TEXT("re.ActorBullets.Count"),
		0,
		TEXT("Actor 탄환 동시 유지 목표 수 (0=비활성). Mass 비교군 — 측정 전용."),
		ECVF_Cheat);

	/** Mass 데모 발사 주기 (REGameMode.cpp:67 DemoFireTimer). */
	constexpr float FireInterval = 0.1f;

	/** Mass 스폰 원점 = Boss 스폰 위치 (REGameMode.cpp:53). Boss는 움직이지 않는다. */
	const FVector SpawnOrigin(0.f, 0.f, 90.f);

	/** Boss의 SpiralRotationStepDeg (REBossCharacter.h:58). */
	constexpr float RotationStepDeg = 15.f;
}

void UREActorBulletSpawner::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 데디서버/에디터 프리뷰 월드는 제외 — 측정은 싱글(게임 월드)에서만.
	if (InWorld.GetNetMode() == NM_DedicatedServer || !InWorld.IsGameWorld())
	{
		return;
	}

	// 공유 MID 1개 — 색은 Mass ISM과 동일 (REBulletRenderSubsystem.cpp:37-43).
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		SharedMID = UMaterialInstanceDynamic::Create(Base, this);
		if (SharedMID)
		{
			SharedMID->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
		}
	}
	if (!SharedMID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] ActorBulletSpawner: SharedMID 생성 실패 (탄환 기본색으로 진행)"));
	}

	// 자립 구동 — GameMode 무관. CVar가 0이면 Fire()가 즉시 return이라 비용 무시 가능.
	InWorld.GetTimerManager().SetTimer(FireTimer, this, &UREActorBulletSpawner::Fire,
		FireInterval, /*bLoop=*/true);
}

void UREActorBulletSpawner::Fire()
{
	const int32 Target = CVarActorBulletCount.GetValueOnGameThread();
	if (Target <= 0)
	{
		return;  // 0 = 비활성. 평상시 게임 영향 0.
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	REBulletPattern::FSpiralParams SP;  // Speed 300 / Lifetime 3 = 기본값 = Mass와 동일

	// 정상상태 탄 수 = PerShot * (Lifetime / Interval). 목표 Target을 만족하는 PerShot 역산.
	// 소수부는 누산해 다음 발사로 넘긴다 (매번 올림하면 목표를 최대 +30% 초과한다).
	PerShotAccum += Target / (SP.Lifetime / FireInterval);
	const int32 N = FMath::FloorToInt(PerShotAccum);
	PerShotAccum -= N;
	if (N <= 0)
	{
		return;
	}

	SP.Count        = N;
	SP.AngleStepDeg = 360.f / N;   // 균등 링
	SP.BaseAngleDeg = BaseAngleDeg;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (const FBulletSpawnParams& P : REBulletPattern::GenerateSpiral(SpawnOrigin, SP))
	{
		if (AREBulletActor* Bullet = World->SpawnActor<AREBulletActor>(
				AREBulletActor::StaticClass(), P.Location, FRotator::ZeroRotator, SpawnParams))
		{
			Bullet->Init(P.Velocity, P.Lifetime, SharedMID);
		}
	}

	BaseAngleDeg += RotationStepDeg;

	// 프로브: 1초에 1줄 (로그가 측정을 오염시키지 않게).
	if ((FireCount++ % 10) == 0)
	{
		int32 Live = 0;
		for (TActorIterator<AREBulletActor> It(World); It; ++It)
		{
			++Live;
		}
		UE_LOG(LogTemp, Log, TEXT("[RE] ActorBulletProbe: live=%d target=%d perShot=%d"), Live, Target, N);
	}
}
```

- [ ] **Step 3: 빌드 게이트**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 4: 헤드리스 프로브 — 기본값(0)에서 액터 0, 게임 무변화**

CVar를 안 건드리고 실행한다.

Run:
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe45_off.log &
sleep 30
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
grep -c "ActorBulletProbe" "Saved/Logs/RE_probe45_off.log"
grep -E "\[RE\] (RenderProbe|SimProbe)" "Saved/Logs/RE_probe45_off.log" | head -5
```
Expected:
- `ActorBulletProbe` 매치 수 = **0** (`grep -c` 출력이 `0`)
- Mass 로그(`RenderProbe`/`SimProbe`)는 평소대로 나온다 → 기존 게임 동작 무변화 확인
- 크래시 없음

- [ ] **Step 5: 헤드리스 프로브 — Count 100 에서 ~100발 유지**

Run:
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.ActorBullets.Count 100" -log=RE_probe45_100.log &
sleep 30
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
grep "ActorBulletProbe" "Saved/Logs/RE_probe45_100.log"
```
Expected: `live=` 값이 0에서 증가해 **수명 3초가 지난 뒤 90~110 사이로 안정**(±10%)된다. `target=100`, `perShot=3` 또는 `4`(3.33 누산이라 3/4가 번갈아 나온다).

판정: 3초 이후 줄들의 `live` 가 전부 [90, 110] 이면 합격. 계속 증가만 하면 = 액터가 안 죽는 것(Tick/Destroy 버그), 0에 머물면 = 스폰 실패.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Baseline/REActorBulletSpawner.h Source/Project_RE/Baseline/REActorBulletSpawner.cpp
git commit -m "feat(M3): self-driving actor bullet spawner behind re.ActorBullets.Count (#45)"
```

---

### Task 3: 부하 확인 — 1000 / 5000 에서 죽지 않는다

**Files:** 없음 (실행 검증만). 코드가 바뀌면 안 된다 — 느린 건 결론이지 버그가 아니다.

**Interfaces:**
- Consumes: CVar `re.ActorBullets.Count`, 로그 `[RE] ActorBulletProbe` (Task 2)
- Produces: 없음 (관측 결과만)

- [ ] **Step 1: Count 1000 헤드리스**

Run:
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.ActorBullets.Count 1000" -log=RE_probe45_1000.log &
sleep 45
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
grep "ActorBulletProbe" "Saved/Logs/RE_probe45_1000.log" | tail -5
grep -iE "fatal|assertion|crash" "Saved/Logs/RE_probe45_1000.log" | head -5
```
Expected: `live` 가 900~1100 근처에서 안정. fatal/assertion/crash 매치 0줄.

- [ ] **Step 2: Count 5000 헤드리스**

Run:
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.ActorBullets.Count 5000" -log=RE_probe45_5000.log &
sleep 90
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
grep "ActorBulletProbe" "Saved/Logs/RE_probe45_5000.log" | tail -5
grep -iE "fatal|assertion|crash" "Saved/Logs/RE_probe45_5000.log" | head -5
```
Expected: 크래시 없음. `live` 가 수천 대로 올라간다. **프레임레이트가 떨어져 `live`가 5000에 못 미치거나 프로브 줄 간격이 벌어지는 건 정상이다** — 타이머가 프레임에 종속되기 때문이고, 그 느려짐 자체가 이 이슈의 결론이다. 합격 기준은 오직 "크래시 없이 실행된다".

- [ ] **Step 3: 관측 결과를 이슈에 코멘트**

```bash
gh issue comment 45 --body "부하 확인: Count=1000 live≈<관측값> 안정, Count=5000 크래시 없이 실행(프레임 저하 관측 — 의도된 결론). 로그: Saved/Logs/RE_probe45_1000.log, RE_probe45_5000.log"
```
`<관측값>` 은 Step 1에서 실제로 읽은 숫자로 바꿔 넣는다.

---

### Task 4: 시각 검증 — 실 RHI 스크린샷 (임시 프로브, 커밋 안 함)

**Files:**
- 임시 수정: `Source/Project_RE/Baseline/REActorBulletSpawner.cpp` (검증 후 `git checkout` 으로 되돌린다 — 절대 커밋하지 않는다)

**Interfaces:**
- Consumes: Task 2의 `Fire()` / `FireCount`
- Produces: 없음 (PNG 1장)

`-nullrhi`는 렌더를 안 하므로 시각 검증에 못 쓴다. 실 RHI(`-windowed`)로 띄우고, 5초 시점에 스크린샷을 1장 찍는 임시 코드를 넣었다 뺀다.

- [ ] **Step 1: 임시 스크린샷 프로브 삽입**

`REActorBulletSpawner.cpp` 최상단 include 목록에 추가:

```cpp
#include "UnrealClient.h"   // ★임시 — 검증 후 제거
```

`Fire()` 의 프로브 로그 블록 바로 아래(`UE_LOG(... ActorBulletProbe ...)` 다음 줄, 함수 끝 `}` 앞)에 추가:

```cpp
	// ★임시 시각 검증 프로브 (#45) — 커밋 금지. 50회 발사 = 5초 시점에 1장.
	if (FireCount == 50)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("RE_actor_bullets"), /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
	}
```

- [ ] **Step 2: 빌드**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Build succeeded`.

- [ ] **Step 3: 실 RHI 실행 후 스크린샷 확보**

Run:
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.ActorBullets.Count 100" -log=RE_shot45.log &
sleep 25
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
ls Saved/Screenshots/Windows*/
```
Expected: `RE_actor_bullets.png` 존재.

- [ ] **Step 4: PNG 확인**

Read 툴로 `Saved/Screenshots/Windows*/RE_actor_bullets.png` 를 연다.

Expected: **빨간 구체**들이 원점 주위에 나선/링 형태로 퍼져 있다 (Mass 탄막과 같은 색·같은 크기). 회색 구체면 = MID 공유 실패, 구체가 안 보이면 = 스케일/메시 로드 실패.

주의: Mass 탄막(480발)도 같은 화면에 함께 떠 있다. 액터 탄환은 스폰 원점이 같고 색·크기도 같아 **구분이 안 되는 게 정상이자 합격 신호**다 ("시각적으로 동일"). 구분해서 세는 건 목적이 아니다.

- [ ] **Step 5: 임시 프로브 제거**

```bash
git checkout -- Source/Project_RE/Baseline/REActorBulletSpawner.cpp
git status --short
```
Expected: `git status` 깨끗함 (Baseline 파일에 수정 없음). 스크린샷 코드는 리포에 남지 않는다.

- [ ] **Step 6: 리빌드로 원상복구 확인**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Build succeeded`.

---

### Task 5: 마무리 — 이슈 코멘트 + PR

**Files:** 없음

- [ ] **Step 1: 스펙/플랜이 커밋됐는지 확인**

```bash
git log --oneline dev..HEAD
```
Expected: 스펙 커밋 + Task 1 커밋 + Task 2 커밋 + 플랜 커밋이 보인다.

- [ ] **Step 2: #44 선행 의존을 이슈에 기록**

```bash
gh issue comment 45 --body "설계 중 확인: 이슈 본문이 전제한 re.Bullets.Count(Mass off 스위치)는 코드에 존재하지 않는다. Mass 데모 탄막은 REGameMode::BeginPlay 타이머로 항상 돈다. 두 경로 배타 실행은 #44가 Mass CVar를 만든 뒤에 가능하다 — 실수치 비교(#46)의 선행 조건. 이 이슈의 완료 조건은 Mass가 함께 돌아도 전부 검증된다."
```

- [ ] **Step 3: 브랜치 푸시 + PR 생성**

PR은 `pr_creation_convention` 규칙을 따른다: base = `dev`, 이슈 #45의 label/milestone/assignee/project를 미러링, Reviewer 생략.

```bash
git push -u origin feature/M3-actor-baseline
gh pr create --base dev --title "feat(M3): Actor 기반 탄환 베이스라인 (Mass vs Actor 비교군) (#45)" --body "..."
```
PR 본문에는 프로브 결과(Count 0 / 100 / 1000 / 5000 관측값)와 스크린샷 확인 결과를 넣는다.

---

## Self-Review

- **Spec coverage:** D1(Count=동시유지수 역산) → T2 Step2 `PerShotAccum`. D2(원점 하드코딩) → T2 `SpawnOrigin`. D3(Mass 배타는 범위 밖) → Global Constraints + T5 Step2 이슈 코멘트. D4(WorldSubsystem+타이머) → T2 `OnWorldBeginPlay`. D5(공유 MID) → T2 `SharedMID` + T1 `Init`. 완료조건 4개 → T2 Step4(Count 0), T2 Step5(Count 100), T3(1000/5000), T4(시각). Build.cs → T1 Step3. 갭 없음.
- **Placeholder scan:** 코드 블록 전부 완전. T3 Step3의 `<관측값>`은 실행 결과를 넣으라는 명시적 지시라 placeholder 아님. T5 Step3의 PR body `"..."`는 프로브 결과를 넣으라고 바로 아래 문장에 명시.
- **Type consistency:** `Init(const FVector&, float, UMaterialInterface*)` — T1 선언 ↔ T2 호출 일치. `SharedMID`는 `TObjectPtr<UMaterialInstanceDynamic>` → `UMaterialInterface*` 로 암시 변환(상속) OK. `FBulletSpawnParams{Location, Velocity, Lifetime}` = `REBulletSpawnSubsystem.h` 실제 필드. `FSpiralParams{Count, BaseAngleDeg, AngleStepDeg, Speed, Lifetime}` = `REBulletPatternGenerator.h:15-22` 실제 필드. `SetTimer(FTimerHandle&, UserClass*, void(UserClass::*)(), float, bool)` = 엔진 오버로드(UObject 파생 필요 — `UWorldSubsystem`은 UObject).
- **소유권:** 수정 파일은 `Baseline/**` 신규 4개 + `Project_RE.Build.cs` 1줄. `Core/`·`Mass/`·`REGameMode` 무수정 → #43/#44와 충돌 0.