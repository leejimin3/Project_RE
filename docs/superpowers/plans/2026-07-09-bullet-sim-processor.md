# M1 #15 이동/수명 Processor (SimProcessor Execute) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `UREBulletSimProcessor`의 스텁을 실제 이동/수명 로직으로 채운다. 매 틱 위치를 Velocity로 전진, Lifetime 감소, 소진 시 지연 파괴. 헤드리스 프로브로 이동+파괴를 시간 경과로 실증.

**Architecture:** `ConfigureQueries`에 `FTransformFragment`(RW) + `FBulletTag`(All 필터)를 추가하고, `Execute`에서 `ForEachEntityChunk`로 청크를 순회하며 Transform 전진 + Lifetime 감소 + 만료 지연 파괴. 검증은 `AREGameMode`(기존 프로브 하네스)에 nonzero velocity/lifetime 테스트 탄환 1발을 스폰하고 `Tick`에서 Location/생존을 readback 로깅하는 프로브 스캐폴딩으로 수행.

**Tech Stack:** UE 5.8 C++, MassEntity/MassCore 엔진 모듈 (이미 Build.cs 의존). 신규 모듈·플러그인 없음.

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `FTransformFragment`는 MassCore(`Mass/EntityFragments.h`)에 있고 이미 의존.
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그 관측** (`[[headless-runtime-probe]]`).
- 로그 접두어 `[RE]` 고정. 클래스/타입명 스펙과 동일.
- 브랜치: `feature/M1-bullet-archetype-spawn` (현재 브랜치, #14 후속). PR base=dev.
- YAGNI: 패턴 수학(#16), ISM 렌더(#17), 병렬 순회, 경계 컬링 전부 스코프 밖.

**검증된 API (UE 5.8 실물 — 파일:라인):**
- `FMassEntityQuery::ForEachEntityChunk(FMassExecutionContext&, const FMassExecuteFunction&)` — `MassEntityQuery.h:89` (5.6+ EntityManager 인자 없는 시그니처; 구버전은 `UE_DEPRECATED(5.6)`)
- `FMassExecutionContext::GetDeltaTimeSeconds()` — `MassExecutionContext.h:429`
- `FMassExecutionContext::GetNumEntities()` — `MassExecutionContext.h:447`
- `FMassExecutionContext::GetEntity(int32)` — `MassExecutionContext.h:452`
- `FMassExecutionContext::GetMutableFragmentView<T>()` — `MassExecutionContext.h:630`
- `FMassExecutionContext::Defer()` → `FMassCommandBuffer&` — `MassExecutionContext.h:437`
- `FMassCommandBuffer::DestroyEntity(FMassEntityHandle)` — `MassCommandBuffer.h:344`
- `FMassEntityManager::IsEntityValid(FMassEntityHandle)` — `MassEntityManager.h:738`
- `FTransformFragment::GetMutableTransform()` / `GetTransform()` — `Mass/EntityFragments.h` (MassCore)

---

### Task 1: SimProcessor Execute — 이동/수명 로직

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletSimProcessor.cpp` (헤더 불변)

**Interfaces:**
- Consumes: 기존 `FBulletSimFragment`(Velocity, Lifetime), `FBulletTag`, 엔진 `FTransformFragment`. #14 Archetype이 이 3요소를 이미 포함.
- Produces: 없음(런타임 동작). 후속 #17 RenderProcessor가 갱신된 `FTransformFragment`를 읽어 ISM 렌더.

- [ ] **Step 1: include 추가**

`REBulletSimProcessor.cpp` 상단 include 블록에 추가:
```cpp
#include "Mass/EntityFragments.h"  // FTransformFragment
```
(기존 `REBulletSimProcessor.h`, `REBulletFragments.h`, `MassExecutionContext.h` 유지.)

- [ ] **Step 2: ConfigureQueries 교체**

현재 본문(`FBulletSimFragment` RW 1줄)을 아래로 교체:
```cpp
void UREBulletSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
}
```

- [ ] **Step 3: Execute 교체**

현재 스텁 본문(UE_LOG 1줄)을 아래로 교체:
```cpp
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

- [ ] **Step 4: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletSimProcessor.cpp
git commit -m "feat(M1): implement bullet sim move/lifetime in UREBulletSimProcessor (#15)"
```

---

### Task 2: GameMode 프로브 배선 + headless 검증

프로덕션 스폰 경로는 vel=0/life=0(패턴 수학 #16 전)이라 이동을 관측할 수 없다. `AREGameMode`(기존 프로브 하네스)에 nonzero velocity/lifetime 테스트 탄환 1발을 스폰하고 `Tick`에서 Location/생존을 readback 로깅한다.

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h` (Tick + 프로브 멤버)
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (테스트 탄환 스폰 + Tick readback)

**Interfaces:**
- Consumes: Task 1의 SimProcessor 동작, `UREBulletSpawnSubsystem::SpawnBullet`(#14), `FMassEntityManager::IsEntityValid` + `GetFragmentDataChecked<FTransformFragment>`.
- Produces: 없음(검증 스캐폴딩). headless 로그로 이동+파괴 실증.

- [ ] **Step 1: `REGameMode.h` — Tick + 프로브 멤버 추가**

`AREGameMode` 클래스에 추가 (constructor에서 tick 활성화, Tick override, 프로브 핸들/누적시간):
```cpp
public:
	AREGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// #15 프로브 전용: 이동/수명 관측용 테스트 탄환. #17 데모 씬에서 제거 예정.
	FMassEntityHandle ProbeBullet;
	float ProbeElapsed = 0.f;
```
헤더 상단 include에 `MassEntityTypes.h`는 이미 존재(FRETestFragment용) → `FMassEntityHandle` 사용 가능.

- [ ] **Step 2: `REGameMode.cpp` — constructor tick 활성화**

`AREGameMode::AREGameMode()` 본문에 추가:
```cpp
	PrimaryActorTick.bCanEverTick = true;
```

- [ ] **Step 3: `REGameMode.cpp` — BeginPlay 끝에 테스트 탄환 스폰**

`BeginPlay()` 맨 끝(Boss 트리거 뒤)에 추가. include 상단에 `#include "REBulletSpawnSubsystem.h"` 추가:
```cpp
	// #15 프로브: nonzero velocity/lifetime 탄환 1발 → SimProcessor 이동/파괴 관측용.
	if (UREBulletSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>())
	{
		ProbeBullet = Spawner->SpawnBullet(FVector::ZeroVector, FVector(100.f, 0.f, 0.f), 0.5f);
		UE_LOG(LogTemp, Log, TEXT("[RE] SimProbe spawn: Vel=(100,0,0) Life=0.50"));
	}
```

- [ ] **Step 4: `REGameMode.cpp` — Tick readback**

새 함수 추가:
```cpp
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
`GetFragmentDataChecked<FTransformFragment>`용 include: `Mass/EntityFragments.h` 추가.
`FMassEntityHandle::IsSet()` / `Reset()` 은 `MassEntityTypes.h` 제공.

- [ ] **Step 5: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 6: headless 런타임 프로브 (Acceptance)**

PIE 없이 headless. Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe15.log &
sleep 30
grep "\[RE\] SimProbe" "Saved/Logs/RE_probe15.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected 로그 패턴 (수치는 프레임레이트 의존, 경향이 핵심):
```
[RE] SimProbe spawn: Vel=(100,0,0) Life=0.50
[RE] SimProbe: t=0.03 Loc=X=3.000 Y=0.000 Z=0.000 Alive=1     ← Loc.X 증가
[RE] SimProbe: t=0.10 Loc=X=10.000 Y=0.000 Z=0.000 Alive=1
...
[RE] SimProbe: t=0.52 Alive=0 (destroyed)                     ← Life=0.5 소진 후 파괴
```
**합격 기준 2개:**
1. **이동:** `Loc.X`가 시간에 비례해 증가 (≈ `100 * t`). Y/Z=0 유지.
2. **파괴:** `t≈0.5` 부근에서 `Alive=0 (destroyed)` 1회 출력, 이후 로그 종료.

둘 다 관측되면 SimProcessor 이동+수명 로직 검증 완료.

- [ ] **Step 7: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "test(M1): add SimProcessor move/lifetime headless probe in GameMode (#15)"
```

---

## 완료 후

- #15 완료 → 후속 M1: #16(패턴 Velocity 수학 — nonzero Velocity를 실제 패턴으로 주입), #17(ISM 렌더 + 데모 영상).
- **프로브 스캐폴딩 처리:** `AREGameMode`의 `ProbeBullet`/`Tick` readback은 `// #15 프로브` 마커로 커밋 유지. #17 데모 씬 구성 시 GameMode 프로브 경로 제거(실제 보스 발사 루프로 대체). 지금 제거하면 회귀 검증 수단이 사라지므로 M1 마감까지 존치.
- #16이 Boss 스폰의 Velocity=0 → 실제 패턴 속도로 교체하면, 프로덕션 탄환도 이동하게 되어 GameMode 테스트 탄환 의존이 줄어듦.

## Self-Review

- **Spec coverage:** 스펙 §변경범위1 ConfigureQueries→T1 Step2, §변경범위2 Execute→T1 Step3, §검증(프로브 이동+파괴)→T2 Step6. 갭 없음.
- **Placeholder scan:** 코드 블록 전부 완전. 잔존 `// #15 프로브` 마커는 스펙이 명시한 의도된 검증 스캐폴딩(작업 누락 아님).
- **Type consistency:** `ForEachEntityChunk(Context, lambda)` 시그니처 = 엔진 5.6+ 실물. 청크 뷰 `TArrayView<FTransformFragment>`/`TArrayView<FBulletSimFragment>` 동일 인덱스 정렬, `GetEntity(i)`도 동일 인덱스 → 파괴 핸들 정확. `SpawnBullet(FVector,FVector,float)` 시그니처(#14) ↔ T2 호출 `(ZeroVector, (100,0,0), 0.5f)` 일치. `IsEntityValid`/`GetFragmentDataChecked<FTransformFragment>` 실물 검증됨.
- **지연 파괴 안전성:** `Defer().DestroyEntity`는 Execute 종료 후 flush → 순회 중 뷰 무효화 없음. 다음 틱부터 `IsEntityValid=false` → 프로브가 파괴 관측.
- **Include 검증:** `Mass/EntityFragments.h`(FTransformFragment, MassCore), `MassExecutionContext.h`(Context API), `MassEntityTypes.h`(FMassEntityHandle IsSet/Reset — REGameMode.h 기존 include), `REBulletSpawnSubsystem.h`(#14 산출) — 전부 실물 확인.
