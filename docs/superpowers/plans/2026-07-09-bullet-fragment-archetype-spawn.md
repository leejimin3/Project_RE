# M1 #14 탄환 Fragment 확정 + FBulletTag + Archetype 스폰 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 탄환 엔티티의 데이터 레이아웃(FTransformFragment + FBulletSimFragment + FBulletRenderFragment + FBulletTag)을 확정하고, `UREBulletSpawnSubsystem`으로 초기값이 주입된 탄환을 배치 스폰하는 재사용 경로를 만든다.

**Architecture:** 신규 `FBulletTag`(FMassTag)로 탄환을 Query 필터링 가능하게 하고, 신규 `UWorldSubsystem`이 탄환 Archetype을 lazy 캐싱하며 `SpawnBullet`/`SpawnBulletBatch`로 EntityManager 경유 스폰 + Fragment 초기화를 담당한다. 기존 `AREBossCharacter::TriggerBulletPattern`의 인라인 스폰 루프를 이 subsystem 호출로 교체한다.

**Tech Stack:** UE 5.8 C++, MassEntity/MassCore 엔진 모듈 (이미 Build.cs 의존). 신규 모듈·플러그인 없음.

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `FTransformFragment`는 MassCore(`Mass/EntityFragments.h`)에 있고 MassCore는 이미 의존. MassCommon/MassGameplay 플러그인 불필요.
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그 관측**.
- 로그 접두어 `[RE]` 고정. 클래스/타입명 스펙과 동일.
- API 매크로(`PROJECT_RE_API`) 불필요 — 게임 모듈 단일.
- 브랜치: `feature/M1-bullet-archetype-spawn` (dev에서 분기). PR base=dev.
- YAGNI: 이동/수명(#15), 패턴 수학(#16), ISM 렌더(#17), 배치 최적화 API 전부 스코프 밖.

**검증된 API (UE 5.8 실물):**
- `FMassEntityManager::CreateArchetype(TConstArrayView<const UScriptStruct*>)` → `FMassArchetypeHandle` (Fragment+Tag 혼합 리스트 허용)
- `FMassEntityManager::CreateEntity(const FMassArchetypeHandle&)` → `FMassEntityHandle`
- `FMassEntityManager::GetFragmentDataChecked<T>(FMassEntityHandle)` → `T&`
- `FTransformFragment::GetMutableTransform()` → `FTransform&` (`Mass/EntityFragments.h`, MassCore)
- `FMassArchetypeHandle::IsValid()` → `bool`
- EntityManager 획득: `GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager()`

---

### Task 1: FBulletTag + UREBulletSpawnSubsystem

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletFragments.h` (FBulletTag 추가)
- Create: `Source/Project_RE/Mass/REBulletSpawnSubsystem.h`
- Create: `Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp`

**Interfaces:**
- Consumes: 기존 `FBulletSimFragment`, `FBulletRenderFragment` (REBulletFragments.h). 엔진 `FTransformFragment`, `UMassEntitySubsystem`, `FMassEntityManager`.
- Produces:
  - `struct FBulletTag : public FMassTag`
  - `struct FBulletSpawnParams { FVector Location; FVector Velocity; float Lifetime; }`
  - `UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity, float Lifetime) -> FMassEntityHandle`
  - `UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params) -> void`
  - Task 2(Boss)가 `GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>()`로 소비.

- [ ] **Step 1: `REBulletFragments.h`에 FBulletTag 추가**

`Source/Project_RE/Mass/REBulletFragments.h`의 마지막 `};`(FBulletRenderFragment 닫는 줄) 뒤에 삽입:

```cpp

/** 탄환 식별 태그. Query 필터 전용(데이터 없음). 후속 Processor가 이 태그로 탄환만 선별. */
USTRUCT()
struct FBulletTag : public FMassTag
{
	GENERATED_BODY()
};
```

- [ ] **Step 2: `REBulletSpawnSubsystem.h` 생성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "REBulletSpawnSubsystem.generated.h"

/** 탄환 1발 스폰 파라미터. UStruct 아님 — 함수 인자 전용 경량 구조체. */
struct FBulletSpawnParams
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float   Lifetime = 0.f;
};

/**
 *  탄환 스폰 단일 진입점. 탄환 Archetype을 lazy 캐싱하고 EntityManager로 스폰 + Fragment 초기화.
 *  Boss/패턴 제너레이터(#16)가 GetSubsystem으로 접근한다. 이동/렌더는 후속 이슈.
 */
UCLASS()
class UREBulletSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 탄환 1발 스폰 + Fragment 초기값 주입. EntityManager 없으면 무효 핸들 반환. */
	FMassEntityHandle SpawnBullet(FVector Location, FVector Velocity, float Lifetime);

	/** N발 배치 스폰. 내부는 SpawnBullet 루프(배치 최적화는 YAGNI). */
	void SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params);

private:
	/** 탄환 Archetype 최초 스폰 시 1회 생성·캐싱. */
	void EnsureArchetype(FMassEntityManager& EntityManager);

	/** 같은 World의 UMassEntitySubsystem에서 EntityManager 획득. 없으면 nullptr. */
	FMassEntityManager* GetEntityManager() const;

	FMassArchetypeHandle BulletArchetype;
};
```

- [ ] **Step 3: `REBulletSpawnSubsystem.cpp` 생성**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSpawnSubsystem.h"
#include "REBulletFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

FMassEntityManager* UREBulletSpawnSubsystem::GetEntityManager() const
{
	UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	return Mass ? &Mass->GetMutableEntityManager() : nullptr;
}

void UREBulletSpawnSubsystem::EnsureArchetype(FMassEntityManager& EntityManager)
{
	if (BulletArchetype.IsValid())
	{
		return;
	}
	BulletArchetype = EntityManager.CreateArchetype({
		FTransformFragment::StaticStruct(),
		FBulletSimFragment::StaticStruct(),
		FBulletRenderFragment::StaticStruct(),
		FBulletTag::StaticStruct() });
}

FMassEntityHandle UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity, float Lifetime)
{
	FMassEntityManager* EM = GetEntityManager();
	if (!EM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] SpawnBullet: EntityManager NULL"));
		return FMassEntityHandle();
	}

	EnsureArchetype(*EM);
	FMassEntityHandle Entity = EM->CreateEntity(BulletArchetype);

	EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Location);
	FBulletSimFragment& Sim = EM->GetFragmentDataChecked<FBulletSimFragment>(Entity);
	Sim.Velocity = Velocity;
	Sim.Lifetime = Lifetime;
	// FBulletRenderFragment.InstanceIndex는 기본값 INDEX_NONE 유지 (#17에서 할당).

	return Entity;
}

void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
	for (const FBulletSpawnParams& P : Params)
	{
		SpawnBullet(P.Location, P.Velocity, P.Lifetime);
	}
}
```

- [ ] **Step 4: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0. (신규 .h/.cpp 인식 안 되면 `Project_RE.uproject` 우클릭 → Generate VS project files 후 재빌드.)

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletFragments.h Source/Project_RE/Mass/REBulletSpawnSubsystem.h Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp
git commit -m "feat(M1): add FBulletTag + UREBulletSpawnSubsystem (#14)"
```

---

### Task 2: Boss 배선 + headless 검증

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp` (인라인 스폰 → subsystem 호출)
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (검증 로그 확인용 — 변경 없을 수도. 아래 참고)

**Interfaces:**
- Consumes: Task 1의 `UREBulletSpawnSubsystem::SpawnBulletBatch`, `FBulletSpawnParams`.
- Produces: 없음(최종 통합). headless 로그로 스폰 수 + 첫 탄환 초기값 실증.

- [ ] **Step 1: `REBossCharacter.cpp` 수정 — 인라인 스폰을 subsystem 호출로 교체**

현재 `TriggerBulletPattern` 본문(Line 19-56)에서 EntityManager/Archetype 직접 조작 부분을 교체한다. include도 조정.

파일 상단 include 블록을 아래로 교체:
```cpp
#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
```
(`REBulletFragments.h`, `MassEntitySubsystem.h`, `MassEntityManager.h` include 3개 제거 — 이제 subsystem이 캡슐화.)

`TriggerBulletPattern` 함수 전체를 아래로 교체:
```cpp
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

	switch (Pattern)
	{
	case EBulletPattern::Spiral:
		// TODO M1(#16): 나선 — 각도 증분으로 Velocity 세팅.
		break;
	case EBulletPattern::Fan:
		// TODO M1(#16): 부채꼴 — 중심각 기준 좌우 분산 Velocity.
		break;
	case EBulletPattern::Homing:
		// TODO M1(#16): 호밍 — 타깃 방향 Velocity + 추적 플래그.
		break;
	}

	// #14: 보스 위치에서 N발 스폰. Velocity=0/Lifetime=0 (패턴 수학은 #16, 이동은 #15).
	TArray<FBulletSpawnParams> Params;
	Params.Reserve(BulletsPerPattern);
	for (int32 i = 0; i < BulletsPerPattern; ++i)
	{
		Params.Add({ GetActorLocation(), FVector::ZeroVector, 0.f });
	}
	Spawner->SpawnBulletBatch(Params);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities at %s"),
		(int32)Pattern, Seed, StartTime, BulletsPerPattern, *GetActorLocation().ToString());
}
```

(파일 상단 `namespace { constexpr int32 BulletsPerPattern = 16; }` 블록은 그대로 유지.)

- [ ] **Step 2: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 3: headless 런타임 프로브 (Acceptance)**

기존 GameMode BeginPlay가 Boss 스폰 + `TriggerBulletPattern(Spiral,12345,0)` 호출함. PIE 없이 headless로 로그 관측. Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe14.log &
sleep 30
grep "\[RE\]" "Saved/Logs/RE_probe14.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected 로그 포함:
```
[RE] Boss::TriggerBulletPattern: Pattern=0 Seed=12345 Start=0.00 -> spawned 16 entities at X=0.000 Y=0.000 Z=0.000
```
- `spawned 16 entities` → 배치 스폰 수 확인.
- `UREBulletSpawnSubsystem NULL` 경고가 **아니어야** 함 → subsystem 배선 확인.
- `EntityManager NULL` 경고가 **아니어야** 함 → Archetype 생성 + Fragment 초기화 경로 도달 확인.

> **Fragment 초기값 추가 관측(선택):** 위 로그의 `at X=... Z=...`가 보스 위치(=스폰 Location 인자)와 일치하면 Location 주입 경로 실증. Velocity=0/Lifetime=0은 이번 스코프 기본값이라 별도 로그 불필요.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M1): route boss bullet spawn through UREBulletSpawnSubsystem (closes #14)"
```

---

## 완료 후

- 브랜치 `feature/M1-bullet-archetype-spawn` → PR (base=dev, 이슈 #14 메타 미러링: label `mass-entity`,`C++` + milestone M1 + assignee + project, 6개 필드 전부).
- #14 완료 → 후속 M1: #15(이동/수명 Processor, FBulletTag로 Query), #16(패턴 Velocity 수학), #17(ISM 렌더 + 데모 영상).
- 남은 TODO 마커: `TODO M1(#16)`(패턴 수학), `TODO M5`(Multicast RPC).

## Self-Review

- **Spec coverage:** 스펙 §데이터레이아웃→T1(FBulletTag+Archetype 4요소), §컴포넌트1 FBulletTag→T1 Step1, §컴포넌트2 Subsystem→T1 Step2-3, §컴포넌트3 Boss수정→T2 Step1, §검증→T2 Step3. 갭 없음.
- **Placeholder scan:** TBD/TODO(작업지시) 없음. 코드 블록 전부 완전. 잔존 `TODO M1(#16)`/`TODO M5` 주석은 스펙이 명시한 의도된 후속 마커(작업 누락 아님).
- **Type consistency:** `FBulletSpawnParams`(T1 정의: Location/Velocity/Lifetime) → T2에서 `{ GetActorLocation(), FVector::ZeroVector, 0.f }` 순서 일치. `SpawnBulletBatch(TConstArrayView<FBulletSpawnParams>)` 시그니처 T1 정의 ↔ T2 호출 `TArray<FBulletSpawnParams>`(TConstArrayView 암시 변환) 일치. `GetFragmentDataChecked<FTransformFragment>().GetMutableTransform()` 실물 API 검증됨.
- **Include 검증:** `Subsystems/WorldSubsystem.h`(Engine Public), `Mass/EntityFragments.h`(MassCore, FTransformFragment), `MassEntitySubsystem.h`→`GetMutableEntityManager()` — 전부 실물 확인.
