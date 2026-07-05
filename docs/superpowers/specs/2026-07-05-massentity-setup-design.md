# M0 #2 MassEntity 셋업 + 런타임 프로브 Design Spec

**이슈:** #2 [M0] MassEntity 플러그인 활성화 + Build.cs 모듈 설정
**마일스톤:** M0 (셋업 + 아키텍처 결정)
**날짜:** 2026-07-05
**브랜치(예정):** `feature/M0-mass-setup`

## 목표

M1 Mass 탄막 작업 전, Mass 코어 모듈을 빌드에 배선하고 PIE에서 엔티티 1개를 실제 생성하여 `UMassEntitySubsystem`이 살아있음을 런타임으로 실증한다.

## 배경 — 이슈 #2 원문과 UE 5.8 실물 차이

이슈 #2 목록은 구버전 정보 기준이라 UE 5.8 실물과 어긋난다. 검증 결과:

| 이슈 #2 항목 | UE 5.8 실물 | 조치 |
|---|---|---|
| `MassEntity` 플러그인 활성화 | **엔진 런타임 모듈** (`Source/Runtime/MassEntity`), 플러그인 아님 | Build.cs 모듈 의존만 추가 |
| `MassActorIntegration` 플러그인 | 그런 플러그인 없음. 모듈명은 `MassActors` (MassGameplay 내부) | M6 표현단계 전 불필요 |
| `StructUtils` 플러그인/모듈 | Experimental 플러그인 존재하나 `MassEntity`는 `MassCore`에만 의존 | 불필요 |
| `MassGameplay` 플러그인 | 존재 (MassCommon/MassSpawner/MassMovement/MassRepresentation/MassActors 등 포함) | **M1에서** 활성화 |
| `MassCore` | 엔진 런타임 모듈 (`Source/Runtime/Mass/MassCore`) | Build.cs 모듈 의존 추가 |

핵심: `UMassEntitySubsystem`·`FMassFragment`·`FMassEntityManager`가 전부 `MassEntity` 엔진 모듈에 있고, 이 모듈은 `MassCore` 엔진 모듈에만 의존한다. 따라서 **완료기준("UMassEntitySubsystem 접근 가능")은 Build.cs에 `MassEntity`,`MassCore` 2개만 추가하면 충족되며 .uproject 플러그인 토글은 불필요**하다.

## 스코프 결정 (YAGNI)

M0 #2는 셋업 스모크 테스트다. 최소셋만 배선한다.

- **포함:** `MassEntity`, `MassCore` 모듈 배선 + 커스텀 프래그먼트 1개로 엔티티 생성 프로브.
- **제외 (M1 이후):** MassGameplay 플러그인, 프로세서, 스포너, MassMovement/MassRepresentation 모듈, 프래그먼트 데이터 실제화, StructUtils, MassActors.

이유: 안 쓰는 모듈을 미리 링크하면 빌드시간만 늘고, M1에서 실제 API를 만질 때 정확히 필요한 모듈을 알게 된다 (CLAUDE.md Simplicity First).

## 검증 가능한 완료 조건 (Acceptance)

1. `Project_REEditor` 빌드 성공, 에러 0.
2. PIE 실행 → Output Log에 `[RE] Mass entity created: Index=0 Serial=<n>` 출력. (`[RE] UMassEntitySubsystem NULL` 경고가 아님.)

## 설계

### 1. 빌드 배선

`Source/Project_RE/Project_RE.Build.cs`의 `PublicDependencyModuleNames`에 추가:

```csharp
"MassEntity",
"MassCore",
```

`.uproject` 변경 없음.

### 2. 테스트 프래그먼트 (throwaway seed)

`Source/Project_RE/Core/REGameMode.h`에 인라인 정의:

```cpp
USTRUCT()
struct FRETestFragment : public FMassFragment
{
    GENERATED_BODY()
    int32 Dummy = 0;
};
```

M1에서 실제 탄막 프래그먼트(위치/속도)가 생기면 별도 `MassFragments.h`로 이동·교체한다.

### 3. 런타임 프로브 — `AREGameMode::BeginPlay`

```cpp
void AREGameMode::BeginPlay()
{
    Super::BeginPlay();

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
}
```

- GameMode는 서버 권위(항상 authority)이므로 별도 `HasAuthority()` 가드 불필요.
- `AREGameMode`는 현재 생성자만 있음(#1). `BeginPlay` 오버라이드를 신규 추가한다.

### API 근거 (UE 5.8 헤더 검증됨)

- `UMassEntitySubsystem::GetMutableEntityManager()` → `FMassEntityManager&` (`MassEntitySubsystem.h`)
- `FMassEntityManager::CreateArchetype(TConstArrayView<const UScriptStruct*>)` → `FMassArchetypeHandle` (`MassEntityManager.h:162`)
- `FMassEntityManager::CreateEntity(const FMassArchetypeHandle&)` → `FMassEntityHandle` (`MassEntityManager.h:284`)
- `FMassEntityHandle`: `Index`, `SerialNumber` (int32)

## 테스트 전략

자동화 테스트 인프라 없음. 각 단계는 **빌드 성공**을 게이트로, 최종 단계는 **PIE Output Log 관측**을 게이트로 한다. (프로브 자체가 런타임 스모크 테스트 역할.)

## 파일 요약

- Modify: `Source/Project_RE/Project_RE.Build.cs` (모듈 2개 추가)
- Modify: `Source/Project_RE/Core/REGameMode.h` (`#include` + `FRETestFragment` + `BeginPlay` 선언)
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (`BeginPlay` 구현 + Mass include)
