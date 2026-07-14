# 구현 목표: [M3 / #45] Actor 기반 탄환 베이스라인 (Mass vs Actor 비교군)

## 컨텍스트

Project_RE는 UE 5.8 C++ 탄막(bullet-hell) 프로젝트다. Mass Entity 기반 탄환 경로가 이미 완성돼 있다(스폰/이동/ISM 렌더). M3 마일스톤 목표는 "CPU 시간 수치 기록 (Mass vs Actor 비교)"인데, **비교군인 Actor 기반 탄환이 코드에 없다.**

이 goal은 **최적화하지 않은 순진한 액터 탄환 경로**를 만든다 (이슈 #45, 마일스톤 M3). 최적화하면 비교가 무의미해진다 — 풀링·HISM·Tick 튜닝 전부 금지다. "액터로 짜면 보통 이렇게 짠다"의 정직한 버전이어야 공정한 비교가 된다.

산출물: CVar `re.ActorBullets.Count` (기본 0 = 비활성 → 평상시 게임 영향 0).

스코프 밖 (손대지 말 것): 액터 경로 최적화, 액터 탄환 피격 판정, 네트워크 복제, 실제 수치 수집·비교표(#46), Mass off 스위치 `re.Bullets.Count`(#44).

설계 스펙: `docs/superpowers/specs/2026-07-15-actor-bullet-baseline-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-15-actor-bullet-baseline.md`
(참고 가능. 단 **아래 코드가 최종 정본**이다.)

## 브랜치

`dev` 에서 분기: `feature/M3-actor-baseline`
워크트리: `E:\UnrealProjects\Project_RE-actor-baseline`
(이미 이 브랜치·워크트리에 있으면 그대로 진행. 스펙/플랜 커밋 2개는 이미 올라가 있다.)

## 전역 제약

- **`Source/Project_RE/Core/REGameMode.cpp/.h` 를 절대 수정하지 않는다.** #43 세션이 같은 파일을 뜯고 있다 — 정면충돌한다.
- **`Source/Project_RE/Mass/**` 를 수정하지 않는다.** #44 소유. `Mass/REBulletPatternGenerator.h` 는 읽기 전용 참조만 한다 (`GenerateSpiral` 재사용 — 패턴 수학을 복붙하지 않는다).
- 소유 파일: `Source/Project_RE/Baseline/**` (전부 신규), `Source/Project_RE/Project_RE.Build.cs` (이 파일은 #45만 건드린다).
- **액터 경로를 최적화하지 않는다.** 오브젝트 풀링·HISM 전환·Tick 그룹 튜닝 금지.
- 스포너는 자립해야 한다. GameMode의 BeginPlay/타이머에 얹지 않는다.
- 빌드 커맨드 (Git Bash):
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
    -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
- 헤드리스 프로브는 Git Bash에서 `MSYS_NO_PATHCONV=1` 필수 (경로 mangling 방지).
- Mass 경로는 이 브랜치에서 끌 수 없다 (스위치가 #44 소유). 프로브 로그에 Mass의 `RenderProbe`/`SimProbe` 줄이 섞여 나오는 건 정상이다 — `ActorBulletProbe` 줄만 본다.

## 검증된 API (실물 확인됨)

Mass 기준값 — 전부 소스 대조 완료:

| 항목 | 값 | 출처 |
|---|---|---|
| 발사당 탄 수 | 16 | `Mass/REBulletPatternGenerator.h` `FSpiralParams::Count` |
| 각 간격 | 22.5° | 동 (360/16 균등 링) |
| 속도 | 300 uu/s | 동 `Speed` (기본값) |
| 수명 | 3 s | 동 `Lifetime` (기본값) |
| 발사 주기 | 0.1 s | `Core/REGameMode.cpp:67` `DemoFireTimer` |
| Spiral 회전 스텝 | +15°/발사 | `Core/REBossCharacter.h:58` `SpiralRotationStepDeg` |
| 스폰 원점 | `FVector(0, 0, 90)` | `Core/REGameMode.cpp:53` (Boss 스폰 위치, 안 움직임) |
| 메시 | `/Engine/BasicShapes/Sphere.Sphere` | `Mass/REBulletRenderSubsystem.cpp:33` |
| 스케일 | 0.5 | `Mass/REBulletRenderProcessor.cpp:14` `BulletScale` |
| 머티리얼 | `/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial` + MID `Color`=Red | `Mass/REBulletRenderSubsystem.cpp:37-43` |
| 콜리전 | `ECollisionEnabled::NoCollision` | `Mass/REBulletRenderSubsystem.cpp:30` (#34) |

정상 상태 Mass 탄환 수 = 16 × (3 s / 0.1 s) = **480발**.

재사용할 심볼:

```cpp
// Mass/REBulletPatternGenerator.h  (읽기 전용 참조)
namespace REBulletPattern
{
    struct FSpiralParams
    {
        int32 Count        = 16;
        float BaseAngleDeg = 0.f;
        float AngleStepDeg = 22.5f;
        float Speed        = 300.f;   // uu/s
        float Lifetime     = 3.f;     // s
    };
    TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P);
}

// Mass/REBulletSpawnSubsystem.h  (읽기 전용 참조)
struct FBulletSpawnParams   // 필드: Location, Velocity, Lifetime
```

엔진 API:
- `UWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)` — 헤더 `Subsystems/WorldSubsystem.h`
- `FTimerManager::SetTimer(FTimerHandle&, UserClass* InObj, void(UserClass::*InTimerMethod)(), float InRate, bool bLoop)` — 헤더 `TimerManager.h` (UserClass는 UObject 파생이어야 함 — `UWorldSubsystem` OK)
- `UMaterialInstanceDynamic::Create(UMaterialInterface* Parent, UObject* Outer)` — 헤더 `Materials/MaterialInstanceDynamic.h`
- `TActorIterator<T>(UWorld*)` — 헤더 `EngineUtils.h`
- `ConstructorHelpers::FObjectFinder<UStaticMesh>` — 헤더 `UObject/ConstructorHelpers.h`
- `TAutoConsoleVariable<int32>(TEXT("name"), default, TEXT("help"), ECVF_Cheat)` + `.GetValueOnGameThread()`
- `FScreenshotRequest::RequestScreenshot(const TCHAR* Name, bool bShowUI, bool bAddFilenameSuffix)` — 헤더 `UnrealClient.h` (TASK 4 임시 프로브에서만)

## 기존 파일 현황 (변경 대상)

- `Source/Project_RE/Baseline/` — **폴더 자체가 없다.** 전부 신규 생성.
- `Source/Project_RE/Project_RE.Build.cs` — `PublicIncludePaths.AddRange` 에 `"Project_RE"`, `"Project_RE/Core"`, `"Project_RE/Mass"`, `"Project_RE/UI"`, `Variant_*` 들이 이미 있다. `"Project_RE/Baseline"` 만 추가한다. `PublicDependencyModuleNames` 는 이미 `Core/CoreUObject/Engine/...` 전부 있어 변경 불요.
- CVar는 이 프로젝트에 **하나도 없다.** `re.ActorBullets.Count` 가 첫 CVar다. (이슈 본문이 언급한 `re.Bullets.Count` 는 존재하지 않는다 — #44 몫.)

================================================================
## TASK 1: AREBulletActor — 탄환 액터
================================================================

### 1-1. `Source/Project_RE/Baseline/REBulletActor.h` (신규)

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

### 1-2. `Source/Project_RE/Baseline/REBulletActor.cpp` (신규)

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

### 1-3. `Source/Project_RE/Project_RE.Build.cs` (수정)

`PublicIncludePaths.AddRange` 목록 맨 앞 `"Project_RE",` 다음 줄에 `"Project_RE/Baseline",` 한 줄만 추가한다. 수정 후 그 부분은 정확히 이렇게 된다:

```csharp
		PublicIncludePaths.AddRange(new string[] {
			"Project_RE",
			"Project_RE/Baseline",
			"Project_RE/Core",
			"Project_RE/Mass",
			"Project_RE/UI",
			"Project_RE/Variant_Platforming",
```

다른 줄·다른 섹션은 건드리지 않는다.

### 1-4. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```

기대 출력: `Build succeeded` (에러 0). 아직 아무도 이 액터를 스폰하지 않으므로 런타임 동작 변화는 없다.

### 1-5. 커밋

```bash
git add Source/Project_RE/Baseline/REBulletActor.h Source/Project_RE/Baseline/REBulletActor.cpp Source/Project_RE/Project_RE.Build.cs
git commit -m "feat(M3): add naive AREBulletActor for Mass-vs-Actor baseline (#45)"
```

================================================================
## TASK 2: UREActorBulletSpawner — 자립 스포너 + CVar
================================================================

### 2-1. `Source/Project_RE/Baseline/REActorBulletSpawner.h` (신규)

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

### 2-2. `Source/Project_RE/Baseline/REActorBulletSpawner.cpp` (신규)

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

### 2-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```

기대 출력: `Build succeeded` (에러 0).

### 2-4. 헤드리스 게이트 A — 기본값(0)에서 액터 0, 기존 게임 무변화

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe45_off.log &
sleep 30
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
grep -c "ActorBulletProbe" "Saved/Logs/RE_probe45_off.log"
grep -E "\[RE\] (RenderProbe|SimProbe)" "Saved/Logs/RE_probe45_off.log" | head -5
```

기대 출력:
- `grep -c "ActorBulletProbe"` → `0` (액터 경로 완전 침묵)
- `RenderProbe` / `SimProbe` 줄은 평소대로 출력 → 기존 Mass 게임 동작 무변화
- 크래시 없음

### 2-5. 헤드리스 게이트 B — Count 100 에서 ~100발 유지

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.ActorBullets.Count 100" -log=RE_probe45_100.log &
sleep 30
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
grep "ActorBulletProbe" "Saved/Logs/RE_probe45_100.log"
```

기대 출력: `[RE] ActorBulletProbe: live=... target=100 perShot=3` (또는 `perShot=4` — 3.33 누산이라 3/4가 번갈아 나온다). `live` 값이 0에서 증가해 **수명 3초가 지난 뒤 90~110 사이에서 안정**된다(±10%).

판정:
- 3초 이후 줄들의 `live` 가 전부 [90, 110] → **합격**
- `live` 가 계속 증가만 함 → 액터가 안 죽는 것 (Tick/Destroy 버그)
- `live` 가 0에 머묾 → 스폰 실패

### 2-6. 커밋

```bash
git add Source/Project_RE/Baseline/REActorBulletSpawner.h Source/Project_RE/Baseline/REActorBulletSpawner.cpp
git commit -m "feat(M3): self-driving actor bullet spawner behind re.ActorBullets.Count (#45)"
```

================================================================
## TASK 3: 부하 확인 — 1000 / 5000 에서 죽지 않는다
================================================================

코드 변경 없음. 실행 관측만 한다. **느린 건 버그가 아니라 결론이다 — 고치려 들지 마라.**

### 3-1. Count 1000

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

기대 출력: `live` 가 900~1100 근처에서 안정. `fatal|assertion|crash` 매치 **0줄**.

### 3-2. Count 5000

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

기대 출력: 크래시 없음(`fatal|assertion|crash` 0줄). `live` 가 수천 대로 올라간다.

**프레임레이트가 떨어져 `live`가 5000에 못 미치거나 프로브 줄 간격이 벌어지는 건 정상이다** — 타이머가 프레임에 종속되기 때문이고, 그 느려짐 자체가 이 이슈의 결론이다. 합격 기준은 오직 "크래시 없이 실행된다".

### 3-3. 관측 결과 이슈 코멘트

`<관측값>` 자리에 3-1/3-2에서 실제로 읽은 숫자를 넣는다.

```bash
gh issue comment 45 --body "부하 확인: Count=1000 live≈<관측값> 안정, Count=5000 크래시 없이 실행(프레임 저하 관측 — 의도된 결론). 로그: Saved/Logs/RE_probe45_1000.log, RE_probe45_5000.log"
```

================================================================
## TASK 4: 시각 검증 — 실 RHI 스크린샷 (임시 프로브, 커밋 금지)
================================================================

`-nullrhi`는 렌더를 안 하므로 시각 검증에 못 쓴다. 실 RHI(`-windowed`)로 띄우고 5초 시점에 1장 찍는 임시 코드를 넣었다 뺀다. **이 코드는 절대 커밋하지 않는다.**

### 4-1. 임시 프로브 삽입 (`Source/Project_RE/Baseline/REActorBulletSpawner.cpp`)

include 목록 맨 아래에 추가:

```cpp
#include "UnrealClient.h"   // ★임시 — 검증 후 제거
```

`Fire()` 의 프로브 로그 블록(`if ((FireCount++ % 10) == 0) { ... }`) **바로 아래**, 함수 닫는 `}` 앞에 추가:

```cpp
	// ★임시 시각 검증 프로브 (#45) — 커밋 금지. 50회 발사 = 5초 시점에 1장.
	if (FireCount == 50)
	{
		FScreenshotRequest::RequestScreenshot(TEXT("RE_actor_bullets"), /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
	}
```

### 4-2. 빌드

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```

기대 출력: `Build succeeded`.

### 4-3. 실 RHI 실행 → PNG 확보

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" /Game/Level/Main \
  -game -windowed -ResX=1280 -ResY=720 -unattended -nosplash -stdout -NoSound \
  -ExecCmds="re.ActorBullets.Count 100" -log=RE_shot45.log &
sleep 25
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
ls Saved/Screenshots/Windows*/
```

기대 출력: `RE_actor_bullets.png` 존재.

### 4-4. PNG 육안 판정

Read 툴로 `Saved/Screenshots/Windows*/RE_actor_bullets.png` 를 연다.

기대: **빨간 구체**들이 원점 주위에 나선/링 형태로 퍼져 있다 (Mass 탄막과 같은 색·같은 크기).
- 회색 구체 → 공유 MID 적용 실패
- 구체가 안 보임 → 스케일/메시 로드 실패

주의: Mass 탄막(480발)도 같은 화면에 함께 떠 있다. 액터 탄환은 원점·색·크기가 같아 **구분이 안 되는 게 정상이자 합격 신호**다("시각적으로 동일"). 구분해 세는 건 목적이 아니다.

### 4-5. 임시 프로브 제거 (필수)

```bash
git checkout -- Source/Project_RE/Baseline/REActorBulletSpawner.cpp
git status --short
```

기대 출력: `git status --short` 가 **빈 출력** (스크린샷 코드가 리포에 남지 않음).

### 4-6. 리빌드로 원상복구 확인

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-actor-baseline\Project_RE.uproject" -WaitMutex -NoHotReload
```

기대 출력: `Build succeeded`.

## 완료 후

### 이슈 코멘트 — #44 선행 의존 기록

```bash
gh issue comment 45 --body "설계 중 확인: 이슈 본문이 전제한 re.Bullets.Count(Mass off 스위치)는 코드에 존재하지 않는다. Mass 데모 탄막은 REGameMode::BeginPlay 타이머로 항상 돈다. 두 경로 배타 실행은 #44가 Mass CVar를 만든 뒤에 가능하다 — 실수치 비교(#46)의 선행 조건. 이 이슈의 완료 조건은 Mass가 함께 돌아도 전부 검증된다."
```

### PR 생성

`pr_creation_convention` 규칙을 따른다:
- base = `dev` (main 아님)
- 이슈 #45의 메타를 미러링: label(`architecture`, `C++`), milestone(`M3: 스케일 업 + 프로파일링`), assignee(`leejimin3`), project(`Project_RE 개발 로드맵`)
- Reviewer는 생략
- 본문에 프로브 관측값(Count 0 / 100 / 1000 / 5000)과 스크린샷 판정 결과를 넣는다

```bash
git push -u origin feature/M3-actor-baseline
gh pr create --base dev \
  --title "feat(M3): Actor 기반 탄환 베이스라인 (Mass vs Actor 비교군) (#45)" \
  --label architecture --label C++ \
  --milestone "M3: 스케일 업 + 프로파일링" \
  --assignee leejimin3 \
  --body "<프로브 결과 + 스크린샷 판정 포함>"
```

### 남은 의도된 TODO (후속 이슈 몫 — 지금 구현하지 않는다)

- Mass off 스위치 `re.Bullets.Count` → #44
- 실제 CPU 시간 수집·Mass vs Actor 비교표 → #46

## 하지 말 것 (스코프 밖)

- ❌ **액터 경로 최적화** — 오브젝트 풀링, HISM 전환, Tick 그룹 튜닝, Tick 간격 조절. 최적화하면 비교가 무의미해진다. 느린 게 결론이다.
- ❌ **`Core/REGameMode.*` 수정** — #43 세션이 같은 파일 작업 중. 스포너를 GameMode 타이머에 얹지 마라.
- ❌ **`Mass/**` 수정** — #44 소유. `GenerateSpiral`은 include해서 재사용만.
- ❌ **패턴 수학 복붙** — `REBulletPattern::GenerateSpiral` 재사용. Spiral 각도 계산을 Baseline에 다시 쓰지 마라.
- ❌ **액터 탄환 피격 판정** — 비교 대상은 스폰/이동/렌더 비용뿐.
- ❌ **액터 경로 네트워크 복제** — 싱글 측정 전용.
- ❌ **스크린샷 프로브 커밋** — TASK 4의 임시 코드는 반드시 `git checkout --` 으로 되돌린다.
- ❌ **`Fan`/`Homing` 패턴 지원** — Spiral만.
