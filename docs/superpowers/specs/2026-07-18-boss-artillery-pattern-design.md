# 보스 곡사(Artillery) 탄막 패턴 Design

**마일스톤:** M5 후속 (보스 패턴 확장)
**날짜:** 2026-07-18
**선행:** #16 패턴 제너레이터(Spiral/Fan), #64 패턴 로테이션(PR#65) 머지 완료

## 목표

보스가 탄환을 **분수처럼 포물선으로 뿜어** 지정 지점에 떨어뜨리고, 착지 예고(빨간 원 마커)를 미리 보여준 뒤, 착지 순간 **범위 데미지**를 주는 곡사(artillery) 패턴을 추가한다. 기존 직선 탄막(Spiral/Fan)과 공존하며 페이즈 로테이션에 편입된다.

단일 곡사탄 1발의 수명주기는 하나로 고정되고, 착지점을 어디서 뽑느냐만 다른 6종 서브패턴(원형/라인/격자/나선 모양 4종 + 플레이어조준 + 랜덤)을 지원한다.

## 배경 (현재 상태 — 소스 대조)

- **탄환 모델**: Mass 엔티티. `FBulletSimFragment{Velocity, Lifetime}` — 등속 직선, 수명 만료 or 원점근처 히트로 소멸. `FBulletTag`로 프로세서 3종(Sim/Hit/Render)이 선별.
- **Sim** (`REBulletSimProcessor`): `Transform += Velocity*dt`, 수명 감소. 등속만, **중력·가속 없음**. `AllNetModes`.
- **Hit** (`REBulletHitProcessor`): 서버/싱글만, GT전용. 플레이어와 XY거리 `HitRadius=60` 이내면 `Player->TakeDamage(BulletDamage=10, ...)` + 탄 소멸. `State.Dashing` 태그면 판정 스킵.
- **Render** (`REBulletRenderProcessor`): 클라/싱글만, GT전용. 단일 ISM(구체 `BulletScale=0.5`)에 전체 live 탄 매 프레임 리빌드.
- **스폰** (`UREBulletSpawnSubsystem`): `SpawnBullet(Loc,Vel,Life)` + `SpawnBulletBatch(params[])`. Archetype lazy 캐싱.
- **패턴 생성기** (`REBulletPattern` 네임스페이스): 엔진/액터 의존 없는 순수함수 → headless 단위검증 가능. `GenerateSpiral`/`GenerateFan` → `TArray<FBulletSpawnParams>` 반환.
- **발사 주체** (`AREBossCharacter`): `StartFiring(Seed)` → `BeginPhase`/`FireCurrentPattern`/`EndPhase` 상태머신. `EBulletPattern{Spiral,Fan,Homing}`을 `PhaseRng`로 페이즈마다 선택. 페이즈 시간 상수 헤더 보유.

## 설계 결정 (브레인스토밍 확정)

1. **운동 = 목표주도 파라메트릭 궤적** (중력 sim 아님). 착지점을 먼저 정하고 궤적을 역산 → 목표 정확 도달 보장. 텔레그래프 정확성 + 모양패턴 정밀성이 공짜로 나옴.
   - `XY = lerp(Start.XY, Target.XY, t)`, `t = Elapsed/FlightTime`
   - `Z = Start.Z + 4·H·t·(1-t)` — t=0,1에서 바닥, t=0.5에서 최대고도 H
2. **수명주기 타이밍 = 발사 즉시 마커 ON, 비행 내내 표시, 착지=폭발.** 비행시간 = 회피시간. 판정은 착지 프레임 **스냅샷**(그 순간 마커 반경 안이면 히트). 지속 히트박스 아님.
3. **아키텍처 = 기존 Mass 파이프 재사용.** arc 전용 태그/프래그먼트/프로세서 추가. 별도 액터 시스템 아님(대량탄 성능 유지).
4. **마커 = ISM 평면 원 메시.** 별도 마커 ISM 하나 추가. 데칼/Niagara 아님(탑다운 평면 바닥엔 오버킬). 폭발 VFX는 후속 — MVP는 마커소멸=폭발.
5. **착지점 생성기 = 순수함수 6종**, `REBulletPattern` 네임스페이스 확장(headless 검증 유지).
6. **페이즈 통합.** `EBulletPattern::Artillery` 추가 + `EArtilleryShape` 서브타입. `BeginPhase`에서 Artillery 뽑히면 shape 랜덤선택 → 착지점 생성 → arc탄 배치 스폰.

## 단일 곡사탄 수명주기

```
발사: SpawnArcBullet(Start=보스위치, Target=착지점, FlightTime, H, Damage, Radius)
      → arc탄 엔티티 1개 + 마커 엔티티 1개(같은 Target, Z=바닥, 반경=Radius)
비행: Elapsed += dt;  t = Elapsed / FlightTime
      XY = lerp(Start.XY, Target.XY, t)
      Z  = Start.Z + 4·H·t·(1-t)
착지: t >= 1 → 착지 프레임 스냅샷 판정:
        플레이어 XY가 Target 반경(Radius) 안 && !Dashing → Player->TakeDamage(Damage)
      → arc탄 + 마커 소멸
```

## Mass 확장

### 프래그먼트/태그 (`REBulletFragments.h` 확장)
```cpp
USTRUCT()
struct FArcBulletFragment : public FMassFragment
{
    GENERATED_BODY()
    FVector Start      = FVector::ZeroVector;
    FVector Target     = FVector::ZeroVector;
    float   FlightTime = 1.f;
    float   Elapsed    = 0.f;
    float   MaxHeight  = 300.f;  // H
    float   Damage     = 10.f;
    float   Radius     = 100.f;
};

USTRUCT()
struct FArcBulletTag : public FMassTag { GENERATED_BODY() };

/** 착지점 마커. 위치=Target(바닥), 반경=arc탄 Radius. arc탄과 별도 엔티티 or 별도 ISM. */
USTRUCT()
struct FArcMarkerTag : public FMassTag { GENERATED_BODY() };
```

### 프로세서 3종 (신규)
- **`UREArcSimProcessor`** (`AllNetModes`): `FArcBulletTag` + `FTransformFragment` + `FArcBulletFragment`. 위 보간으로 Transform 갱신. `Elapsed >= FlightTime`이면 착지 마킹 → `Defer().DestroyEntity`. 착지 정보를 Hit이 소비하도록: **착지 시 소멸을 Hit 이후로** — ExecutionOrder로 Sim→Hit→소멸 보장(아래 판정 순서 참고).
- **`UREArcHitProcessor`** (Standalone|Server, GT전용): `ExecuteAfter(ArcSim)`. 이번 프레임 착지(`Elapsed >= FlightTime`)한 탄만 골라, 플레이어 XY가 `Target` 반경 안이면 `TakeDamage`. `State.Dashing` 스킵(기존 규칙 재사용). 판정 후 `Defer().DestroyEntity`(arc탄+마커).
- **`UREArcRenderProcessor`** (Standalone|Client, GT전용): arc탄 → 기존 구체 ISM(Z 살아있어 궤적 높이 보임). 마커 → **2번째 ISM(평면 원)**, Z=바닥, 스케일=Radius. 매 프레임 리빌드(기존 RenderProcessor 패턴).

> **판정/소멸 순서 함정**: 등속탄은 Sim이 수명만료로 소멸하지만, arc탄은 "착지=판정 대상"이라 Sim이 먼저 지우면 Hit이 놓친다. → **소멸 책임을 Hit(서버)로**. 단 Render(클라)엔 Hit 프로세서가 없으므로, 클라에선 Sim이 `Elapsed >= FlightTime + 1프레임` 여유 후 소멸(마커 1프레임 잔상 허용) 또는 Sim이 소멸 담당하되 Hit을 `ExecuteBefore(ArcSim)`로. **확정: Hit을 `ExecuteAfter(ArcSim)`, 소멸은 Sim이 `Elapsed >= FlightTime`에서 담당하되 Hit이 같은 프레임 먼저 판정하도록 ExecutionOrder로 Hit→Sim 순서 보장.** 서버=Hit후Sim소멸, 클라=Sim만(판정 없이 소멸). ⚠️ *구현 시 프로세서 실행순서 실측 검증 필요.*

### 스폰 (`UREBulletSpawnSubsystem` 확장)
```cpp
/** 곡사탄 1발 + 마커 1개 스폰. arc Archetype(FArcBulletTag) lazy 캐싱. */
void SpawnArcBullet(FVector Start, FVector Target, float FlightTime,
                    float MaxHeight, float Damage, float Radius);
void SpawnArcBulletBatch(TConstArrayView<FArcBulletSpawnParams>);
```
마커 ISM은 `UREBulletRenderSubsystem`에 2번째 핸들(`GetMarkerISM()`) 추가.

## 착지점 생성기 (순수함수, `REBulletPattern` 확장)

전부 `TArray<FVector> Gen*(...)` → 착지점(월드 XY, Z=바닥) 배열 반환. headless 검증가능.

| 생성기 | 규칙 |
|--------|------|
| **원형 링** | 중심 C, 반경 R, N개 균등각: `C + R·(cos θ_i, sin θ_i)`, `θ_i = 2π·i/N` |
| **라인** | 시작→끝 N등분. 방향 = **보스→플레이어 수직 벽**(쓸어오는 라인). 벽 길이 L, 중심=플레이어, 법선=보스→플레이어 |
| **격자** | 아레나 M×N 균등 그리드. 셀 중심마다 1점 |
| **나선** | 아르키메데스: i번째 = 각 `i·137.5°`(기존 황금각 재사용), 반경 `R·√(i/N)` |
| **플레이어 조준** | 플레이어 현재위치 스냅샷 + 주변 소클러스터(중심 1 + 링 3~4). 리드예측은 후속 |
| **랜덤** | 아레나 반경 내 `PhaseRng` 균등 N개. 겹침 방지 YAGNI |

## 페이즈 통합

```cpp
// REBulletPattern.h
enum class EBulletPattern : uint8 { Spiral, Fan, Homing, Artillery };
enum class EArtilleryShape : uint8 { Ring, Line, Grid, Spiral, PlayerAimed, Random };
```
- `BeginPhase`: `PhaseRng`로 `EBulletPattern` 선택. `Artillery`면 `EArtilleryShape`도 랜덤 선택.
- `FireCurrentPattern` (Artillery 분기): shape별 생성기 호출 → 착지점 배열 → `SpawnArcBulletBatch`.
- 곡사 페이즈: 1회 일제사 또는 짧은 연사. 파라미터 헤더 상수(추후 튜닝):
  `ArtilleryPhaseSec`, `ArtilleryFlightTime≈1.5s`, `ArtilleryMaxHeight≈400`, `ArtilleryRadius≈120`, `ArtilleryDamage≈15`, `ArtilleryCount(shape별)`.
- 측정 하네스(`KeepFiring`)는 Spiral 클로즈드루프 고정이라 Artillery 미개입 — 회귀 없음.

## 변경 범위

**신규 6, 수정 5 (추정):**

| 파일 | 변경 |
|------|------|
| `REBulletFragments.h` | `FArcBulletFragment`, `FArcBulletTag`, `FArcMarkerTag` 추가 |
| `REBulletPattern.h` | `EBulletPattern::Artillery`, `EArtilleryShape` 추가 |
| `REBulletPatternGenerator.h/.cpp` | 착지점 생성기 6종 + arc 파라미터 구조체 |
| `REArcSimProcessor.h/.cpp` | 신규 — 궤적 보간 + 착지 소멸 |
| `REArcHitProcessor.h/.cpp` | 신규 — 착지 프레임 스냅샷 범위판정 |
| `REArcRenderProcessor.h/.cpp` | 신규 — 탄 ISM + 마커 ISM |
| `REBulletSpawnSubsystem.h/.cpp` | `SpawnArcBullet`/`Batch` + arc Archetype |
| `REBulletRenderSubsystem.h/.cpp` | 마커 ISM 2번째 핸들 |
| `REBossCharacter.h/.cpp` | Artillery 페이즈 분기 + shape 선택 + 파라미터 상수 |

## 검증 게이트

1. **headless 단위**: 착지점 생성기 6종(개수·기하 assert), 궤적 보간(`t=0→Start`, `t=1→Target`, `t=0.5→Z=Start.Z+H`).
2. **빌드 게이트**: 컴파일 통과.
3. **런타임 프로브**: headless(`-game -nullrhi`)로 arc탄 스폰 수 + 착지 판정 로그 관측 (기존 headless 프로브 방식).
4. **스크린샷 검증**: 실RHI `-windowed`, `FScreenshotRequest`로 PNG — arc탄 궤적 높이(Z) + 마커 평면 원 육안 확인 (`-nullrhi`는 ISM Bounds=0 오진 함정이라 실RHI 필수).

## 미해결 / 후속

- **프로세서 실행순서**(Hit↔Sim 착지 판정 경합) — 구현 시 실측 검증 필요. ⚠️
- 폭발 VFX(Niagara), 리드 예측 조준, 다중 링/모양 조합, 지형 따라가는 데칼 마커 — 전부 후속.
- 라인 방향 "보스→플레이어 수직 벽" 기본값 — 플레이 후 조정 여지.
