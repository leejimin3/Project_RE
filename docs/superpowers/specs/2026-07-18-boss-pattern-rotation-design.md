# M5 #64 보스 탄막 패턴 로테이션 Design

**이슈:** #64 (M5: 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격)
**날짜:** 2026-07-18
**선행:** #16 패턴 제너레이터(Spiral/Fan) 완료, PR#62 오픈루프 발수, PR#63 Lumen 제외 머지 완료

## 목표

보스가 **Spiral 단일 패턴만 반복 발사**하던 것을 **랜덤 패턴 로테이션**으로 바꾼다. 페이즈 단위로 패턴을 골라 고정 시간 동안 발사하고, 페이즈 사이에 대기(rest)를 둔다. 부수적으로 Fan을 실사용 경로에 편입(플레이어 조준)하고, Spiral의 회전 스텝을 조정해 "직선 방사"로 보이던 문제를 나선형으로 개선한다.

시각적 근거: 현재 `SpiralRotationStepDeg=15°`가 360/16=22.5° 링 간격과 근사 정합이라, 연속 발사 시 탄이 거의 같은 각도 라인 위에 겹쳐 쌓여 **모든 방향으로 직선 살**처럼 보인다. 회전 스텝을 링 간격과 비정합(non-commensurate)하게 만들면 매 발사 각도가 어긋나 나선 팔이 휘어진다.

## 배경 (현재 상태 — 소스 대조)

- `EBulletPattern` enum: `Spiral / Fan / Homing` (`REBulletPattern.h`). Spiral·Fan 구현 완료, Homing 미구현 스텁.
- `GenerateSpiral` / `GenerateFan`: 순수 함수 (`REBulletPatternGenerator`). Fan은 `FFanParams{CenterAngleDeg=0, SpreadDeg=90, Count=16}` 기본.
- `AREBossCharacter::TriggerBulletPattern(Pattern, Seed, StartTime)`: switch로 패턴별 스폰. Spiral case에 측정용 클로즈드루프(`re.Bullets.Count`) 분기 내장. Fan case는 구현돼 있으나 **호출하는 경로 없음**. Homing은 early-return.
- **발사 주체는 GameMode**: `AREGameMode::BeginPlay`가 보스 스폰 후 `DemoFireTimer`(0.1s 루프 람다)로 **Spiral만** 반복 트리거. `EndGame`이 `ClearTimer(DemoFireTimer)`로 정지.
- 측정 하네스: `scripts/profile.ps1`이 `re.Profiling.KeepFiring 1` + `re.Bullets.Count N`으로 실행 → 게임오버 무시하고 Spiral 클로즈드루프 연속 발사. `AREGameMode::EndGame`은 `KeepFiring != 0`이면 early-return.

## 설계 결정 (사전 확정)

1. **발사 주체를 GameMode → Boss로 이관.** GameMode는 `Boss->StartFiring(Seed)` 1회 호출, `EndGame`에서 `Boss->StopFiring()`. 페이즈 상태(현재 패턴, 남은 시간, 대기)를 Boss가 소유. 근거:
   - M5는 "서버가 페이즈 전환마다 패턴P+시드S+시각T를 RPC 브로드캐스트" 구조 — 보스가 권위 소유자라 페이즈 상태를 보스가 갖는 게 RPC 확장에 자연스럽다 (기존 `TriggerBulletPattern`의 `// TODO M5: Multicast_TriggerPattern` 주석과 정합).
   - GameMode `BeginPlay`가 이미 비대. 발사 로직을 옮기면 GameMode 다이어트.
2. **랜덤 = `FRandomStream(Seed)`.** 페이즈마다 `RandRange(0, 1)`로 Spiral/Fan 선택. 같은 시드 → 같은 패턴 순서(결정성) → M5 시드 동기화의 선행. 연속 중복 허용(2종에서 중복 회피는 강제 교대라 랜덤 의미 소멸).
3. **페이즈 시간 상수 (헤더, 추후 튜닝):** `SpiralPhaseSec=5`, `FanPhaseSec=3`, `RestSec=1`, `FanFireIntervalSec=0.5`. Spiral 발사 주기는 기존 `BossFireInterval`(0.1s) 유지.
4. **Fan 플레이어 조준.** 발사 시점 `GetFirstPlayerController()->GetPawn()` 위치로 CenterAngle 계산(atan2). 폰 없으면 0° 폴백. (TODO M5: 멀티는 타깃 선택 필요 — 주석만.)
5. **Spiral 회전 스텝 조정.** `SpiralRotationStepDeg` 15° → 비정합 후보를 실RHI 스크린샷 비교 후 확정. 후보: 황금각 근사 137.5°, 소각 9.7° 등. 판단 기준: 직선 방사 소멸 + 나선 팔 가시성.
6. **측정 하네스 보존.** `KeepFiring != 0`이면 페이즈 로테이션을 우회하고 Spiral 클로즈드루프 연속 발사 유지 → `profile.ps1` 회귀 없음.

## 변경 범위

**수정 3개, 신규 0:**

| 파일 | 변경 |
|------|------|
| `Source/Project_RE/Core/REBossCharacter.h` | `StartFiring`/`StopFiring` 선언, 페이즈 상태 멤버 + 상수, `SpiralRotationStepDeg` 값 변경 |
| `Source/Project_RE/Core/REBossCharacter.cpp` | 페이즈 상태머신 구현, Fan 조준, KeepFiring 우회 |
| `Source/Project_RE/Core/REGameMode.cpp` | `DemoFireTimer` 람다 → `StartFiring`/`StopFiring` 호출로 교체 |

`REBulletPatternGenerator`(순수 함수)는 무변경 — 조준은 CenterAngle 인자로 이미 지원. Build.cs / .uproject / Settings 변경 없음.

## 컴포넌트

### 1. `AREBossCharacter` — 페이즈 스케줄러

**헤더 추가:**
```cpp
public:
    /** 페이즈 로테이션 발사 시작. Seed로 패턴 순서 결정(M5 시드 동기화 선행). */
    void StartFiring(int32 Seed);
    /** 발사 정지. 이미 뜬 탄은 수명까지 유지(일괄 소멸 안 함). */
    void StopFiring();

private:
    void BeginPhase();      // 다음 패턴 랜덤 선택 + 발사 타이머 세팅 + 페이즈 종료 타이머 예약
    void FireCurrentPattern();  // 현재 페이즈 패턴 1회 발사 (발사 타이머 콜백)
    void EndPhase();        // 발사 타이머 정지 + RestSec 뒤 BeginPhase 예약

    FRandomStream PhaseRng;
    EBulletPattern CurrentPhasePattern = EBulletPattern::Spiral;
    bool bFirstPhase = true;      // 첫 페이즈 Spiral 고정 (오프닝 + profiling 오염 창 차단)
    FTimerHandle FireTimer;       // 페이즈 내 발사 반복
    FTimerHandle PhaseTimer;      // 페이즈 종료/대기 전환

    static constexpr float SpiralPhaseSec     = 5.f;
    static constexpr float FanPhaseSec        = 3.f;
    static constexpr float RestSec            = 1.f;
    static constexpr float FanFireIntervalSec = 0.5f;
```

`SpiralRotationStepDeg`는 값만 변경 (스크린샷 비교로 확정, 문서 갱신).

**상태머신 흐름:**
```
StartFiring(Seed):
    PhaseRng.Initialize(Seed)
    bFirstPhase = true
    BeginPhase()               # 즉시 1회, 이후 타이머로 재진입

BeginPhase():
    if (KeepFiring != 0):       # 측정 모드 — 로테이션/대기 우회, Spiral 연속
        CurrentPhasePattern = Spiral
        FireTimer 세팅(BossFireInterval, loop) → FireCurrentPattern
        return                  # PhaseTimer 예약 안 함 → 페이즈 종료/대기 없음
    # 첫 페이즈는 Spiral 고정: (a) 보스 오프닝 시그니처, (b) profiling에서
    # ExecCmds(KeepFiring)가 BeginPlay 직후 세팅되기 전 첫 페이즈가 Fan을 뽑아
    # 측정을 오염시키는 창을 닫는다. 이후 페이즈만 랜덤.
    if (bFirstPhase):
        CurrentPhasePattern = Spiral; bFirstPhase = false
    else:
        CurrentPhasePattern = (PhaseRng.RandRange(0,1) == 0) ? Spiral : Fan
    페이즈시간   = (Spiral) ? SpiralPhaseSec : FanPhaseSec
    발사주기     = (Spiral) ? BossFireInterval : FanFireIntervalSec
    FireTimer 세팅(발사주기, loop) → FireCurrentPattern
    PhaseTimer 세팅(페이즈시간, once) → EndPhase
    로그: [RE] Boss Phase: <패턴> <시간>s

FireCurrentPattern():
    TriggerBulletPattern(CurrentPhasePattern, Seed, 0)

EndPhase():
    ClearTimer(FireTimer)
    PhaseTimer 세팅(RestSec, once) → BeginPhase
    로그: [RE] Boss Phase: Rest <RestSec>s

StopFiring():
    ClearTimer(FireTimer); ClearTimer(PhaseTimer)
```

**KeepFiring 우회 (타이밍 주의):** `re.Profiling.KeepFiring`은 `profile.ps1`의 `-ExecCmds`로 **월드 로드 후** 세팅된다. GameMode `BeginPlay`(=`StartFiring` 호출 시점)엔 아직 미세팅일 수 있어, **`StartFiring`에서 1회 읽으면 안 된다** — 프로파일링이 로테이션 모드로 잘못 진입해 측정이 무효화된다(기존 코드가 KeepFiring을 런타임 `EndGame`/`TakeDamage`에서만 읽는 이유). 대신 **`BeginPhase`(타이머 재진입 콜백)에서 매번 조회**한다. `StartFiring`은 `BeginPhase`를 즉시 1회 호출하지만, KeepFiring 상태에 관계없이 첫 `BeginPhase`가 로테이션이든 연속이든 발사를 시작하고, ExecCmds가 그 뒤 세팅돼도 다음 `BeginPhase` 재진입 시 우회로 수렴한다. 우회 경로는 `PhaseTimer`를 예약하지 않아 `EndPhase`/rest 없이 Spiral을 `BossFireInterval`로 연속 발사 → profile.ps1 클로즈드루프 전제 충족.

### 2. `TriggerBulletPattern` — Fan 조준

Fan case 수정:
```cpp
case EBulletPattern::Fan:
{
    REBulletPattern::FFanParams FP;
    // 플레이어 방향 조준. 폰 없으면 0°(기존 기본) 폴백.
    // TODO M5: 멀티는 타깃 선택 필요 — 지금은 첫 플레이어 고정.
    if (APawn* Target = GetWorld()->GetFirstPlayerController()
            ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr)
    {
        const FVector D = Target->GetActorLocation() - GetActorLocation();
        FP.CenterAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
    }
    Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
    UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Center=%.1f Spread=%.1f -> N=%d"),
        FP.CenterAngleDeg, FP.SpreadDeg, Params.Num());
    break;
}
```
Spiral case는 무변경 (클로즈드루프 분기 그대로).

### 3. `AREGameMode` — 발사 위임

`BeginPlay`의 보스 스폰 블록:
```cpp
if (AREBossCharacter* Boss = GetWorld()->SpawnActor<...>(...))
{
    DemoBoss = Boss;
    Boss->StartFiring(12345);   // 시드 고정 — 결정적 패턴 순서
}
```
- 기존 `Boss->TriggerBulletPattern(Spiral)` 2회 프로브 + `DemoFireTimer` 람다 삭제.
- `#16 프로브(SpiralProbe/FanProbe)` 순수 함수 검증 블록은 유지(제너레이터 회귀 감시).

`EndGame`:
```cpp
// 기존: GetWorld()->GetTimerManager().ClearTimer(DemoFireTimer);
if (DemoBoss) { DemoBoss->StopFiring(); }
```
`DemoFireTimer` 멤버 삭제 (Boss가 타이머 소유). `DemoBoss` 멤버는 유지.

## 검증 (완료 기준)

Headless 프로브(`-game -nullrhi`, `[[headless-runtime-probe]]` 방식) + 실RHI 스크린샷(`[[ue_visual_verify_screenshot]]`).

1. **페이즈 전환 시퀀스** — headless 로그에 `Boss Phase: Spiral 5.0s` / `Boss Phase: Rest 1.0s` / `Boss Phase: Fan 3.0s` 교차 출현. 발사가 페이즈 시간만큼 지속되고 대기 구간엔 `Boss Fan/Spiral: N=` 미출현.
2. **결정성** — 같은 시드(12345) 2회 실행 → 동일 패턴 순서 로그. (M5 시드 동기화 선행 확인.)
3. **Fan 조준** — 로그 `Boss Fan: Center=<θ>`가 보스→플레이어 방향각과 일치. 정지 플레이어 위치(원점 부근)면 보스(600,0)에서 ≈180°.
4. **Spiral 곡선감** — 실RHI 스크린샷으로 후보 스텝값 비교. 직선 방사 소멸 + 나선 팔 확인. 확정값을 본 문서와 헤더 주석에 기록.
5. **측정 하네스 회귀 없음** — `re.Profiling.KeepFiring 1 re.Bullets.Count 1000`로 실행 → 로테이션 우회, Spiral 연속 발사, RenderProbe 라이브 카운트 목표 수렴(기존 동작 유지).
6. **빌드 게이트** — `Build.bat Project_REEditor` Succeeded.

## 스코프 밖 (YAGNI)

- **Homing 구현** — 이번 라운드 제외(사용자 결정). 스텁 유지.
- **패턴 파라미터 Settings 노출** — 상수로 시작. DefaultGame.ini 노출은 후속.
- **M5 RPC 브로드캐스트** — `TriggerBulletPattern`의 시드 동기화 실배선은 M5 본작업. 본 이슈는 싱글 로컬 페이즈 로테이션 + 결정적 시드 순서까지.
- **패턴 종류 추가(3종+)** — Spiral/Fan 2종 로테이션만.
