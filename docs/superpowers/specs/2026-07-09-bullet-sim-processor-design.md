# M1 #15 이동/수명 Processor (SimProcessor Execute) Design

**이슈:** #15 (M1: Mass 보스 탄막 스폰 + 이동 (싱글))
**날짜:** 2026-07-09
**선행:** #14 (Fragment 확정 + Archetype 스폰) 완료

## 목표

`UREBulletSimProcessor`의 스텁(`ConfigureQueries` / `Execute`)을 실제 이동·수명 로직으로 채운다. 매 틱 탄환 위치를 속도로 전진, 수명 감소, 수명 소진 시 엔티티 파괴. 본 이슈는 **시뮬 계산만** — 패턴 수학(#16), ISM 렌더(#17)는 후속.

## 배경 (현재 상태)

`UREBulletSimProcessor`는 M0에서 구조만 존재:
- `ExecutionFlags = AllNetModes(7)` — 싱글/서버/클라 모두 시뮬 (시뮬은 넷모드 무관 동일).
- `ConfigureQueries`: `FBulletSimFragment` RW 하나만 요구.
- `Execute`: `UE_LOG` 스텁.

#14로 탄환 Archetype 확정: `FTransformFragment` + `FBulletSimFragment`(Velocity, Lifetime) + `FBulletRenderFragment` + `FBulletTag`. 스폰 시 Location/Velocity/Lifetime 주입됨. 단 현재는 Velocity=0, Lifetime=0으로 스폰(패턴 수학 #16 전). #15 검증은 프로브에서 값을 직접 넣어 확인.

## 변경 범위

**파일 1개:** `Source/Project_RE/Mass/REBulletSimProcessor.cpp`. 헤더(`.h`) 변경 없음 — `EntityQuery` 멤버 그대로.

### 1. ConfigureQueries

```cpp
EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadWrite);
EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
```

- `FTransformFragment` RW 추가 — 위치 전진에 필요 (기존엔 없었음).
- `FBulletSimFragment` RW 유지 — Velocity 읽기 + Lifetime 감소 쓰기.
- `FBulletTag` All 필터 추가 — 탄환 엔티티만 선별 (다른 Archetype 오염 방지).

`#include "Mass/EntityFragments.h"` 추가 (FTransformFragment 정의처, MassCore 모듈 — Build.cs 수정 불요, #14에서 확인됨).

### 2. Execute

```cpp
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
```

**로직:**
- 위치: `Location += Velocity * Dt` (`AddToTranslation`).
- 수명: `Lifetime -= Dt`.
- 만료(`<= 0`): 지연 파괴로 큐잉.

## API 근거 (UE 5.8 엔진 헤더 확인)

`E:/UE_5.8/Engine/Source/Runtime/MassEntity/Public/` 실물 확인:

| API | 근거 |
|-----|------|
| `ForEachEntityChunk(Context, fn)` | `MassEntityQuery.h:89` — 5.6+ EntityManager 인자 없는 시그니처. 구버전은 `UE_DEPRECATED(5.6)`. |
| `Context.GetDeltaTimeSeconds()` | `MassExecutionContext.h:429` |
| `Context.GetNumEntities()` | `MassExecutionContext.h:447` |
| `Context.GetEntity(i)` | `MassExecutionContext.h:452` |
| `Context.GetMutableFragmentView<T>()` | `MassExecutionContext.h:630` |
| `Context.Defer().DestroyEntity(handle)` | `MassExecutionContext.h:437` + `MassCommandBuffer.h:344` |

## 설계 결정

- **지연 파괴(`Defer().DestroyEntity`)** — 즉시 파괴는 순회 중 청크/Fragment 뷰 무효화 위험. 지연 커맨드는 Execute 종료 후 flush → 루프 안전.
- **`ForEachEntityChunk`(비병렬)** — M1은 싱글, 수백 발 규모. `ParallelForEachEntityChunk`는 YAGNI, M3 프로파일링에서 필요 시 교체.
- **Fragment 뷰 인덱싱** — 청크 내 Transform/Sim 뷰는 동일 순서 보장. `GetEntity(i)`도 같은 인덱스 → 파괴 핸들 정확.
- **`<= 0` 경계** — Lifetime 정확히 0도 파괴 (0초 탄환 = 즉시 소멸, 유효).

## 검증 (완료 기준)

Headless 프로브(`-game -nullrhi`), PIE 없이. `[[headless-runtime-probe]]` 방식 (MSYS_NO_PATHCONV, `-ExecCmds`).

프로브 시나리오:
1. `SpawnBullet(Loc=(0,0,0), Vel=(100,0,0), Lifetime=0.5)` — 알려진 값 직접 주입.
2. N틱 진행 후 로그:
   - **이동:** Location.X 가 `100 * 경과시간` 만큼 전진.
   - **파괴:** 0.5초 경과 후 엔티티 수 0 (파괴 확인).

로그 예:
```
[RE] SimProbe: t=0.10 Loc=(10.00,0,0) Alive=1
[RE] SimProbe: t=0.60 Alive=0   ← Lifetime 소진, 파괴됨
```

넷모드: `AllNetModes(7)`이라 `-server` 프로브에서도 동일 실행 확인 (선택 — 싱글 검증으로 충분, 서버 실행은 M4/M5 몫).

## 스코프 밖 (YAGNI)

- 패턴별 Velocity 수학(나선/부채꼴) → #16
- ISM 인스턴스 갱신/InstanceIndex → #17
- 병렬 순회(`ParallelForEachEntityChunk`) → 필요 시 M3
- 화면 밖(경계) 컬링 파괴 → 미요청, Lifetime만으로 충분
- 헤더(.h) / Build.cs / .uproject 변경 → 불필요
