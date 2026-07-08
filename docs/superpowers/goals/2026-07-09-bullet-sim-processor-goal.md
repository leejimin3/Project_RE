# 구현 목표: M1 #15 — 이동/수명 Processor (SimProcessor Execute)

## 컨텍스트

UE 5.8 C++ 탑뷰 탄막(bullet-hell) 프로젝트. MassEntity ECS로 탄막을 시뮬한다. 이슈 **#15** (마일스톤 **M1: Mass 보스 탄막 스폰 + 이동**).

이 goal이 하는 것: `UREBulletSimProcessor`의 스텁(`ConfigureQueries`/`Execute`)을 실제 이동/수명 로직으로 채운다. 매 틱 탄환 위치를 Velocity로 전진, Lifetime 감소, 소진 시 지연 파괴. 그리고 GameMode(기존 프로브 하네스)에 nonzero velocity/lifetime 테스트 탄환 1발 + Tick readback 로그를 추가해 헤드리스로 이동+파괴를 실증한다.

스코프 밖(손대지 말 것): 패턴 Velocity 수학(#16), ISM 렌더(#17), 병렬 순회, 경계 컬링.

설계 스펙: docs/superpowers/specs/2026-07-09-bullet-sim-processor-design.md
상세 플랜: docs/superpowers/plans/2026-07-09-bullet-sim-processor.md
(참고 가능. 단 **아래 코드가 최종 정본** — spec/plan과 어긋나면 이 goal을 따른다.)

## 브랜치

현재 브랜치 `feature/M1-bullet-archetype-spawn` (dev에서 분기된 #14 후속) 위에서 계속 작업. PR base=dev.

## 전역 제약

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `FTransformFragment`는 MassCore(`Mass/EntityFragments.h`)에 있고 이미 의존.
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그 관측**.
- 로그 접두어 `[RE]` 고정. 클래스/타입명 스펙과 동일.
- YAGNI: 패턴 수학(#16), ISM 렌더(#17), 병렬 순회, 경계 컬링 전부 스코프 밖.

## 검증된 API (실물 확인됨 — UE 5.8, 파일:라인)

- `FMassEntityQuery::ForEachEntityChunk(FMassExecutionContext&, const FMassExecuteFunction&)` — `MassEntityQuery.h:89` (5.6+ EntityManager 인자 없는 시그니처; 구버전은 `UE_DEPRECATED(5.6)`)
- `FMassExecutionContext::GetDeltaTimeSeconds()` — `MassExecutionContext.h:429`
- `FMassExecutionContext::GetNumEntities()` — `MassExecutionContext.h:447`
- `FMassExecutionContext::GetEntity(int32)` — `MassExecutionContext.h:452`
- `FMassExecutionContext::GetMutableFragmentView<T>()` — `MassExecutionContext.h:630`
- `FMassExecutionContext::Defer()` → `FMassCommandBuffer&` — `MassExecutionContext.h:437`
- `FMassCommandBuffer::DestroyEntity(FMassEntityHandle)` — `MassCommandBuffer.h:344`
- `FMassEntityManager::IsEntityValid(FMassEntityHandle)` — `MassEntityManager.h:738`
- `FTransformFragment::GetMutableTransform()` / `GetTransform()` — `Mass/EntityFragments.h` (MassCore)
- `UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity, float Lifetime)` → `FMassEntityHandle` (#14 산출, `REBulletSpawnSubsystem.h`)
- `FMassEntityHandle::IsSet()` / `Reset()` — `MassEntityTypes.h`

## 기존 파일 현황 (변경 대상)

**`Source/Project_RE/Mass/REBulletSimProcessor.cpp`** (현재 — 스텁):
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"

UREBulletSimProcessor::UREBulletSimProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;  // 7: 싱글/서버/클라 모두 시뮬
}

void UREBulletSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadWrite);
}

void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// M0: 구조만. 실제 이동/수명 계산은 M1.
	UE_LOG(LogTemp, Verbose, TEXT("[RE] SimProcessor::Execute"));
}
```
헤더 `REBulletSimProcessor.h` 는 변경 없음 (`EntityQuery` 멤버 그대로).

**`Source/Project_RE/Mass/REBulletFragments.h`** (참고 — 이미 확정, 변경 없음): `FBulletSimFragment{ FVector Velocity; float Lifetime; }`, `FBulletRenderFragment{ int32 InstanceIndex; }`, `FBulletTag : FMassTag`.

**#14 Archetype** (참고): 탄환 = `FTransformFragment` + `FBulletSimFragment` + `FBulletRenderFragment` + `FBulletTag`. `UREBulletSpawnSubsystem::SpawnBullet`으로 스폰됨.

**`Source/Project_RE/Core/REGameMode.h`** (현재): `AGameModeBase` 상속, `AREGameMode()` + `BeginPlay()` override만. `#include "MassEntityTypes.h"` 이미 존재. Tick 없음.

**`Source/Project_RE/Core/REGameMode.cpp`** (현재): constructor에서 DefaultPawn/PlayerController 지정. `BeginPlay()`에서 Mass 스모크 테스트 + Processor flags 로그 + Boss 스폰 후 `TriggerBulletPattern(Spiral,12345,0)`. Boss 스폰 탄환은 vel=0/life=0 (패턴 수학 #16 전) → 이동 관측 불가.

================================================================
## TASK 1: SimProcessor Execute — 이동/수명 로직
================================================================

수정 파일: `Source/Project_RE/Mass/REBulletSimProcessor.cpp` (헤더 불변, 1파일).

### 1-1. `Source/Project_RE/Mass/REBulletSimProcessor.cpp` (수정)

파일 전문을 아래로 교체:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

UREBulletSimProcessor::UREBulletSimProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;  // 7: 싱글/서버/클라 모두 시뮬
}

void UREBulletSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
}

void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
	{
		const float Dt = Context.GetDeltaTimeSeconds();
		const int32 Num = Context.GetNumEntities();
		const TArrayView<FTransformFragment> Transforms = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FBulletSimFragment> Sims       = Context.GetMutableFragmentView<FBulletSimFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			FBulletSimFragment& Sim = Sims[i];
			Transforms[i].GetMutableTransform().AddToTranslation(Sim.Velocity * Dt);
			Sim.Lifetime -= Dt;
			if (Sim.Lifetime <= 0.f)
			{
				Context.Defer().DestroyEntity(Context.GetEntity(i));
			}
		}
	});
}
```

### 1-2. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-3. 커밋

```bash
git add Source/Project_RE/Mass/REBulletSimProcessor.cpp
git commit -m "feat(M1): implement bullet sim move/lifetime in UREBulletSimProcessor (#15)"
```

================================================================
## TASK 2: GameMode 프로브 배선 + headless 검증
================================================================

프로덕션 스폰 경로는 vel=0/life=0(패턴 수학 #16 전)이라 이동을 관측할 수 없다. `AREGameMode`(기존 프로브 하네스)에 nonzero velocity/lifetime 테스트 탄환 1발을 스폰하고 `Tick`에서 Location/생존을 readback 로깅한다.

수정 파일: `Source/Project_RE/Core/REGameMode.h`, `Source/Project_RE/Core/REGameMode.cpp`.

### 2-1. `Source/Project_RE/Core/REGameMode.h` (수정)

`AREGameMode` 클래스 본문을 아래로 교체 (Tick override + 프로브 멤버 추가). 파일 나머지(FRETestFragment, include)는 그대로:

```cpp
UCLASS()
class AREGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AREGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// #15 프로브 전용: 이동/수명 관측용 테스트 탄환. #17 데모 씬에서 제거 예정.
	FMassEntityHandle ProbeBullet;
	float ProbeElapsed = 0.f;
};
```

### 2-2. `Source/Project_RE/Core/REGameMode.cpp` (수정)

파일 전문을 아래로 교체:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "REBulletSimProcessor.h"
#include "REBulletRenderProcessor.h"
#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
}

void AREGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Mass 스모크 테스트: 서브시스템 얻고 엔티티 1개 생성 → 로그.
	// GameMode는 서버 권위라 HasAuthority 가드 불필요.
	if (UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
	{
		FMassEntityManager& EM = Mass->GetMutableEntityManager();
		FMassArchetypeHandle Arch = EM.CreateArchetype({ FRETestFragment::StaticStruct() });
		FMassEntityHandle E = EM.CreateEntity(Arch);
		UE_LOG(LogTemp, Log, TEXT("[RE] Mass entity created: Index=%d Serial=%d"), E.Index, E.SerialNumber);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] UMassEntitySubsystem NULL"));
	}

	// #4 검증: 두 Processor CDO의 ExecutionFlags 확인. Sim=7(AllNetModes), Render=5(Standalone|Client).
	const uint8 SimFlags    = (uint8)GetDefault<UREBulletSimProcessor>()->GetExecutionFlags();
	const uint8 RenderFlags = (uint8)GetDefault<UREBulletRenderProcessor>()->GetExecutionFlags();
	UE_LOG(LogTemp, Log, TEXT("[RE] SimProcessor flags=%d  RenderProcessor flags=%d"), SimFlags, RenderFlags);

	// #5 검증: 보스 스폰 후 탄막 트리거 → 싱글 경로 스폰 카운트 실증.
	// AlwaysSpawn: 원점 캡슐 충돌로 스폰 실패하는 것 방지 (검증용 보스라 위치 무관).
	FActorSpawnParameters BossSpawnParams;
	BossSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(
			AREBossCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, BossSpawnParams))
	{
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
	}

	// #15 프로브: nonzero velocity/lifetime 탄환 1발 → SimProcessor 이동/파괴 관측용.
	if (UREBulletSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>())
	{
		ProbeBullet = Spawner->SpawnBullet(FVector::ZeroVector, FVector(100.f, 0.f, 0.f), 0.5f);
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe spawn: Vel=(100,0,0) Life=0.50"));
	}
}

void AREGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ProbeBullet.IsSet())
	{
		return;
	}

	ProbeElapsed += DeltaSeconds;

	UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!Mass)
	{
		return;
	}
	FMassEntityManager& EM = Mass->GetMutableEntityManager();

	if (EM.IsEntityValid(ProbeBullet))
	{
		const FVector Loc = EM.GetFragmentDataChecked<FTransformFragment>(ProbeBullet).GetTransform().GetLocation();
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe: t=%.2f Loc=%s Alive=1"), ProbeElapsed, *Loc.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe: t=%.2f Alive=0 (destroyed)"), ProbeElapsed);
		ProbeBullet.Reset();  // 파괴 확인 후 로그 종료.
	}
}
```

### 2-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-4. headless 런타임 프로브 (Acceptance)

PIE 없이 headless. Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe15.log &
sleep 30
grep "\[RE\] SimProbe" "Saved/Logs/RE_probe15.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대 로그 패턴 (수치는 프레임레이트 의존, 경향이 핵심):
```
[RE] SimProbe spawn: Vel=(100,0,0) Life=0.50
[RE] SimProbe: t=0.03 Loc=X=3.000 Y=0.000 Z=0.000 Alive=1     ← Loc.X 증가
[RE] SimProbe: t=0.10 Loc=X=10.000 Y=0.000 Z=0.000 Alive=1
...
[RE] SimProbe: t=0.52 Alive=0 (destroyed)                     ← Life=0.5 소진 후 파괴
```
**합격 기준 2개 (둘 다 필수):**
1. **이동:** `Loc.X`가 시간에 비례해 증가 (≈ `100 * t`). Y/Z=0 유지.
2. **파괴:** `t≈0.5` 부근에서 `Alive=0 (destroyed)` 1회 출력, 이후 로그 종료.

### 2-5. 커밋

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "test(M1): add SimProcessor move/lifetime headless probe in GameMode (#15)"
```

## 완료 후

- PR 생성: base=**dev**. 이슈 #15 메타 미러링 — label `mass-entity`+`C++`, milestone `M1: Mass 보스 탄막 스폰 + 이동 (싱글)`, assignee(leejimin3), project. 6개 필드 전부 채움.
- 남은 의도된 TODO 마커: `// #15 프로브`(GameMode 검증 스캐폴딩, #17 데모 씬에서 제거), Boss의 `TODO M1(#16)`(패턴 수학), `TODO M5`(Multicast RPC).
- 후속 M1: #16(패턴 Velocity 수학), #17(ISM 렌더 + 데모 영상 = M1 마감).

## 하지 말 것 (스코프 밖)

- 패턴별 Velocity 수학(나선/부채꼴) → #16
- ISM 인스턴스 갱신/InstanceIndex → #17
- 병렬 순회(`ParallelForEachEntityChunk`) → 필요 시 M3
- 화면 밖(경계) 컬링 파괴 → 미요청, Lifetime만으로 충분
- SimProcessor 헤더(.h) / Build.cs / .uproject 변경 → 불필요
- GameMode 프로브 스캐폴딩 지금 제거 → M1 마감(#17)까지 존치 (회귀 검증 수단)
