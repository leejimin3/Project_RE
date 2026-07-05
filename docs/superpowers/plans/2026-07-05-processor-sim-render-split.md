# Mass Processor 시뮬/렌더 분리 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mass 탄막의 시뮬/렌더 Processor를 ExecutionFlags로 네트워크 역할 분리하는 구조를 만든다 (데디서버는 렌더 skip).

**Architecture:** `FMassFragment` 2개(Sim/Render)와 `UMassProcessor` 2개를 정의한다. SimProcessor는 `AllNetModes`(7), RenderProcessor는 `Standalone|Client`(5)로 ExecutionFlags 설정. M0은 구조만 — Execute는 로그 스텁, 런타임 tick 실증은 M1. 검증은 빌드 성공 + GameMode BeginPlay에서 CDO 플래그 로그 관측.

**Tech Stack:** UE 5.8 C++, MassEntity / MassCore 엔진 모듈 (이미 배선됨).

## Global Constraints

- 엔진: UE 5.8 (`E:\UE_5.8`). 프로젝트: `E:\UnrealProjects\Project_RE\Project_RE.uproject`.
- MassGameplay 플러그인 당기지 않음. `MassEntity`·`MassCore` 모듈만 사용 (Build.cs 모듈 의존 변경 금지).
- `ConfigureQueries` 시그니처: `(const TSharedRef<FMassEntityManager>& EntityManager)` (5.6+ 시그니처). 무인자 오버로드는 deprecated — 쓰지 말 것.
- ExecutionFlags 값: Standalone=1, Server=2, Client=4, AllNetModes=7, (Standalone|Client)=5.
- 자동화 테스트 인프라 없음. 태스크 게이트 = **빌드 성공**. 최종 게이트 = **PIE Output Log 관측**.
- 빌드 커맨드 (Git Bash에서):
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development \
    -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0.

---

### Task 1: Fragments + SimProcessor + Build.cs include path

**Files:**
- Create: `Source/Project_RE/Mass/REBulletFragments.h`
- Create: `Source/Project_RE/Mass/REBulletSimProcessor.h`
- Create: `Source/Project_RE/Mass/REBulletSimProcessor.cpp`
- Modify: `Source/Project_RE/Project_RE.Build.cs` (PublicIncludePaths에 `"Project_RE/Mass"` 추가)

**Interfaces:**
- Consumes: `FMassFragment`, `UMassProcessor`, `FMassEntityQuery`, `FMassEntityManager`, `FMassExecutionContext`, `EProcessorExecutionFlags`, `EMassFragmentAccess` (MassEntity 모듈).
- Produces:
  - `FBulletSimFragment { FVector Velocity; float Lifetime; }`
  - `FBulletRenderFragment { int32 InstanceIndex; }`
  - `class UREBulletSimProcessor : public UMassProcessor` — ctor sets `ExecutionFlags = 7`.

- [ ] **Step 1: Create Fragments header**

`Source/Project_RE/Mass/REBulletFragments.h`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "REBulletFragments.generated.h"

/** 탄막 시뮬 상태. 위치는 M1에서 FTransformFragment로 다룬다. */
USTRUCT()
struct FBulletSimFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Velocity = FVector::ZeroVector;
	float   Lifetime = 0.f;
};

/** 탄막 렌더 상태. M1에서 ISM 인스턴스 인덱스로 사용한다. */
USTRUCT()
struct FBulletRenderFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 InstanceIndex = INDEX_NONE;
};
```

- [ ] **Step 2: Create SimProcessor header**

`Source/Project_RE/Mass/REBulletSimProcessor.h`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REBulletSimProcessor.generated.h"

/**
 *  탄막 이동/수명 시뮬 Processor.
 *  ExecutionFlags = AllNetModes(7): 싱글/서버/클라 모두 실행 (시뮬은 어디서나 동일).
 *  M0은 구조만 — Execute는 스텁. 실제 이동은 M1.
 */
UCLASS()
class UREBulletSimProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREBulletSimProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
```

- [ ] **Step 3: Create SimProcessor cpp**

`Source/Project_RE/Mass/REBulletSimProcessor.cpp`:
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

- [ ] **Step 4: Add include path to Build.cs**

`Source/Project_RE/Project_RE.Build.cs`의 `PublicIncludePaths.AddRange` 배열에서 `"Project_RE/Core",` 줄 바로 뒤에 추가:
```csharp
			"Project_RE/Mass",
```

- [ ] **Step 5: Build to verify it compiles**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 6: Commit**

```bash
git add Source/Project_RE/Mass/REBulletFragments.h Source/Project_RE/Mass/REBulletSimProcessor.h Source/Project_RE/Mass/REBulletSimProcessor.cpp Source/Project_RE/Project_RE.Build.cs
git commit -m "feat(M0): add bullet fragments + sim processor (#4)"
```

---

### Task 2: RenderProcessor

**Files:**
- Create: `Source/Project_RE/Mass/REBulletRenderProcessor.h`
- Create: `Source/Project_RE/Mass/REBulletRenderProcessor.cpp`

**Interfaces:**
- Consumes: `FBulletSimFragment`, `FBulletRenderFragment` (Task 1), `UMassProcessor`, `FMassEntityQuery`, `EMassFragmentAccess`.
- Produces: `class UREBulletRenderProcessor : public UMassProcessor` — ctor sets `ExecutionFlags = 5`.

- [ ] **Step 1: Create RenderProcessor header**

`Source/Project_RE/Mass/REBulletRenderProcessor.h`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REBulletRenderProcessor.generated.h"

/**
 *  탄막 렌더(ISM 트랜스폼) Processor.
 *  ExecutionFlags = Standalone|Client(5): 표시할 화면이 있는 곳만 실행.
 *  데디서버(Server 넷모드)는 자동 skip — 서버권위 분리의 핵심.
 *  M0은 구조만 — Execute는 스텁. 실제 ISM 갱신은 M1.
 */
UCLASS()
class UREBulletRenderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREBulletRenderProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
```

- [ ] **Step 2: Create RenderProcessor cpp**

`Source/Project_RE/Mass/REBulletRenderProcessor.cpp`:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"

UREBulletRenderProcessor::UREBulletRenderProcessor()
	: EntityQuery(*this)
{
	// 5: 데디서버(Server) skip, 싱글/클라만 렌더
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);
}

void UREBulletRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadOnly);       // sim 위치 읽기
	EntityQuery.AddRequirement<FBulletRenderFragment>(EMassFragmentAccess::ReadWrite);   // 인스턴스 쓰기
}

void UREBulletRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	// M0: 구조만. 실제 ISM 트랜스폼 갱신은 M1.
	UE_LOG(LogTemp, Verbose, TEXT("[RE] RenderProcessor::Execute"));
}
```

- [ ] **Step 3: Build to verify it compiles**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: Commit**

```bash
git add Source/Project_RE/Mass/REBulletRenderProcessor.h Source/Project_RE/Mass/REBulletRenderProcessor.cpp
git commit -m "feat(M0): add bullet render processor with client-only flags (#4)"
```

---

### Task 3: GameMode CDO flag probe + PIE verification

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (include 2개 추가, BeginPlay에 플래그 로그 추가)

**Interfaces:**
- Consumes: `UREBulletSimProcessor`, `UREBulletRenderProcessor` (Tasks 1-2), `UMassProcessor::GetExecutionFlags()`.
- Produces: PIE Output Log 라인 `[RE] SimProcessor flags=7  RenderProcessor flags=5`.

- [ ] **Step 1: Add processor includes to REGameMode.cpp**

`Source/Project_RE/Core/REGameMode.cpp` 상단 include 블록에서 `#include "MassEntityManager.h"` 줄 바로 뒤에 추가:
```cpp
#include "REBulletSimProcessor.h"
#include "REBulletRenderProcessor.h"
```

- [ ] **Step 2: Add CDO flag log to BeginPlay**

`Source/Project_RE/Core/REGameMode.cpp`의 `BeginPlay` 안, 기존 Mass 스모크 테스트 `if/else` 블록 **뒤에** (닫는 `}` 다음, 함수 끝 `}` 앞에) 추가:
```cpp
	// #4 검증: 두 Processor CDO의 ExecutionFlags 확인. Sim=7(AllNetModes), Render=5(Standalone|Client).
	const uint8 SimFlags    = (uint8)GetDefault<UREBulletSimProcessor>()->GetExecutionFlags();
	const uint8 RenderFlags = (uint8)GetDefault<UREBulletRenderProcessor>()->GetExecutionFlags();
	UE_LOG(LogTemp, Log, TEXT("[RE] SimProcessor flags=%d  RenderProcessor flags=%d"), SimFlags, RenderFlags);
```

- [ ] **Step 3: Build to verify it compiles**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: PIE runtime verification**

1. UE 에디터로 `Project_RE.uproject` 열기.
2. Play (PIE) 실행.
3. Output Log에서 `[RE]` 필터.

Expected 로그:
```
[RE] Mass entity created: Index=0 Serial=<n>
[RE] SimProcessor flags=7  RenderProcessor flags=5
```
`flags=7` (Sim=AllNetModes), `flags=5` (Render=Standalone|Client) 확인 → Acceptance 충족.

- [ ] **Step 5: Commit**

```bash
git add Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M0): probe processor execution flags in gamemode beginplay (#4)"
```

---

## 완료 후

- 브랜치 `feature/M0-processor-split` → PR (base=dev, 이슈 #4 메타 미러링).
- M0 마일스톤 남은 이슈: #5 (탄막 패턴 시드 RPC 인터페이스) 하나. #4 완료 후 #5 진입.
