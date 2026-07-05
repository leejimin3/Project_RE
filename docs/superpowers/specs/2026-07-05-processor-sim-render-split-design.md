# M0 #4 Mass Processor 시뮬/렌더 분리 Design Spec

**이슈:** #4 [M0] Mass Processor 시뮬 / 렌더 분리 구조
**마일스톤:** M0 (셋업 + 아키텍처 결정)
**날짜:** 2026-07-05
**브랜치(예정):** `feature/M0-processor-split`

## 목표

Mass 탄막의 네트워크 역할 분리 **구조**를 확립한다. 시뮬 Processor는 시뮬레이션이 도는 모든 넷모드(싱글/서버/클라)에서 실행, 렌더 Processor는 표시할 화면이 있는 곳(싱글/클라)에서만 실행 → 데디케이티드 서버는 렌더를 자동으로 건너뛴다.

M0 범위는 **구조(클래스·Fragment·ExecutionFlags·Query)까지**다. 실제 이동/ISM 로직과 런타임 skip 실증은 M1로 미룬다 (근거: 아래 스코프 결정).

## 스코프 결정 (YAGNI)

런타임에 Processor가 tick 되려면 `UMassSimulationSubsystem`(MassGameplay 플러그인 / MassSimulation 모듈)이 phase를 구동해야 한다. `UMassEntitySubsystem`(현재 배선된 MassEntity 모듈)은 phase를 tick 하지 않는다. 스펙 #2가 MassGameplay를 M1로 연기했으므로, M0에서 "서버가 Render를 skip"을 런타임으로 실증하려면 MassGameplay를 앞당겨야 한다.

결정: **MassGameplay를 당기지 않는다.** M0 #4는 구조만 만들고, 검증은 빌드 성공 + CDO의 ExecutionFlags 값 확인으로 한다. 런타임 skip 실증은 M1(MassGameplay + 스포너)에서 한다.

- **포함:** Fragment 2개, Processor 2개(ExecutionFlags·ConfigureQueries·Execute 스텁), Query 분리, CDO 플래그 검증 프로브.
- **제외 (M1 이후):** MassGameplay 플러그인, MassSimulation, 스포너, 실제 이동/수명 계산, ISM 렌더, 런타임 tick 실증.

## 이슈 텍스트와의 차이 (의도적 수정)

이슈 #4는 "SimProcessor ExecutionFlags: Server | Client"로 적었다. 그러나 싱글플레이(M1~M3 데모) PIE는 **Standalone** 넷모드다. Sim이 `Server|Client`만이면 싱글에서 tick 되지 않아 탄막이 움직이지 않는다. 따라서:

| Processor | ExecutionFlags | 값 | 데디서버(Server) | 싱글(Standalone) | 클라(Client) |
|---|---|---|---|---|---|
| `UREBulletSimProcessor` | `AllNetModes` (= Standalone\|Server\|Client) | 7 | 실행 | 실행 | 실행 |
| `UREBulletRenderProcessor` | `Standalone \| Client` | 5 | **skip** | 실행 | 실행 |

핵심 목표(데디서버가 Render를 건너뜀) 유지 + 싱글플레이 비파괴.

> `EProcessorExecutionFlags` (MassProcessingTypes.h): Standalone=1<<0, Server=1<<1, Client=1<<2. AllNetModes = Standalone|Server|Client = 7. Standalone|Client = 5.

## 검증 가능한 완료 조건 (Acceptance)

1. `Project_REEditor` 빌드 성공, 에러 0.
2. PIE 실행 → Output Log에 다음 출력:
   ```
   [RE] SimProcessor flags=7  RenderProcessor flags=5
   ```
   (CDO의 `GetExecutionFlags()` 값. Sim=7, Render=5 확인.)

## 설계

### 1. Fragment — `Source/Project_RE/Mass/REBulletFragments.h` (신규)

```cpp
#pragma once
#include "MassEntityTypes.h"
#include "REBulletFragments.generated.h"

USTRUCT()
struct FBulletSimFragment : public FMassFragment
{
    GENERATED_BODY()
    FVector Velocity = FVector::ZeroVector;
    float   Lifetime = 0.f;
    // 위치는 M1에서 FTransformFragment 사용 예정
};

USTRUCT()
struct FBulletRenderFragment : public FMassFragment
{
    GENERATED_BODY()
    int32 InstanceIndex = INDEX_NONE;  // M1: ISM 인스턴스 인덱스
};
```

M0에서 Fragment는 슬롯 정의만. 실제 필드 활용은 M1.

### 2. SimProcessor — `Source/Project_RE/Mass/REBulletSimProcessor.h/.cpp` (신규)

헤더:
```cpp
#pragma once
#include "MassProcessor.h"
#include "REBulletSimProcessor.generated.h"

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

cpp:
```cpp
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

### 3. RenderProcessor — `Source/Project_RE/Mass/REBulletRenderProcessor.h/.cpp` (신규)

SimProcessor와 동일 골격. 차이:
- ctor: `ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);  // 5: 데디서버 skip`
- `ConfigureQueries`:
  ```cpp
  EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadOnly);      // sim 위치 읽기
  EntityQuery.AddRequirement<FBulletRenderFragment>(EMassFragmentAccess::ReadWrite);  // 인스턴스 쓰기
  ```
- `Execute`: `UE_LOG(LogTemp, Verbose, TEXT("[RE] RenderProcessor::Execute"));`

### 4. Build.cs — include path 추가

`PublicIncludePaths`에 `"Project_RE/Mass"` 추가. 모듈 의존은 변경 없음(`MassEntity`·`MassCore` 그대로).

### 5. 검증 프로브 — `AREGameMode::BeginPlay`

기존 Mass 스모크 테스트 블록 뒤에 CDO 플래그 로그 추가:
```cpp
const uint8 SimFlags    = (uint8)GetDefault<UREBulletSimProcessor>()->GetExecutionFlags();
const uint8 RenderFlags = (uint8)GetDefault<UREBulletRenderProcessor>()->GetExecutionFlags();
UE_LOG(LogTemp, Log, TEXT("[RE] SimProcessor flags=%d  RenderProcessor flags=%d"), SimFlags, RenderFlags);
```
`REGameMode.cpp`에 두 Processor 헤더 include 추가.

## API 근거 (UE 5.8 헤더 검증됨)

- `UMassProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)` — 5.6+ 시그니처 (`MassProcessor.h:225`). 구버전 무인자 오버로드는 deprecated.
- `UMassProcessor::Execute(FMassEntityManager&, FMassExecutionContext&)` (`MassProcessor.h:233`)
- `ExecutionFlags`는 `uint8` UPROPERTY, ctor에서 `(int32)EProcessorExecutionFlags::X`로 설정 (엔진 예: `MassMovementProcessors.cpp:24`)
- `FMassEntityQuery(*this)` ctor로 Owner 프로세서에 등록 (`MassEntityQuery.h:81`, 엔진 예: `MassMovementProcessors.cpp:22`)
- `UMassProcessor::GetExecutionFlags()` public inline → `EProcessorExecutionFlags` (`MassProcessor.h:95`)
- `EProcessorExecutionFlags` 값: Standalone=1, Server=2, Client=4, AllNetModes=7 (`MassProcessingTypes.h:23`)

## 테스트 전략

자동화 테스트 인프라 없음. 각 단계는 **빌드 성공**을 게이트로, 최종은 **PIE Output Log의 `flags=7 ... flags=5` 관측**을 게이트로 한다.

## 파일 요약

- New: `Source/Project_RE/Mass/REBulletFragments.h`
- New: `Source/Project_RE/Mass/REBulletSimProcessor.h` / `.cpp`
- New: `Source/Project_RE/Mass/REBulletRenderProcessor.h` / `.cpp`
- Modify: `Source/Project_RE/Project_RE.Build.cs` (include path 1개)
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (헤더 2개 include + 플래그 로그)
