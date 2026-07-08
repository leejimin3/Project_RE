# M1 #14 탄환 Fragment 확정 + FBulletTag + Archetype 스폰 Design

**이슈:** #14 (M1: Mass 보스 탄막 스폰 + 이동 (싱글))
**날짜:** 2026-07-09

## 목표

탄환 엔티티의 데이터 레이아웃을 확정하고, EntityManager 경유로 탄환을 스폰하는 재사용 헬퍼를 만든다. 이동/수명 계산(#15), 패턴 수학(#16), ISM 렌더(#17)는 후속 이슈. 본 이슈는 **데이터 구조 + 스폰 경로**만.

## 데이터 레이아웃

탄환 Archetype = 4개 요소:

```
FTransformFragment    (엔진, MassCore)  — 위치/회전/스케일. Location만 사용.
FBulletSimFragment    (기존)            — Velocity, Lifetime
FBulletRenderFragment (기존)            — InstanceIndex (기본 INDEX_NONE)
FBulletTag : FMassTag (신규)            — 탄환 식별 Query 필터
```

**중요:** `FTransformFragment`는 UE 5.8에서 `MassCore` 모듈의 `Mass/EntityFragments.h`에 있다. `MassCore`는 이미 Build.cs 의존이므로 **Build.cs 수정·플러그인 토글 불필요.** (M0 플랜이 "MassGameplay 플러그인 M1 활성화"라 적었으나, 실물 확인 결과 `FTransformFragment`는 MassCore에 있어 플러그인 불요.)

## 컴포넌트

### 1. `FBulletTag` — REBulletFragments.h 추가

```cpp
USTRUCT()
struct FBulletTag : public FMassTag
{
    GENERATED_BODY()
};
```

빈 태그. 후속 Processor가 `EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All)`로 탄환만 필터링.

### 2. `UREBulletSpawnSubsystem` — 신규 (Source/Project_RE/Mass/)

`UWorldSubsystem` 상속. 탄환 스폰의 단일 진입점.

**역할 / 인터페이스:**

- `Initialize(FSubsystemCollectionBase&)`: EntityManager 확보 후 탄환 Archetype **1회 캐싱**.
  - EntityManager는 `UMassEntitySubsystem`을 통해 얻는다(같은 World). Subsystem 초기화 순서 문제를 피하려고 Archetype 캐싱은 최초 스폰 시점에 lazy 생성한다(아래 참고).
- `FMassEntityHandle SpawnBullet(FVector Location, FVector Velocity, float Lifetime)`
  - 엔티티 1개 생성 + Fragment 초기화: `FTransformFragment.Transform.Location = Location`, `FBulletSimFragment.Velocity/Lifetime` 세팅. Render.InstanceIndex는 기본값(INDEX_NONE) 유지.
  - 반환: 생성된 핸들.
- `void SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)`
  - N발 스폰. 내부는 `SpawnBullet` 루프. 배치 최적화 API(BatchCreateEntities)는 **YAGNI** — M3 프로파일링에서 필요하면 교체.

**보조 구조체:**

```cpp
struct FBulletSpawnParams
{
    FVector Location = FVector::ZeroVector;
    FVector Velocity = FVector::ZeroVector;
    float   Lifetime = 0.f;
};
```

**Archetype lazy 캐싱:** `Initialize`에서 다른 subsystem(`UMassEntitySubsystem`) 준비 여부가 불확실하므로, `FMassArchetypeHandle`은 멤버로 두고 최초 스폰 호출 시 `EnsureArchetype()`로 생성·캐싱한다. 이후 호출은 캐싱된 핸들 재사용.

**Fragment 초기화 방식:** `EntityManager.CreateEntity(Archetype)`로 엔티티 생성 후 `EntityManager.GetFragmentDataChecked<T>(Entity)`로 각 Fragment 참조를 얻어 값 주입. (배치 CreateEntity + 초기화 콜백 API는 YAGNI.)

### 3. `AREBossCharacter::TriggerBulletPattern` 수정

인라인 `CreateArchetype` + `CreateEntity` 루프 삭제. 대신:

```cpp
UREBulletSpawnSubsystem* Spawner = GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>();
// 보스 위치에서 N발, Velocity=0 (패턴 수학은 #16).
TArray<FBulletSpawnParams> Params;
Params.Reserve(BulletsPerPattern);
for (int32 i = 0; i < BulletsPerPattern; ++i)
    Params.Add({ GetActorLocation(), FVector::ZeroVector, 0.f });
Spawner->SpawnBulletBatch(Params);
```

패턴 `switch`의 TODO 주석(#16 몫)은 유지. Seed/StartTime 미사용도 유지(M5).

## 검증 (완료 기준)

Headless 프로브(`-game -nullrhi`)로 기존 GameMode BeginPlay → Boss 트리거 경로 재사용. `SpawnBulletBatch` 후 로그:

- 스폰된 엔티티 수 (= BulletsPerPattern = 16)
- 첫 엔티티 Fragment 초기값 확인: Location(= 보스 위치), Velocity(=0), Lifetime(=0), Tag 존재.

로그 예: `[RE] SpawnBulletBatch: N=16, first bullet Loc=(...) Vel=(...) Life=0.00`

`[[headless-runtime-probe]]` 방식(MSYS_NO_PATHCONV, `-game -nullrhi -ExecCmds`). PIE 없이 관측.

## 스코프 밖 (YAGNI)

- 이동/수명 감소 로직 → #15
- 패턴별 Velocity 수학(나선/부채꼴) → #16
- ISM 인스턴스 생성/InstanceIndex 할당 → #17
- 배치 최적화 API(BatchCreateEntities) → 필요 시 M3
- Build.cs / .uproject 플러그인 변경 → 불필요(FTransformFragment는 MassCore)
