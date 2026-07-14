# 설계 스펙: [M3 #43] 탄막 스케일 노브 — CVar `re.Bullets.Count`

## 개요
UE 5.8 C++ 탑뷰 탄막 프로젝트. 이슈 **#43** (마일스톤 **M3: 스케일 업 + 프로파일링**).
현재 동시 탄환 수는 하드코딩 3개(발사당 16발 / 발사주기 0.1s / 수명 3s)의 곱으로 우연히 **약 480발**에 고정돼 있다. 100/1000/5000 스케일 측정을 하려면 먼저 "동시 N발"을 런타임에 재현 가능하게 만들어야 한다. 이 이슈는 M3 측정 전체의 전제 조건이며, 산출물은 **CVar `re.Bullets.Count` 하나**다.

성능 측정·최적화는 **범위 밖** (#46). 프로파일링 하네스는 **#44**, Actor 베이스라인은 **#45**가 소유한다.

## 결정 요약 (브레인스토밍 확정)
| 항목 | 결정 | 근거 |
|---|---|---|
| 역산 로직 위치 | `REBulletPattern` 네임스페이스에 **순수 함수** `MakeSpiralForLiveCount(TargetLive, BaseAngle)` | 기존 제너레이터의 "엔진/액터 의존 없는 순수 함수 → headless 단위 검증 가능" 철학 유지. CVar 읽기는 호출자(Boss)가 담당해 순수성 보존. |
| 발사주기·수명 상수 단일 출처 | `REBulletPatternGenerator.h`에 `constexpr float FireIntervalSec = 0.1f; BulletLifetimeSec = 3.f;` | 역산 공식이 두 값을 모두 필요로 하는데 현재 주기는 `REGameMode.cpp:67` SetTimer 리터럴, 수명은 `FSpiralParams` 기본값으로 흩어져 있다. 제너레이터 헤더로 통합하고 GameMode 타이머·`FSpiralParams::Lifetime` 기본값이 이를 참조. |
| CVar 조회 시점 | **발사 시점** (`Boss::TriggerBulletPattern` 진입 시 `GetValueOnGameThread()`) | 완료조건 "재시작 없이 다음 발사부터 반영"을 타이머 재설정 없이 자동 충족. |
| CVar 선언 위치 | `REBossCharacter.cpp` 파일 스코프 `static TAutoConsoleVariable<int32>` | 읽는 곳이 Boss 한 곳뿐. 헤더 노출 불필요. |
| 고정할 축 | **수명(3s)·발사주기(0.1s) 고정, 발사당 탄 수만 역산** | 측정에 필요한 건 "동시 N발"이라는 단일 축. 3개 노브를 각각 노출하면 조합 폭발 → CVar 1개로 제한. |
| 균등 링 유지 | `AngleStepDeg = 360 / Count` 계산 (기존 `22.5f` 하드코딩 제거) | `22.5f`는 `360/16`. Count가 바뀌면 링이 안 닫힌다. |
| 기존 M0/M1 프로브 코드 | **미접촉** (BeginPlay `:55-56` 버스트 2발, `:78-91` 수학 프로브, SimProbe + `Tick()`) | 초기 2발 버스트는 3초 뒤 소멸 → steady-state 무영향. SimProbe는 0.5s 단발이라 `Tick()` 로그가 곧 멈춘다(`ProbeBullet.Reset()`). 측정 노이즈 사실상 0. 무관한 삭제는 diff·리뷰 범위만 넓힌다. |
| 관측 방법 | `REBulletRenderProcessor.cpp:71-76`의 **기존 `RenderProbe` 재사용** | `live=%d`는 Mass 쿼리로 센 실제 엔티티 수(ISM 인스턴스 아님)라 그대로 쓸 수 있다. 새 카운터 안 만든다. **해당 파일은 #44 소유 — 읽기만 하고 수정 금지.** |
| Fan / Homing 경로 | **미변경** | 측정은 Spiral 하나로 충분 (이슈 명시 범위 밖). |

## 아키텍처

### 1. `Source/Project_RE/Mass/REBulletPatternGenerator.h` (수정)
```cpp
namespace REBulletPattern
{
    /** 발사 주기(s). GameMode 발사 타이머와 역산 공식의 단일 출처. */
    constexpr float FireIntervalSec   = 0.1f;
    /** 탄 수명(s). FSpiralParams::Lifetime 기본값과 역산 공식의 단일 출처. */
    constexpr float BulletLifetimeSec = 3.f;

    struct FSpiralParams
    {
        int32 Count        = 16;
        float BaseAngleDeg = 0.f;
        float AngleStepDeg = 22.5f;
        float Speed        = 300.f;
        float Lifetime     = BulletLifetimeSec;   // 3.f 리터럴 → 상수 참조
    };

    /**
     *  목표 동시 탄환 수 N → 균등 링 Spiral 파라미터.
     *  steady-state 동시 탄환 = 발사당_탄수 / 발사주기 × 수명 이므로
     *  Count = ceil(N × FireIntervalSec / BulletLifetimeSec), AngleStep = 360/Count.
     */
    FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg);
}
```

### 2. `Source/Project_RE/Mass/REBulletPatternGenerator.cpp` (수정)
```cpp
FSpiralParams MakeSpiralForLiveCount(int32 TargetLive, float BaseAngleDeg)
{
    FSpiralParams P;
    P.Count        = FMath::Max(1, FMath::CeilToInt(TargetLive * FireIntervalSec / BulletLifetimeSec));
    P.AngleStepDeg = 360.f / P.Count;   // Count 무관 균등 링
    P.BaseAngleDeg = BaseAngleDeg;
    return P;                            // Speed/Lifetime은 기본값 유지
}
```
`FMath::Max(1, ...)`은 `N <= 0` 입력 시 `360 / 0` 나눗셈을 막는 가드다 (CVar는 사용자 입력 경계).

역산 결과:

| N | Count = ceil(N/30) | AngleStep |
|---|---|---|
| 100 | 4 | 90.0° |
| 480 (기본값) | 16 | 22.5° |
| 1000 | 34 | 10.588° |
| 5000 | 167 | 2.156° |

### 3. `Source/Project_RE/Core/REBossCharacter.cpp` (수정)
파일 스코프 CVar 선언:
```cpp
static TAutoConsoleVariable<int32> CVarBulletCount(
    TEXT("re.Bullets.Count"), 480,
    TEXT("목표 동시 탄환 수. 다음 발사부터 반영."),
    ECVF_Cheat);
```
`TriggerBulletPattern`의 **Spiral 케이스만** 교체:
```cpp
case EBulletPattern::Spiral:
{
    const int32 N = CVarBulletCount.GetValueOnGameThread();
    REBulletPattern::FSpiralParams SP =
        REBulletPattern::MakeSpiralForLiveCount(N, SpiralBaseAngleDeg);
    Params = REBulletPattern::GenerateSpiral(GetActorLocation(), SP);
    UE_LOG(LogTemp, Log, TEXT("[RE] Boss Spiral: Target=%d BaseAngle=%.1f -> N=%d"),
        N, SpiralBaseAngleDeg, Params.Num());
    SpiralBaseAngleDeg += SpiralRotationStepDeg;
    break;
}
```
시그니처·Fan·Homing·`bIsDead` 가드·나머지 전부 불변.

### 4. `Source/Project_RE/Core/REGameMode.cpp:67` (수정, 1줄)
```cpp
GetWorld()->GetTimerManager().SetTimer(
    DemoFireTimer, FireDel, REBulletPattern::FireIntervalSec, /*bLoop=*/true);
```
`0.1f` 리터럴 제거 → 상수 참조. `REBulletPatternGenerator.h`는 이미 include 돼 있다. BeginPlay의 나머지 프로브 코드는 미접촉.

### 데이터 흐름
```
콘솔 `re.Bullets.Count 1000`
   → (다음 발사 시점) Boss::TriggerBulletPattern → CVar.GetValueOnGameThread() = 1000
   → MakeSpiralForLiveCount(1000, BaseAngle) → Count=34, AngleStep=10.588°
   → GenerateSpiral → SpawnBulletBatch (34 엔티티, Lifetime=3s)
   → SimProcessor: 3초 후 파괴
   → steady-state live ≈ 34 / 0.1 × 3 = 1020  (N=1000 ±2%)
   → RenderProcessor RenderProbe: `[RE] RenderProbe: live=1020 ISM.Count=1020` (30틱마다)
```

## 검증 (자동 테스트 인프라 없음 → 빌드 + headless 프로브)
게이트:
1. **에디터 빌드 성공** (에러 0):
   ```
   "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" -WaitMutex -NoHotReload
   ```
   (`MSYS_NO_PATHCONV=1` 필요)
2. **headless 프로브 3회** — 기존 M0/M1 프로브 패턴(`[[headless-runtime-probe]]`) 재사용. CVar는 `-ExecCmds`로 주입, 새 파일 생성 없음 (`Config/`·`scripts/`는 #44 소유):
   ```bash
   MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
     "E:\UnrealProjects\Project_RE-scale-knob\Project_RE.uproject" /Game/Level/Main \
     -game -nullrhi -unattended -nosplash -stdout -NoSound \
     -ExecCmds="re.Bullets.Count 1000" -log=RE_scale1000.log &
   sleep 30
   grep -E "RenderProbe|Boss Spiral" "Saved/Logs/RE_scale1000.log" | tail -20
   "/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
   ```
   `100`, `1000`, `5000` 각각 실행하고 로그의 `[RE] RenderProbe: live=` 값이 **수명(3s) 경과 후 N ±10%**에서 안정하는지 확인.
   - `[RE] Boss Spiral: Target=N -> N=Count` 로그로 역산값(4 / 34 / 167)도 함께 확인.
3. **균등 링 확인** — `GenerateSpiral` 각도는 `BaseAngle + i × (360/Count)`이므로 Count와 무관하게 링이 닫힌다. `AngleStep × Count == 360`을 위 로그의 Count로 산술 확인.

**폴백:** `-nullrhi`에서 `UREBulletRenderSubsystem`의 ISM이 없으면 `RenderProcessor::Execute`가 early-return(`REBulletRenderProcessor.cpp:39-42`)해 `live=` 로그가 안 찍힌다. 그 경우 실RHI `-windowed`로 동일 측정한다 (판정은 구현 후 실측으로).

## 스코프 경계 (손대지 말 것)
- **파일 소유권:** 내 소유는 `Core/REGameMode.*`, `Core/REBossCharacter.*`, `Mass/REBulletPatternGenerator.*` **뿐**.
  - `Mass/REBulletSimProcessor.cpp`, `REBulletRenderProcessor.cpp`, `REBulletHitProcessor.cpp`, `Config/`, `scripts/` → **#44 소유. 절대 수정 금지** (읽기만).
  - 신규 `Baseline/` 폴더 → **#45 소유.**
- 스폰 성능 최적화 (`REBulletSpawnSubsystem.cpp:49-55`의 per-entity `CreateEntity` 루프) → **측정 전 최적화 금지.** N=5000이면 발사당 167 엔티티 × 0.1s 주기로 부하가 크지만, 이건 이번 이슈의 **측정 대상**이지 수정 대상이 아니다. 병목 수치가 나오면 #46.
- 실제 CPU 시간 측정·프로파일링 → **#46.**
- 패턴 다양화(Fan/Homing 스케일링) → 범위 밖.
- 탄환 수에 따른 게임 밸런스(5000발이면 즉사) → 측정용 모드지 플레이용이 아님.
- BeginPlay의 M0/M1 프로브 코드 정리 → 범위 밖 (미접촉 결정).

## 커밋 계획 (Conventional Commits, 태스크당 1커밋)
1. `feat(M3): derive spiral count from target live bullet count (#43)` — 제너레이터 상수 + `MakeSpiralForLiveCount` 순수 함수
2. `feat(M3): add re.Bullets.Count CVar to scale bullet volume (#43)` — Boss CVar 조회 + GameMode 타이머 상수화
3. `test(M3): headless probe for 100/1000/5000 live bullet counts (#43)` — 프로브 실행 + 로그 확인

브랜치: `dev`에서 분기 → `feature/M3-bullet-scale-knob` (worktree `E:/UnrealProjects/Project_RE-scale-knob`).
