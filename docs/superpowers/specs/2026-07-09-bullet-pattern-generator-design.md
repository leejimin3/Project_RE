# M1 #16 패턴 제너레이터 (나선/부채꼴) Design

**이슈:** #16 (M1: Mass 보스 탄막 스폰 + 이동 (싱글))
**날짜:** 2026-07-09
**선행:** #14 (Archetype 스폰) + #15 (SimProcessor 이동/수명) 완료

## 목표

`EBulletPattern`별 발사 수학을 구현한다. M1은 **Spiral(나선) / Fan(부채꼴)** 2종. 보스 위치를 시작점으로 각도별 방향벡터 × Speed = Velocity를 계산해 배치 스폰 헬퍼(#14 `SpawnBulletBatch`)로 넘긴다. 이동/수명(#15)이 이미 Velocity·Lifetime을 소비하므로, 본 이슈는 **초기 Velocity/Lifetime을 채우는 순수 수학만** 담당. ISM 렌더(#17)는 후속.

## 배경 (현재 상태)

`AREBossCharacter::TriggerBulletPattern`은 #14에서 스폰 경로만 연결됨:
- `switch (Pattern)`의 각 case는 `// TODO M1(#16)` 주석만.
- 실제 스폰은 switch 밖에서 `BulletsPerPattern`(=16)발을 `Velocity=0, Lifetime=0`으로 채워 `SpawnBulletBatch` 호출.

`FBulletSpawnParams { FVector Location; FVector Velocity; float Lifetime; }` (UStruct 아님, 함수 인자 전용) 이미 존재.
`SpawnBullet(Location, Velocity, Lifetime)` / `SpawnBulletBatch(TConstArrayView<FBulletSpawnParams>)` 이미 존재.

## 설계 결정 (사전 확정)

- **순수 수학 유닛 신규 파일** — 엔진/액터 의존 없는 static 함수 → headless 프로브에서 각도/속도 단위 검증 쉬움. Boss는 결과 배열을 받아 `SpawnBulletBatch`로 넘기기만.
- **Spiral 단발 호출 = 나선 팔 1개** — `TriggerBulletPattern` 1회 = N발, 각도 `BaseAngle + i*AngleStep`. `BaseAngle` 누적은 Boss 멤버로 두어 **호출마다 회전 증가**(연속 트리거 시 링이 돈다). 타이머 연사는 M1 범위 밖 — 단발 호출로 셰이프만 검증.

## 변경 범위

**신규 2개 + 수정 1개:**

| 파일 | 변경 |
|------|------|
| `Source/Project_RE/Mass/REBulletPatternGenerator.h` | 신규 — 제너레이터 선언 |
| `Source/Project_RE/Mass/REBulletPatternGenerator.cpp` | 신규 — 나선/부채꼴 수학 |
| `Source/Project_RE/Core/REBossCharacter.h/.cpp` | 수정 — switch 분기 실배선 + BaseAngle 멤버 |

Build.cs / .uproject 변경 없음 — 순수 C++/`FMath`, `FBulletSpawnParams`는 기존 헤더.

## 컴포넌트

### 1. `REBulletPatternGenerator` (신규)

`FBulletSpawnParams`(정의처: `REBulletSpawnSubsystem.h`)를 반환하므로 그 헤더 include.

```cpp
// REBulletPatternGenerator.h
#pragma once
#include "CoreMinimal.h"
#include "REBulletSpawnSubsystem.h"   // FBulletSpawnParams

namespace REBulletPattern
{
    struct FSpiralParams
    {
        int32 Count        = 16;
        float BaseAngleDeg = 0.f;    // 이번 발사의 시작각 (Boss가 누적해 전달)
        float AngleStepDeg = 22.5f;  // 탄 간 각 간격 (기본 360/16 = 균등 링)
        float Speed        = 300.f;  // uu/s
        float Lifetime     = 3.f;    // s
    };

    struct FFanParams
    {
        int32 Count          = 16;
        float CenterAngleDeg = 0.f;   // 부채꼴 중심 방향
        float SpreadDeg      = 90.f;  // 전체 벌어짐 각
        float Speed          = 300.f;
        float Lifetime       = 3.f;
    };

    /** 나선 팔 1개: 각도 = BaseAngle + i*AngleStep, i=0..Count-1. */
    TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P);

    /** 부채꼴: CenterAngle 기준 -Spread/2 .. +Spread/2 를 Count 등분 동시 발사. */
    TArray<FBulletSpawnParams> GenerateFan(const FVector& Origin, const FFanParams& P);
}
```

**공통 방향 헬퍼(cpp 내부 static):** 각도(deg) → 수평면 단위벡터.
```cpp
static FVector DirFromDeg(float Deg)
{
    const float R = FMath::DegreesToRadians(Deg);
    return FVector(FMath::Cos(R), FMath::Sin(R), 0.f);  // XY 수평면, Z up
}
```

**Spiral 구현:**
```cpp
Out.Reserve(P.Count);
for (int32 i = 0; i < P.Count; ++i)
{
    const float Angle = P.BaseAngleDeg + i * P.AngleStepDeg;
    Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime });
}
```

**Fan 구현:** `Count==1`이면 중심각 1발(0-나눗셈 방지). 아니면 좌끝에서 균등.
```cpp
Out.Reserve(P.Count);
const float Start = P.CenterAngleDeg - P.SpreadDeg * 0.5f;
const float Step  = (P.Count > 1) ? P.SpreadDeg / (P.Count - 1) : 0.f;
for (int32 i = 0; i < P.Count; ++i)
{
    const float Angle = Start + i * Step;
    Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime });
}
```

### 2. `AREBossCharacter` 수정

**헤더:** 나선 누적 상태 멤버 추가.
```cpp
private:
    /** Spiral 호출마다 누적되는 시작각. 연속 트리거 시 링이 회전한다. */
    float SpiralBaseAngleDeg = 0.f;
    /** Spiral 호출당 BaseAngle 증가량. */
    static constexpr float SpiralRotationStepDeg = 15.f;
```

**cpp:** switch 각 case가 Params를 만들고, switch 후 단일 `SpawnBulletBatch`. 기존 "Velocity=0 루프" 삭제.
```cpp
TArray<FBulletSpawnParams> Params;
switch (Pattern)
{
case EBulletPattern::Spiral:
{
    REBulletPattern::FSpiralParams SP;
    SP.BaseAngleDeg = SpiralBaseAngleDeg;
    Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
    SpiralBaseAngleDeg += SpiralRotationStepDeg;  // 다음 호출 시 회전
    break;
}
case EBulletPattern::Fan:
{
    REBulletPattern::FFanParams FP;
    Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
    break;
}
case EBulletPattern::Homing:
    // M1 범위 밖 — 슬롯만 유지, 미구현. 스폰 없음.
    UE_LOG(LogTemp, Warning, TEXT("[RE] Boss: Homing 미구현 (M1 범위 밖)"));
    return;
}
Spawner->SpawnBulletBatch(Params);
```

로그 라인은 `BulletsPerPattern` 대신 `Params.Num()` 사용하도록 수정. 상수 `BulletsPerPattern`은 제너레이터 기본 `Count`로 이관 → 익명 namespace 상수 삭제 가능.

## 설계 결정 (근거)

- **각도 → XY 수평면(Z up)** — 탑다운 카메라 프로젝트. Z 고정. (게임이 다른 평면 쓰면 헬퍼만 교체.)
- **파라미터 구조체 기본값** — Count/Speed/Lifetime/Step을 구조체 기본값으로. Boss는 필요한 것만 오버라이드. 매직넘버 튜닝 한 곳.
- **Spiral BaseAngle 누적 = Boss 멤버** — 제너레이터는 무상태 순수 함수 유지. 회전 상태 소유는 호출자(Boss). 단발 호출은 링, 반복 호출로 나선 애니메이션(타이머는 후속 마일스톤).
- **Fan Count==1 가드** — `SpreadDeg/(Count-1)` 0-나눗셈 방지.
- **Homing은 early-return** — 스폰 없이 미구현 명시. #15 이동 로직 오염 안 함.

## 검증 (완료 기준)

순수 함수라 headless 프로브(`-game -nullrhi`)로 직접 호출·관측. `[[headless-runtime-probe]]` 방식(MSYS_NO_PATHCONV, `-ExecCmds`).

**제너레이터 단위:**
1. `GenerateSpiral(Origin=(0,0,0), Count=16, Base=0, Step=22.5, Speed=300)`:
   - `Num()==16`, 모든 `Location==Origin`, 모든 `|Velocity|≈300`.
   - `V[0]` 각도 ≈ 0°, `V[1]` ≈ 22.5°, … 증가 확인.
2. `GenerateFan(Center=0, Spread=90, Count=16)`:
   - 첫 탄 각도 ≈ -45°, 끝 탄 ≈ +45° (좌우 대칭).

**Boss 경로(기존 GameMode BeginPlay 재사용):**
3. `TriggerBulletPattern(Spiral)` 2회 → 2번째 배치 각도가 `SpiralRotationStepDeg`만큼 회전(BaseAngle 누적 확인).
4. #15 SimProcessor가 이어받아 실제 전개되는지(탄이 방사형으로 퍼짐) 로그로 교차 확인.

로그 예:
```
[RE] SpiralProbe: N=16 |V0|=300.0 ang0=0.0 ang1=22.5
[RE] FanProbe:    N=16 ang_first=-45.0 ang_last=45.0
[RE] Boss Spiral #2: BaseAngle=15.0 (회전 누적 확인)
```

## 스코프 밖 (YAGNI)

- **Homing 수학** — 슬롯만, 미구현 명시 (타깃 추적은 후속).
- **발사 타이머/연사 루프** — 단발 트리거만. 나선 애니메이션(연속 회전)은 타이머 도입하는 후속 마일스톤.
- **플레이어 조준(Fan CenterAngle 동적)** — M1은 고정 중심각. 조준은 후속.
- **ISM 인스턴스/InstanceIndex** — #17.
- **이동/수명 감소** — #15 완료분.
- **Build.cs / .uproject 변경** — 불필요(순수 C++).
