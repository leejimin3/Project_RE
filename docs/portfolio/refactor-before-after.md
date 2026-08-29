# 리팩터링 코드 전후 비교

**작성일:** 2026-08-29 · **구현 완료:** 2026-08-30 · **성격:** **실행 기록**
**계획 문서:** [`refactor-plan.md`](refactor-plan.md) (왜 고치는지·위험·게이트는 그쪽)
**구현:** `feature/M8-portfolio-hardening` (#141) · 커밋 15개

> **읽는 법**
> - `Before` 블록은 리팩터 전 리포지토리에서 그대로 옮긴 것이다. 파일:줄 표기는 2026-08-29 기준.
> - `After` 블록은 **제안**이었고, 실제 적용과 달라진 곳은 각 항목의 **"제안과 달라진 것"**에
>   적었다. 제안이 실물과 어긋난 경우가 넷 있었다(R-01 파일 수, R-02 스크립트, R-04 두 값, R-07 상수 이름).
> - 게이트 체크박스는 실행 결과로 채웠다.
> - **R-06 은 기각됐다.** 측정이 판정했다 — [`baseline/perf-r06.md`](baseline/perf-r06.md).
> - 기준선과 재현 절차: [`baseline/README.md`](baseline/README.md)

---

## 목차

| ID | 제목 | 주 파일 |
|---|---|---|
| [R-01](#r-01) | 템플릿 잔재 제거 | `Project_RE.Build.cs`, `Project_RE.uproject` |
| [R-02](#r-02) | 로그 카테고리 신설 | `Project_RE.h/.cpp` (신설), 전 `.cpp` |
| [R-03](#r-03) | 널·불변식 계약 명시화 | `REAttackComponent.cpp`, `REBossCharacter.cpp` 외 |
| [R-04](#r-04) | 패턴 정의 단일 출처 테이블 | `REBossPatternTable.h` (신설), `REBossCharacter.*` |
| [R-05](#r-05) | `Multicast_FireArtillery_Implementation` 분해 | `REBossCharacter.cpp` |
| [R-06](#r-06) | Mass 배치 스폰 | `REBulletSpawnSubsystem.cpp` |
| [R-07](#r-07) | 히트 반경 결합 해소 + 데미지 이관 | `REBulletGeometry.h` (신설), `REStatsSettings.h` |
| [R-08](#r-08) | 헤드리스 프로브 분리 | `REHeadlessProbeComponent.*` (신설) |

---

<a name="r-01"></a>
## R-01 — 템플릿 잔재 제거

### ① 디렉터리

**Before** — `Source/Project_RE/` 142파일 중

```
Variant_Combat/          18 파일   (CombatCharacter, CombatAIController, StateTree 유틸, EQS 컨텍스트 …)
Variant_Platforming/      8 파일
Variant_SideScrolling/    6 파일
```

참조 검사:

```bash
grep -rl "Variant_Combat\|Variant_Platforming\|Variant_SideScrolling" \
  Source/Project_RE --include=*.cpp --include=*.h | grep -v "^Source/Project_RE/Variant_"
# → 출력 없음 (실사용 코드에서 참조 0)
```

**After** — 세 디렉터리 삭제.

### 제안과 달라진 것 — 파일 수가 32가 아니라 591이었다

실측은 **76파일**(38 cpp + 38 h)이다. 그리고 에셋도 같이 지워야 했다:
`DefaultGame.ini` 에 `DirectoriesToNeverCook` 이 없어 `Lvl_Combat` / `Lvl_SideScrolling` 도
매번 쿡된다. C++ 만 지우면 그 맵들이 부모 클래스를 못 찾아 쿡 경고가 난다 — 게이트 4 가 깨진다.

```
Source/Project_RE/Variant_{Combat,Platforming,SideScrolling}/     76
Content/Variant_*/                                                 61
Content/__ExternalActors__/Variant_*/                             414
Content/__ExternalObjects__/Variant_*/                             40
                                                                  ───
                                                                  591
```

`DefaultEngine.ini` 의 `ActiveClassRedirects` 는 건드리지 않았다 — 가리키는 대상이
`TP_ThirdPerson*` → `Project_RE*` 이고 Variant 와 무관하다.

### ② `Project_RE.Build.cs`

**Before**

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
    "AIModule",
    "StateTreeModule",              // ← Variant_Combat 전용
    "GameplayStateTreeModule",      // ← Variant_Combat 전용
    "UMG", "Slate", "SlateCore",
    "MassEntity", "MassCore", "GameplayAbilities", "Niagara",
    "GameplayTags", "GameplayTasks", "NavigationSystem", "DeveloperSettings"
});

PrivateDependencyModuleNames.AddRange(new string[] { });

PublicIncludePaths.AddRange(new string[] {
    "Project_RE",
    "Project_RE/Baseline",
    "Project_RE/Core",
    "Project_RE/Mass",
    "Project_RE/UI",
    "Project_RE/Variant_Platforming",                 // ← 이하 13줄 전부 잔재
    "Project_RE/Variant_Platforming/Animation",
    "Project_RE/Variant_Combat",
    "Project_RE/Variant_Combat/AI",
    "Project_RE/Variant_Combat/Animation",
    "Project_RE/Variant_Combat/Gameplay",
    "Project_RE/Variant_Combat/Interfaces",
    "Project_RE/Variant_Combat/UI",
    "Project_RE/Variant_SideScrolling",
    "Project_RE/Variant_SideScrolling/AI",
    "Project_RE/Variant_SideScrolling/Gameplay",
    "Project_RE/Variant_SideScrolling/Interfaces",
    "Project_RE/Variant_SideScrolling/UI"
});

// Uncomment if you are using Slate UI
// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
// Uncomment if you are using online features
// PrivateDependencyModuleNames.Add("OnlineSubsystem");
```

**After**

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
    // AIModule: REPlayerController 가 UAIBlueprintHelperLibrary::SimpleMoveToLocation 을 쓴다.
    // 이름만 보고 지우면 우클릭 이동이 죽는다.
    "AIModule",
    "UMG", "Slate", "SlateCore",
    "MassEntity", "MassCore", "GameplayAbilities", "Niagara",
    "GameplayTags", "GameplayTasks", "NavigationSystem", "DeveloperSettings"
});

PublicIncludePaths.AddRange(new string[] {
    "Project_RE",
    "Project_RE/Abilities",
    "Project_RE/Baseline",
    "Project_RE/Core",
    "Project_RE/Mass",
    "Project_RE/UI"
});
```

> `Abilities` 는 원래 목록에 없었다. 지금은 모듈 루트 상대경로(`"Abilities/REGameplayTags.h"`)로
> 우회하고 있어 동작하지만, 다른 디렉터리와 규칙이 어긋난다. 같이 맞춘다.
> 삭제한 `StateTreeModule` / `GameplayStateTreeModule` 은 실사용 코드 참조 0 —
> 검사: `grep -rn "StateTree" Source/Project_RE/{Core,Mass,UI,Abilities,Baseline}` → 출력 없음.
> 주석 처리된 예시 두 줄은 템플릿 잔재라 같이 지운다(`Slate`/`SlateCore` 는 이미 Public 에 있다).

### ③ `Project_RE.uproject`

**Before**

```
ModelingToolsEditorMode      True
StateTree                    True      ← 잔재
GameplayStateTree            True      ← 잔재
MassGameplay                 True
GameplayAbilities            True
ModelContextProtocol         True      ← 개발 도구
Niagara                      True
PythonScriptPlugin           True      ← scripts/*.py 가 쓴다. 유지
CascadeToNiagaraConverter    True      ← 잔재 (변환은 이미 끝났다)
```

**After** — `StateTree`, `GameplayStateTree`, `CascadeToNiagaraConverter` 를 `"Enabled": false`.

> `ModelContextProtocol` 은 개발 도구다. 쿡에 영향이 있는지 확인 후 판단 — **불확실하면 남긴다.**
> 플러그인 변경은 **별도 커밋**으로 분리한다 (롤백 지점).

### 게이트 — 전부 통과

- [x] Development Editor 빌드 통과, 신규 경고 0
- [x] **풀 유니티 빌드** 통과
- [x] 쿡 경고 없음 — `BuildCookRun` **0 error / 0 warning**. Variant 맵 515개를 지운 뒤에도 동일
- [x] `dedi-verify.ps1 -Clients 2` PASS (21/21)

**`ModelContextProtocol` 은 남겼다** — 개발 도구이고 쿡 영향이 불확실하다. 계획 규칙: 불확실하면 남긴다.

**`StateTree` 는 uproject 에서 껐지만 여전히 마운트된다.** `MassGameplay.uplugin` 이
`StateTree` 를 의존하기 때문이다(ZoneGraph / PoseSearch / SmartObjects / StateTree /
DataValidation). 이 변경이 없애는 것은 "이 프로젝트가 StateTree 를 **직접** 쓴다"는 거짓
선언이지 플러그인 로드 자체가 아니다. 헤드리스 로그로 확인했다 — `GameplayStateTree` 와
`CascadeToNiagaraConverter` 는 로드 0회가 됐고 `StateTree` 만 남는다.

---

<a name="r-02"></a>
## R-02 — 로그 카테고리 신설

**현황:** `UE_LOG` 77회, 전부 `LogTemp`. 전용 카테고리 0.

### 선언 (신설)

**Before** — 없음.

**After** — `Source/Project_RE/Project_RE.h`

```cpp
// 게임루프 공통 (GameMode / CharacterBase / PlayerController)
DECLARE_LOG_CATEGORY_EXTERN(LogRE, Log, All);
// Mass 탄막 (Sim / Render / Hit / Arc / Fx / SpawnSubsystem)
DECLARE_LOG_CATEGORY_EXTERN(LogREBullet, Log, All);
// 복제·RPC 경로 (Multicast 구현, 헤드리스 프로브)
DECLARE_LOG_CATEGORY_EXTERN(LogRENet, Log, All);
```

`Source/Project_RE/Project_RE.cpp`

```cpp
DEFINE_LOG_CATEGORY(LogRE);
DEFINE_LOG_CATEGORY(LogREBullet);
DEFINE_LOG_CATEGORY(LogRENet);
```

> 기본 verbosity `Log`, 컴파일 상한 `All` — `LogTemp` 와 동일하게 맞춘다.
> 다르게 잡으면 기존 `Verbose` 로그가 조용히 사라진다.

### 치환 — 카테고리만 바꾼다

**Before** — `Mass/REBulletHitProcessor.cpp:85`

```cpp
UE_LOG(LogTemp, Verbose, TEXT("[RE] BulletHit: dash-destroy (무적, 데미지 없음)"));
```

**After**

```cpp
UE_LOG(LogREBullet, Verbose, TEXT("[RE] BulletHit: dash-destroy (무적, 데미지 없음)"));
```

**Before** — `Core/REBossCharacter.cpp:436`

```cpp
UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: %s %.1fs"), PhaseName, PhaseSec);
```

**After**

```cpp
UE_LOG(LogRE, Log, TEXT("[RE] Boss Phase: %s %.1fs"), PhaseName, PhaseSec);
```

> **`[RE]` 접두어와 메시지 본문은 한 글자도 바꾸지 않는다.** `dedi-verify.ps1` 과
> 헤드리스 프로브 판정이 이 문자열을 grep 한다. 바뀌는 건 첫 인자뿐이다.

### 제안과 달라진 것 — `dedi-verify.ps1` 에 `-LogCmds` 는 없었다

계획은 스크립트가 `-LogCmds="LogTemp Verbose"` 를 하드코딩하고 있다고 적었지만 **없다.**
판정 정규식이 메시지 본문만 보므로 카테고리 변경으로 죽는 판정도 없다:

```powershell
Assert-Log 'server' '탄막 발사(권위)' $ServerLines '\[RE\] Boss Fire(Direct|Artillery):.*role=ROLE_Authority'
```

대신 SelfTest 합성 로그의 `'LogTemp: '` 접두어 29곳을 실제 카테고리로 맞췄다 —
합성 로그가 실물과 다르면 자기검사가 실물을 대변하지 못한다.

```powershell
# Before
'LogTemp: [Move] probe start: pawn=X=0 target=X=0',
# After
'LogRENet: [Move] probe start: pawn=X=0 target=X=0',
```

Verbose 를 켜는 이득 자체는 그대로다 — 이제 `-LogCmds="LogREBullet Verbose"` 로
대쉬 무적 판정 로그만 딱 켠다. 전에는 그러려면 엔진 전체를 켜야 했다.

### 적용 분포 (84곳)

| 카테고리 | 곳 | 범위 |
|---|--:|---|
| `LogRE` | 35 | GameMode / CharacterBase / PlayerController 로컬 / 보스 페이즈·발사 결정 / 어빌리티 |
| `LogREBullet` | 19 | Sim / Render / Hit / Arc / Fx / SpawnSubsystem / Actor 비교군 |
| `LogRENet` | 27 | Multicast 구현, 서버/클라 RPC, 헤드리스 프로브 |

같은 파일 안에서 갈리는 두 파일(`REBossCharacter` / `REPlayerController`)은 **감싸는
함수**로 정했다 — RPC 구현과 프로브가 `LogRENet`, 나머지가 `LogRE`.

`LogProject_RE`(템플릿 기본 카테고리)는 남겼다. 모듈 루트 템플릿 파일 두 곳
(`Project_RECharacter.cpp`, `Project_REPlayerController.cpp`)이 실제로 쓰고 있고,
그 파일들은 계획이 범위 밖으로 둔 것이다.

### 게이트 — 전부 통과

- [x] `grep -rc "UE_LOG(LogTemp" Source/Project_RE` → **0**
- [x] `dedi-verify.ps1 -Clients 2` PASS (21/21)
- [x] `dedi-verify.ps1 -SelfTest` 통과 (판정 엔진 + 신선도 자기검사 4/4)
- [x] 헤드리스 프로브 3종 완주, 로그 문자열 diff 0
- [x] **메시지 본문 불변을 기계로 확인**: `UE_LOG(Log*,` → `UE_LOG(CAT,` 로 정규화한 뒤
      리팩터 전 커밋과 diff → 카테고리를 신설한 두 파일 외 **16파일 전부 차이 0**

---

<a name="r-03"></a>
## R-03 — 널·불변식 계약 명시화

**현황:** `check`/`checkf`/`ensure`/`ensureMsgf` **0건**. `GetWorld()` 40회 중 17회가 널 검사 없이 즉시 역참조.

### 먼저 정책을 코드에 적는다

**After** — `Source/Project_RE/Project_RE.h` (R-02 와 같은 파일)

```cpp
/**
 *  이 모듈의 실패 처리 규약.
 *
 *  1) 정상적으로 발생 가능한 부재  → if 폴백 + UE_LOG. 예외가 아니라 설계된 경로다.
 *     (데디에 ISM 없음 / 폰 없음 / 전원 사망 / 접속 직후 GameState 미복제)
 *  2) 프로그래머 실수로만 발생      → ensureMsgf + 조기 반환.
 *     개발 빌드에선 콜스택 + 메시지가 남고, 쉬핑에선 컴파일 아웃되어 조기 반환만 남는다.
 *  3) 진행하면 데이터가 깨짐        → checkf. 현재 이 모듈엔 해당 경로가 없다.
 *
 *  금지: 로그 없는 early-return. 조용한 실패가 이 프로젝트의 측정을 두 번 망쳤다 (#50, #88).
 *  금지: Mass 프로세서의 워커 스레드 경로에서 ensure — 발화 순서가 보장되지 않는다.
 *        (bRequiresGameThreadExecution=false 인 프로세서: REBulletSimProcessor, REArcSimProcessor)
 */
```

### 사례 ① — 부류 2 (프로그래머 실수)

**Before** — `Core/REAttackComponent.cpp:54,68`

```cpp
const double Now = GetWorld()->GetTimeSeconds();
...
const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);
```

컴포넌트가 등록됐는데 월드가 없는 상황은 정상 경로에 없다. 그런데 지금은 널이면
`EXCEPTION_ACCESS_VIOLATION` 으로 **원인 메시지 없이** 죽는다.

**After**

```cpp
UWorld* World = GetWorld();
if (!ensureMsgf(World, TEXT("[RE] AttackComponent: World 없음 — 등록된 컴포넌트에선 불가능한 상태")))
{
    return;   // 쉬핑에선 ensure 가 컴파일 아웃되고 이 반환만 남는다
}
const double Now = World->GetTimeSeconds();
...
const bool bBlockingHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);
```

> 부수 효과: `GetWorld()` 호출이 2회 → 1회. 의도가 아니라 덤이다.

### 사례 ② — 부류 1 (정상 부재) + 일관성

**Before** — `Core/REBossCharacter.cpp:910`

```cpp
int32 CurrentLive = -1;
if (const UREBulletRenderSubsystem* RS = GetWorld()->GetSubsystem<UREBulletRenderSubsystem>())
{
    if (const UInstancedStaticMeshComponent* ISM = RS->GetISM())
    {
        CurrentLive = ISM->GetInstanceCount();
    }
}
```

`RS` 가 없는 건 **정상**이다 — 데디 서버엔 ISM 이 없다(그래서 `-1` 폴백이 있다).
문제는 `GetWorld()` 자체를 안 막는 것이고, 같은 파일 613줄에서는 막고 있다.

**After**

```cpp
int32 CurrentLive = -1;   // -1 = 라이브 관측 불가 → 피드포워드 폴백 (데디 등, 정상 경로)
if (const UWorld* World = GetWorld())
{
    if (const UREBulletRenderSubsystem* RS = World->GetSubsystem<UREBulletRenderSubsystem>())
    {
        if (const UInstancedStaticMeshComponent* ISM = RS->GetISM())
        {
            CurrentLive = ISM->GetInstanceCount();
        }
    }
}
```

> 여기엔 `ensure` 를 **쓰지 않는다.** 부재가 설계된 경로이고, 아래에서 `CurrentLive < 0` 을
> 이미 분기한다. `ensure` 를 달면 데디 서버에서 매 발사마다 오탐이 뜬다.

### 사례 ③ — 로그 없는 폴백 금지

**Before** — `Core/REBossCharacter.cpp:478` (`FireCurrentPattern` 도입부)

```cpp
if (bIsDead)
{
    return;
}
```

죽은 보스가 타이머 콜백을 받는 건 정상이다(타이머 정리와 사망 사이 한 틱). 이건 그대로 둔다.

**Before** — `Core/REBossCharacter.cpp:527` (조준 폴백)

```cpp
if (const APawn* Target = FindNearestLivingPlayerPawn())
{
    const FVector D = Target->GetActorLocation() - GetActorLocation();
    AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
}
// else: AngleDeg = 0 폴백 — 로그 없음
```

**After**

```cpp
if (const APawn* Target = FindNearestLivingPlayerPawn())
{
    const FVector D = Target->GetActorLocation() - GetActorLocation();
    AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
}
else
{
    // 전원 사망 — 곧 EndGame 이 발사를 끊는다. 그때까지 0도로 쏜다.
    // 로그가 없으면 "왜 갑자기 한 방향으로만 쏘는가"를 사후에 알 수 없다.
    UE_LOG(LogRE, Verbose, TEXT("[RE] Boss: no living target, AngleDeg=0 폴백"));
}
```

### 적용 대상 (17곳)

| 파일 | 줄 | 부류 |
|---|---|---|
| `Core/REAttackComponent.cpp` | 54, 68 | 2 (ensure) |
| `Core/REBossCharacter.cpp` | 910, 1129 | 1 (if 폴백) |
| `Core/RECharacterBase.cpp` | 398 | 1 |
| `Core/REGameMode.cpp` | 59, 87, 138 | 2 (GameMode 는 월드 없이 존재 불가) |
| `Core/REPlayerController.cpp` | 274, 386, 484, 487, 514, 567, 614, 616, 655, 691, 711, 713 | 대부분 2 |
| `Mass/REBulletSpawnSubsystem.cpp` | 12 | 이미 가드됨 — 변경 없음 |

> `REPlayerController` 의 프로브 관련 줄(614~713)은 **R-08 에서 파일이 통째로 이동한다.**
> R-03 을 R-08 앞에 두면 같은 줄을 두 번 만진다 — 그래서 실행 순서가 R-03 → R-08 이다.

### 실제 적용 — 25곳 중 12곳

`GetWorld()` 무가드 역참조는 실측 25곳이었다. 3곳은 이미 삼항으로 가드돼 있었고,
프로브 10곳은 R-08 에서 파일째 이동하므로 거기서 함께 처리했다. 남은 **12곳**이 대상이다.

`ensureMsgf` 는 **반드시 조기 반환과 짝**이다 — 쉬핑에선 ensure 가 컴파일 아웃되어
그 반환이 유일한 방어가 되기 때문이다. 반환값이 있는 함수(`TakeDamage`)에서는
계산된 `Applied` 를 그대로 돌려준다.

### 게이트 — 전부 통과

- [x] Development 빌드 통과, 신규 경고 0
- [ ] **Shipping 빌드** — 실행 중 (`ensure` 컴파일 아웃 후에도 조기 반환이 남는 것이 판정 대상)
- [x] 헤드리스 프로브 3종 완주 — 새 `ensure` 발화 0건
- [x] `dedi-verify.ps1 -Clients 2` PASS (21/21)
- [x] **게이트 10 PASS**: 워커 스레드 프로세서(`REBulletSimProcessor`, `REArcSimProcessor` —
      `bRequiresGameThreadExecution` 을 켜지 않는 둘)에 `ensure` 0건.
      새 `ensureMsgf` 6곳은 전부 게임 스레드 경로(컴포넌트/액터/GameMode/RPC 구현)다.

---

<a name="r-04"></a>
## R-04 — 패턴 정의 단일 출처 테이블 ★

### Before — 같은 지식이 7곳에 흩어져 있다

**(1) 계열 판별** — `Core/REBossCharacter.cpp:53~77`

```cpp
namespace REBoss
{
    static bool IsArcPattern(EBulletPattern P)
    {
        return P == EBulletPattern::Artillery
            || P == EBulletPattern::ArtilleryStorm
            || P == EBulletPattern::LissajousStorm
            || P == EBulletPattern::BezierVortex
            || P == EBulletPattern::RoseField
            || P == EBulletPattern::AerialDome
            || P == EBulletPattern::Spirograph
            || P == EBulletPattern::MicroMissile;
    }

    static bool IsBloomPattern(EBulletPattern P)
    {
        return P == EBulletPattern::StarBloom
            || P == EBulletPattern::LemniscateBloom
            || P == EBulletPattern::SuperformulaBloom;
    }
}
```

**(2) 로테이션 풀** — `:355`

```cpp
static const EBulletPattern Pool[15] = {
    EBulletPattern::Spiral, EBulletPattern::Fan, EBulletPattern::Artillery,
    EBulletPattern::ArtilleryStorm, EBulletPattern::RoseEnvelope, EBulletPattern::Cardioid,
    EBulletPattern::LissajousStorm, EBulletPattern::BezierVortex,
    EBulletPattern::StarBloom, EBulletPattern::LemniscateBloom, EBulletPattern::SuperformulaBloom,
    EBulletPattern::RoseField, EBulletPattern::AerialDome, EBulletPattern::Spirograph,
    EBulletPattern::MicroMissile };
```

**(3) 페이즈 길이 + 로그 이름** — `:397~431` (14 case)

```cpp
float PhaseSec = SpiralPhaseSec;
const TCHAR* PhaseName = TEXT("Spiral");
switch (CurrentPhasePattern)
{
case EBulletPattern::Fan:
    PhaseSec = FanPhaseSec; PhaseName = TEXT("Fan"); break;
case EBulletPattern::Artillery:
    PhaseSec = ArtilleryPhaseSec; PhaseName = TEXT("Artillery"); break;
// … 12 case 더
default: break;   // Spiral 기본값
}
```

**(4) 발사 간격** — `:455~476` (12 case)

```cpp
float AREBossCharacter::FireIntervalFor(EBulletPattern P)
{
    switch (P)
    {
    case EBulletPattern::Fan:            return FanFireIntervalSec;
    case EBulletPattern::Artillery:      return ArtilleryFireInterval;
    // … 10 case 더
    default:                             return REBulletPattern::FireIntervalSec();
    }
}
```

**(5) 외관** — `:165~236` (14 case), **(6) 곡사 파라미터** — `:632~661` (7단 if-else, R-05 에서 다룸)

### 문제의 크기

패턴 16번째를 추가하려면 **7곳**을 고쳐야 한다. 하나를 빠뜨려도 **컴파일이 통과한다** —
전부 `default:` 가 있다. 결과는 크래시가 아니라 조용한 오동작이다.

코드 자신이 이미 알고 있다 (`:56`):

> `// 두 곳에 각각 나열하면 패턴을 늘릴 때 한쪽만 고쳐 조용히 어긋난다.`

주석은 기록할 뿐 **막지 못한다.**

### After — 테이블 하나 + 컴파일 타임 검증

**신설** `Source/Project_RE/Core/REBossPatternTable.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "REBulletPattern.h"

namespace REBoss
{
    /** 발사 경로 분기. 세 계열이 서로 다른 스폰 진입점을 탄다. */
    enum class EFamily : uint8
    {
        Direct,   // 직선탄 — Multicast_FireDirect
        Bloom,    // 곡선 블룸(직선탄이지만 스폰 위치로 모양을 그린다) — Multicast_FireDirect
        Arc       // 곡사 — Multicast_FireArtillery
    };

    /** 보스 외관(#130). 패턴마다 (질감, 이미시브) 쌍이 겹치지 않게 배분돼 있다. */
    struct FBossLook
    {
        float        Snow = 0.f;
        float        Lava = 0.f;
        FLinearColor Emis = FLinearColor(1.f, 0.1f, 0.05f, 1.f);   // 기본 = Spiral 빨강
    };

    /** 곡사 계열 전용 파라미터. Family != Arc 이면 읽지 않는다. */
    struct FArcDef
    {
        float FlightTime = 0.f;
        float MaxHeight  = 0.f;
        int32 Count      = 0;
    };

    /**
     *  패턴 1종의 정의 전부. **이 구조체가 패턴 지식의 유일한 출처다.**
     *  패턴을 추가하려면 아래 테이블에 한 줄을 넣는다 — 넣지 않으면 컴파일이 실패한다.
     */
    struct FPatternDef
    {
        EBulletPattern Pattern;        // 자기 자신 (테이블 순서 검증용)
        EFamily        Family;
        const TCHAR*   Name;           // 로그 표기
        float          PhaseSec;       // 발사 구간 길이
        float          FireInterval;   // < 0 이면 ini 기본(REBulletPattern::FireIntervalSec())
        bool           bInRotation;    // 로테이션 풀 포함 여부
        FArcDef        Arc;
        FBossLook      Look;
    };

    /** 인덱스 == (int32)EBulletPattern. 아래 static_assert 가 강제한다. */
    extern const FPatternDef PatternTable[];
    extern const int32       PatternTableNum;

    /** 테이블 조회. 범위 밖이면 Spiral(인덱스 0) 폴백 + 로그. */
    const FPatternDef& GetPatternDef(EBulletPattern P);

    inline bool IsArcPattern  (EBulletPattern P) { return GetPatternDef(P).Family == EFamily::Arc;   }
    inline bool IsBloomPattern(EBulletPattern P) { return GetPatternDef(P).Family == EFamily::Bloom; }
}
```

**신설** `Source/Project_RE/Core/REBossPatternTable.cpp` (발췌 — 16행 전부는 구현 세션이 채운다)

```cpp
#include "REBossPatternTable.h"
#include "Project_RE.h"   // LogRE

namespace REBoss
{
// 상수는 REBossCharacter.h 의 기존 constexpr 를 **값 그대로** 옮긴다. 계산식 변경 금지.
const FPatternDef PatternTable[] =
{
    //  Pattern                       Family          Name                     Phase  Interval  Rot   Arc{Flight, Height, Count}     Look{Snow, Lava, Emis}
    {   EBulletPattern::Spiral,           EFamily::Direct, TEXT("Spiral"),            5.f,   -1.f,  true,  {},                            {} },
    {   EBulletPattern::Fan,              EFamily::Direct, TEXT("Fan"),               3.f,   0.5f,  true,  {},                            { FanSnowAmount, 0.f, FLinearColor(0.15f, 0.70f, 1.0f, 1.f) } },
    {   EBulletPattern::Homing,           EFamily::Direct, TEXT("Homing"),            0.f,   -1.f,  false, {},                            {} },   // #67 백로그 스텁 — 로테이션 제외
    {   EBulletPattern::Artillery,        EFamily::Arc,    TEXT("Artillery"),         /*…*/  },
    // … 나머지 12행
};

const int32 PatternTableNum = UE_ARRAY_COUNT(PatternTable);

/**
 *  테이블 인덱스가 enum 값과 1:1인지 컴파일 타임에 확인한다.
 *  이게 이 리팩터링의 목적 전부다 — 패턴을 추가하면서 테이블 행을 빠뜨리거나
 *  순서를 어긋내면 **빌드가 실패한다.** 지금은 조용히 오동작한다.
 */
constexpr bool IsTableOrdered()
{
    for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(PatternTable); ++i)
    {
        if ((int32)PatternTable[i].Pattern != i)
        {
            return false;
        }
    }
    return true;
}
static_assert(IsTableOrdered(), "PatternTable 순서가 EBulletPattern 과 어긋났다");
static_assert(UE_ARRAY_COUNT(PatternTable) == (int32)EBulletPattern::Count,
              "EBulletPattern 에 패턴을 추가했으면 PatternTable 에도 행을 추가하라");

const FPatternDef& GetPatternDef(EBulletPattern P)
{
    const int32 Idx = (int32)P;
    if (Idx >= 0 && Idx < PatternTableNum)
    {
        return PatternTable[Idx];
    }
    // 네트워크 페이로드로 들어온 값이라 범위 밖이 이론상 가능하다(구버전 클라 등).
    // 크래시 대신 Spiral 로 폴백하되 조용히 넘기지 않는다.
    UE_LOG(LogRE, Error, TEXT("[RE] GetPatternDef: 범위 밖 패턴 %d — Spiral 폴백"), Idx);
    return PatternTable[0];
}
} // namespace REBoss
```

**enum 에 센티널 추가** — `Mass/REBulletPattern.h`

```cpp
    MicroMissile,    // 동/서/남/북 윗대각선으로 뻗었다가 꺾여 플레이어 위치로 내리꽂는 4발.

    Count UMETA(Hidden)   // 테이블 크기 검증 전용. BP 드롭다운에서 숨긴다.
};
```

> 기존 값이 하나도 안 바뀌므로 **네트워크 페이로드 호환이 유지된다.**
> `UMETA(Hidden)` 이라 블루프린트 드롭다운에도 안 뜬다.

### 호출부 치환

**(2) 로테이션 풀 — Before**

```cpp
static const EBulletPattern Pool[15] = { /* 15개 나열 */ };
...
NewPattern = Pool[PhaseRng.RandRange(0, UE_ARRAY_COUNT(Pool) - 1)];
```

**After**

```cpp
// 로테이션 풀은 테이블에서 파생한다 — 손으로 나열하지 않는다.
// static 지역: 첫 호출에 1회 구성. 테이블이 constexpr 이라 결과가 불변이다.
static const TArray<EBulletPattern> Pool = []()
{
    TArray<EBulletPattern> P;
    for (int32 i = 0; i < REBoss::PatternTableNum; ++i)
    {
        if (REBoss::PatternTable[i].bInRotation)
        {
            P.Add(REBoss::PatternTable[i].Pattern);
        }
    }
    return P;
}();
...
NewPattern = Pool[PhaseRng.RandRange(0, Pool.Num() - 1)];
```

> **주의:** `Pool.Num()` 이 15 임을 게이트로 확인한다. 16이 되면
> `Homing` 이 로테이션에 섞여 "미구현" 경고만 찍고 아무것도 안 쏘는 페이즈가 생긴다.

**(3) 페이즈 길이 + 이름 — Before** (14 case switch, 35줄)

**After**

```cpp
const REBoss::FPatternDef& Def = REBoss::GetPatternDef(CurrentPhasePattern);
const float        PhaseSec  = Def.PhaseSec;
const TCHAR* const PhaseName = Def.Name;
```

**(4) 발사 간격 — Before** (12 case switch, 22줄)

**After**

```cpp
float AREBossCharacter::FireIntervalFor(EBulletPattern P)
{
    const float Interval = REBoss::GetPatternDef(P).FireInterval;
    // 음수 = "ini 기본을 쓴다". Spiral / RoseEnvelope / Cardioid 는 링 지오메트리가 같아 공유한다.
    return (Interval >= 0.f) ? Interval : REBulletPattern::FireIntervalSec();
}
```

**(5) 외관 — Before** (14 case switch, 72줄)

**After**

```cpp
REBoss::FBossLook AREBossCharacter::LookForPattern(EBulletPattern Pattern)
{
    return REBoss::GetPatternDef(Pattern).Look;
}
```

> 각 색 선택의 **근거 주석**(왜 이 패턴이 청록인지, 왜 Cardioid 는 질감까지 바꾸는지)은
> 테이블 행 위로 그대로 옮긴다. 이 프로젝트에서 그 주석이 자산이다 — 지우지 않는다.

### 이행 안전장치 (한 커밋 동안만)

```cpp
// 값을 옮기며 오타가 나면 게임플레이가 조용히 바뀐다. 원본 constexpr 를 아직 지우지 말고
// 컴파일 타임에 동일함을 증명한 뒤, 다음 커밋에서 원본을 제거한다.
static_assert(PatternTable[(int32)EBulletPattern::LissajousStorm].PhaseSec == LissaPhaseSec, "");
static_assert(PatternTable[(int32)EBulletPattern::LissajousStorm].Arc.Count == LissaCount,   "");
// … 옮긴 값 전부에 대해
```

### 제안과 달라진 것 — 제안 테이블의 값 두 개가 실물과 달랐다

그대로 베꼈으면 게임플레이 회귀였다.

| 제안 | 실물 | 왜 |
|---|---|---|
| `FBossLook::Emis` 기본값 `(1, 0.1, 0.05, 1)` | **`(1, 0, 0, 1)`** | 팩 기본 MI 값. Spiral 의 외관이 바뀔 뻔했다 |
| `Homing` 행 = `PhaseSec 0` / 이름 `"Homing"` | **`SpiralPhaseSec`(5) / `"Spiral"`** | 현행 `BeginPhase` switch 에 `Homing` case 가 없어 `default`(=Spiral)로 떨어진다 |

`Homing` 은 `bInRotation = false` 이고 `re.Debug.BossPattern` 이 풀 인덱스로 클램프해
**도달 불가**다. 그래도 동작 불변이 게이트이므로 현행 값을 그대로 두고 주석으로 이유를 남겼다.

이행 안전장치는 제안대로 했다 — 원본 `constexpr` 를 안 지운 채 `static_assert` **90여 개**로
테이블 값이 원본과 같음을 증명하고(`REBossCharacter.h` 말미, 클래스 안이라 private 상수에
접근된다), **다음 커밋**에서 원본과 검증 블록을 함께 제거했다. 그 커밋의 빌드 통과가 곧
값 이관이 무결하다는 증명이다.

한 가지 구조가 제안과 다르다: 테이블을 `.cpp` 의 `extern const` 가 아니라 헤더의
**`inline constexpr`** 로 뒀다. `extern const` 배열은 상수식이 아니라
`static_assert(PatternTable[i].PhaseSec == ...)` 자체가 성립하지 않는다.

### 규모 (실측)

| | Before | After |
|---|---:|---:|
| 패턴 지식이 사는 곳 | 7곳 | **1곳** |
| 패턴 추가 시 수정 지점 | 7 | **1** |
| 누락 시 결과 | 조용한 오동작 | **컴파일 실패** |
| `REBossCharacter.h` | 719줄 | **516줄** |

### 게이트 — 전부 통과

- [x] **리팩터 전에** 15종 각각 `re.Debug.BossPattern <N>` 고정 실행 → 로그 캡처 (기준선)
- [x] 리팩터 후 같은 실행 → 볼리당 스폰 수·페이즈 길이·패턴 이름 **15/15 동일**
- [x] `Pool.Num() == 15` (테이블 16행 중 `bInRotation` true 15개)
- [x] 이행용 `static_assert` 전부 통과 후 제거

> **게이트 도구 정정:** 게이트 6 서명에서 `Shape=` 열을 뺐다. `CurrentArtilleryShape` 는
> 페이즈 패턴이 `Artillery`/`ArtilleryStorm` 일 때만 대입되고, 나머지 곡사 6종은 **직전
> 페이즈가 남긴 값을 그대로 페이로드에 싣는다**(`REBossCharacter.cpp:304,310`).
> 첫 페이즈가 랜덤이라 그 잔류값이 실행마다 달라진다. 그 값은 `Pattern == Artillery`
> 분기에서만 읽히므로 다른 패턴에서는 로그 표기일 뿐이고, Shape 6종의 결정론 비교는
> 게이트 7 이 고정 입력으로 전량 덮는다.

---

<a name="r-05"></a>
## R-05 — `Multicast_FireArtillery_Implementation` 분해

### Before — `Core/REBossCharacter.cpp:608~867`, 한 함수 259줄

```cpp
void AREBossCharacter::Multicast_FireArtillery_Implementation(EBulletPattern Pattern, EArtilleryShape Shape,
                                                              FVector_NetQuantize Origin, FVector_NetQuantize AimLoc,
                                                              int32 CallSeed, int32 SweepIdx, float ServerTime)
{
    UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
    if (!Spawner) { /* 로그 후 반환 */ }

    // ── 7개 병렬 bool ────────────────────────────────────────────
    const bool bStorm  = (Pattern == EBulletPattern::ArtilleryStorm);
    const bool bLissa  = (Pattern == EBulletPattern::LissajousStorm);
    const bool bVortex = (Pattern == EBulletPattern::BezierVortex);
    const bool bRoseF  = (Pattern == EBulletPattern::RoseField);
    const bool bDome   = (Pattern == EBulletPattern::AerialDome);
    const bool bSpiro  = (Pattern == EBulletPattern::Spirograph);
    const bool bMicro  = (Pattern == EBulletPattern::MicroMissile);
    const bool bShaped = bRoseF || bDome || bSpiro;

    // ── 분기 1회차: 파라미터 (7단 if-else, 30줄) ──────────────────
    float FlightTime = ArtilleryFlightTime;
    float MaxHeight  = ArtilleryMaxHeight;
    int32 Count      = ArtilleryCount;
    if (bStorm)       { FlightTime = StormFlightTime;  MaxHeight = StormMaxHeight;  Count = StormCount; }
    else if (bLissa)  { FlightTime = LissaFlightTime;  MaxHeight = LissaMaxHeight;  Count = LissaCount; }
    else if (bVortex) { /* … */ }
    else if (bRoseF)  { /* … */ }
    else if (bDome)   { /* … */ }
    else if (bSpiro)  { /* … */ }
    else if (bMicro)  { /* … */ }

    const float Elapsed = GetElapsedSince(ServerTime);
    if (Elapsed >= FlightTime) { /* 로그 후 반환 */ }
    StartPatternLook(Pattern);
    FRandomStream CallRng(CallSeed);

    // ── 분기 2회차: 착지점 생성 (7단 if-else + Shape switch, 75줄) ─
    TArray<FVector> Targets;
    if (bStorm)       { Targets = REBulletPattern::GenSweepSpiral(/*…*/); }
    else if (bLissa)  { Targets = REBulletPattern::GenLissajous(/*…*/); }
    // … 5단 더
    else
    {
        switch (Shape)   // ← default 없음
        {
        case EArtilleryShape::Ring:        Targets = /*…*/; break;
        case EArtilleryShape::Line:        Targets = /*…*/; break;
        case EArtilleryShape::Grid:        Targets = /*…*/; break;
        case EArtilleryShape::Spiral:      Targets = /*…*/; break;
        case EArtilleryShape::PlayerAimed: Targets = /*…*/; break;
        case EArtilleryShape::Random:      Targets = /*…*/; break;
        }                                  // ← Shape 추가 시 Targets 가 빈 채로 진행
    }

    // ── 분기 3회차: 샷 성형 (같은 bool 을 또 본다, 100줄) ──────────
    TArray<REBulletPattern::FArcBulletSpawnParams> Shots;
    for (int32 i = 0; i < Targets.Num(); ++i)
    {
        /* … P.Damage = bMicro ? … ; P.Radius = bLissa ? … ; if (bMicro) …; if (bShaped) …;
             if (bVortex) …; if (bStorm) …;  각각 continue 로 스킵 가능 … */
        Shots.Add(P);
    }
    Spawner->SpawnArcBulletBatch(Shots);
    UE_LOG(/* … */);
}
```

**문제:** 같은 7개 조건을 세 번 다시 분기한다. 한 곳만 고치면 어긋나고,
이 함수는 **서버와 클라가 같이 실행하는 결정론 경로**다 — 어긋나면 탄막이 갈린다.
그리고 `EArtilleryShape` switch 에 `default:` 가 없어, Shape 를 늘리면
`Targets` 가 빈 채로 `SpawnArcBulletBatch(0)` 이 호출되고 **아무 일도 안 한 것과 구별되지 않는다.**

### After — 3단 분리 (R-04 테이블 의존)

```cpp
void AREBossCharacter::Multicast_FireArtillery_Implementation(EBulletPattern Pattern, EArtilleryShape Shape,
                                                              FVector_NetQuantize Origin, FVector_NetQuantize AimLoc,
                                                              int32 CallSeed, int32 SweepIdx, float ServerTime)
{
    UWorld* World = GetWorld();
    UREBulletSpawnSubsystem* Spawner = World ? World->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
    if (!Spawner)
    {
        UE_LOG(LogRENet, Warning, TEXT("[RE] Boss::FireArtillery: Spawner NULL"));
        return;
    }

    // 파라미터는 테이블이 쥔다 — 7단 if-else 가 사라진다 (R-04).
    const REBoss::FArcDef& Arc = REBoss::GetPatternDef(Pattern).Arc;

    // 지연 보정 — 이미 착지한 볼리는 착지점 생성·난수 소비 **전에** 버린다.
    // 순서를 바꾸면 CallRng 소비 횟수가 달라져 서버/클라가 갈린다.
    const float Elapsed = GetElapsedSince(ServerTime);
    if (Elapsed >= Arc.FlightTime)
    {
        UE_LOG(LogRENet, Log, TEXT("[RE] Boss FireArtillery: skipped (Elapsed=%.3f >= FlightTime=%.2f)"),
            Elapsed, Arc.FlightTime);
        return;
    }

    StartPatternLook(Pattern);   // 페이즈 도중 접속한 클라 따라잡기

    FRandomStream CallRng(CallSeed);   // 서버 시드 → 양쪽 동일 난수열
    const FVector GroundZ2 = FVector(0, 0, Origin.Z + MarkerGroundOffset);

    // 1) 착지 지오메트리
    TArray<FVector> Targets;
    if (!BuildArcTargets(Pattern, Shape, Origin, AimLoc, SweepIdx, ServerTime, Arc.Count, CallRng, Targets))
    {
        return;   // 실패는 BuildArcTargets 가 로그한다
    }

    // 2) 궤적 성형 + 어긋내기
    TArray<REBulletPattern::FArcBulletSpawnParams> Shots;
    ShapeArcShots(Pattern, Origin, Targets, Arc, Elapsed, SweepIdx, Shots);

    // 3) 스폰
    Spawner->SpawnArcBulletBatch(Shots);

    UE_LOG(LogRENet, Log,
        TEXT("[RE] Boss FireArtillery: Pattern=%d Shape=%d N=%d Flight=%.2f Sweep=%d Elapsed=%.3f role=%s"),
        (int32)Pattern, (int32)Shape, Shots.Num(), Arc.FlightTime, SweepIdx, Elapsed,
        *UEnum::GetValueAsString(GetLocalRole()));
}
```

**착지점 생성 — Shape 누락 방어 포함**

```cpp
/**
 *  착지점 생성. 실패 시 false + 로그.
 *
 *  결정론 계약: CallRng 를 소비하는 경로(GenRandom)의 **호출 위치와 횟수를 바꾸면 안 된다.**
 *  서버와 클라가 같은 시드로 같은 순서로 뽑아야 착지점이 일치한다.
 */
bool AREBossCharacter::BuildArcTargets(EBulletPattern Pattern, EArtilleryShape Shape,
                                       const FVector& BossLoc, const FVector& PlayerLoc,
                                       int32 SweepIdx, float ServerTime, int32 Count,
                                       FRandomStream& CallRng, TArray<FVector>& Out) const
{
    const float GroundZ = BossLoc.Z + MarkerGroundOffset;

    switch (Pattern)
    {
    case EBulletPattern::ArtilleryStorm:
    {
        const float Denom = (float)FMath::Max(StormShotsPerSweep - 1, 1);
        const float T0 = FMath::Clamp((SweepIdx * Count)             / Denom, 0.f, 1.f);
        const float T1 = FMath::Clamp((SweepIdx * Count + Count - 1) / Denom, 0.f, 1.f);
        Out = REBulletPattern::GenSweepSpiral(BossLoc, StormMinRadius, StormMaxRadius,
                                              StormSweepTurns, T0, T1, Count, StormArms, GroundZ);
        break;
    }
    case EBulletPattern::LissajousStorm:
        Out = REBulletPattern::GenLissajous(BossLoc, LissaExtent, LissaExtent, LissaFreqX, LissaFreqY,
                                            LissaDeltaDegPerSec * ServerTime, Count, GroundZ);
        break;
    // … BezierVortex / RoseField / AerialDome / Spirograph / MicroMissile
    //    (본문은 현행과 **한 줄도 다르지 않게** 옮긴다)

    case EBulletPattern::Artillery:
        return BuildArtilleryShapeTargets(Shape, BossLoc, PlayerLoc, Count, GroundZ, CallRng, Out);

    default:
        // 곡사가 아닌 패턴이 이 경로에 오면 라우팅이 깨진 것이다 (R-04 테이블 Family 불일치).
        UE_LOG(LogRENet, Error, TEXT("[RE] BuildArcTargets: 곡사 아닌 패턴 %d 가 곡사 경로에 진입"),
            (int32)Pattern);
        return false;
    }
    return true;
}

bool AREBossCharacter::BuildArtilleryShapeTargets(EArtilleryShape Shape, const FVector& BossLoc,
                                                  const FVector& PlayerLoc, int32 Count, float GroundZ,
                                                  FRandomStream& CallRng, TArray<FVector>& Out) const
{
    switch (Shape)
    {
    case EArtilleryShape::Ring:
        Out = REBulletPattern::GenRing(BossLoc, ArenaRadius * 0.75f, Count, GroundZ);            break;
    case EArtilleryShape::Line:
        Out = REBulletPattern::GenLine(BossLoc, PlayerLoc, ArenaRadius * 2.f, Count, GroundZ);   break;
    case EArtilleryShape::Grid:
        Out = REBulletPattern::GenGrid(BossLoc, ArenaRadius, ArenaRadius, 4, 3, GroundZ);        break;
    case EArtilleryShape::Spiral:
        Out = REBulletPattern::GenArcSpiral(BossLoc, ArenaRadius, Count, GroundZ);               break;
    case EArtilleryShape::PlayerAimed:
        Out = REBulletPattern::GenPlayerCluster(PlayerLoc, 150.f, 4, GroundZ);                   break;
    case EArtilleryShape::Random:
        Out = REBulletPattern::GenRandom(BossLoc, ArenaRadius, Count, CallRng, GroundZ);         break;

    default:
        // Before 에는 이 경로가 없었다. Shape 를 늘리면 Targets 가 빈 채로 진행해
        // "발사했는데 아무것도 안 떨어지는" 조용한 실패가 됐다.
        // 동작은 그대로다(빈 배열 → 스폰 0). 관측 가능하게 만드는 것이 전부다.
        ensureMsgf(false, TEXT("[RE] EArtilleryShape %d 가 BuildArtilleryShapeTargets 에 없다"), (int32)Shape);
        UE_LOG(LogRENet, Error, TEXT("[RE] BuildArtilleryShapeTargets: 미처리 Shape %d"), (int32)Shape);
        return false;
    }
    return true;
}
```

> **결정론 주의:** `Random` 케이스만 `CallRng` 를 소비한다. `default` 를 추가해도
> 난수 소비 순서는 변하지 않는다 — 그래서 이 방어는 안전하다.

**샷 성형** — `ShapeArcShots` 는 현행 for 루프를 통째로 옮긴다. 바뀌는 것:

```cpp
// Before: 7개 bool 을 함수 상단에서 만들어 루프 안에서 재사용
P.Damage = bMicro ? MicroDamage : ArtilleryDamage;
P.Radius = bLissa ? LissaRadius : (bMicro ? MicroRadius : ArtilleryRadius);

// After: 루프 밖에서 패턴별로 한 번만 결정 (조건 재평가 제거)
const float ShotDamage = (Pattern == EBulletPattern::MicroMissile) ? MicroDamage : ArtilleryDamage;
const float ShotRadius = (Pattern == EBulletPattern::LissajousStorm) ? LissaRadius
                       : (Pattern == EBulletPattern::MicroMissile)   ? MicroRadius
                       :                                               ArtilleryRadius;
```

> **인덱스 계약 유지:** `Shots` 는 `Targets` 보다 짧을 수 있다(`continue` 스킵).
> 루프 인덱스 `i` 는 끝까지 **`Targets` 기준**이며 `Shots.Num()` 이 아니다.
> `MicroMissile` 의 접선 각과 `Spirograph` 의 부호 뒤집기(`i & 1`)가 이 인덱스에 의존한다.

### 규모

| | Before | After |
|---|---:|---:|
| 최대 함수 길이 | **259줄** | **50줄** (+ 헬퍼 3개: 80 / 36 / 115) |
| 병렬 bool | 7 (함수 전체) | 0 (성형 단계 안 5개는 그 단계 지역 판별) |
| 같은 조건 재분기 | 3회 | 1회 |
| Shape 누락 시 | 조용한 무발사 | `ensureMsgf` + Error 로그 |

### 게이트 — 전부 통과

- [x] **리팩터 전에** 곡사 8종 × 전 Shape × 스윕 3회 = **1,767 샷** 고정 입력 캡처 (기준선)
- [x] 리팩터 후 같은 실행 → **1,767줄 바이트 단위 동일**
- [x] `dedi-verify.ps1 -Clients 2` PASS — 서버/클라 착지점 일치
- [x] 게이트 6: 패턴 15종 스폰 수·페이즈 길이 동일

> **기준선을 어떻게 잡았나 — 라이브 로그로는 판정할 수 없었다.**
> 곡사 8종 중 5종이 착지점을 `ServerTime` 에서 유도한다(Lissa / Vortex / RoseField /
> Dome / Spiro). `Origin`·`AimLoc` 도 라이브 위치다. 즉 **리팩터를 안 해도 실행마다
> 좌표가 달라진다** — 고정 `CallSeed` 만으로는 "바이트 동일"이 성립하지 않는다.
>
> STEP 0 에서 입력을 전부 고정한 임시 덤프(`re.Debug.DumpArcTargets`)를 넣었다.
> `ServerTime` 을 현재 시각보다 크게 잡으면 `GetElapsedSince` 의 `[0,1]` 클램프가
> `Elapsed` 를 항상 0 으로 만들어 어긋내기까지 결정론이 된다. 비교 대상은 착지점이
> 아니라 **함수의 실제 출력인 `Shots` 전량**이라, 분해가 파라미터 결정·착지 지오메트리·
> 샷 성형 어느 단계를 어긋내도 잡힌다. 2회 실행 diff 0 으로 결정론을 먼저 확인했다.
> R-04 / R-05 / R-07 / R-08 네 항목의 게이트 7 을 이 코드로 판정했고, 마지막 커밋에서 되돌렸다.
> 재현 절차는 [`baseline/README.md`](baseline/README.md).

---

<a name="r-06"></a>
## R-06 — Mass 배치 스폰

### Before — `Mass/REBulletSpawnSubsystem.cpp:12, 29~57`

```cpp
FMassEntityManager* UREBulletSpawnSubsystem::GetEntityManager() const
{
    UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    return Mass ? &Mass->GetMutableEntityManager() : nullptr;
}

FMassEntityHandle UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity,
                                                       float Lifetime, float ColorSel)
{
    FMassEntityManager* EM = GetEntityManager();          // ← 발당 1회: GetWorld + GetSubsystem
    if (!EM)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RE] SpawnBullet: EntityManager NULL"));
        return FMassEntityHandle();
    }

    EnsureArchetype(*EM);                                  // ← 발당 1회
    FMassEntityHandle Entity = EM->CreateEntity(BulletArchetype);   // ← 엔티티 1개씩

    EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Location);
    FBulletSimFragment& Sim = EM->GetFragmentDataChecked<FBulletSimFragment>(Entity);
    Sim.Velocity = Velocity;
    Sim.Lifetime = Lifetime;
    Sim.ColorSel = ColorSel;
    return Entity;
}

void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
    for (const FBulletSpawnParams& P : Params)
    {
        SpawnBullet(P.Location, P.Velocity, P.Lifetime, P.ColorSel);   // ← N회 전부 반복
    }
}
```

`SpawnArcBulletBatch` 도 같은 구조다 (`:105~113`).

**비용:** 볼리 N발마다 `GetWorld()` N회, `GetSubsystem` N회, `CreateEntity` N회.
현재 게임플레이 발수는 `BulletsPerShot = 48`(ini), 곡사 폭풍은 `StormCount` 단위 볼리다.
이 프로젝트의 주장이 성능인데 **스폰 경로가 배치 API 를 안 쓰고 있다.**

### After

```cpp
void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
    if (Params.IsEmpty())
    {
        return;   // BatchCreateEntities(0) 호출 금지
    }

    FMassEntityManager* EM = GetEntityManager();   // ← 볼리당 1회
    if (!EM)
    {
        UE_LOG(LogREBullet, Warning, TEXT("[RE] SpawnBulletBatch: EntityManager NULL (N=%d)"), Params.Num());
        return;
    }
    EnsureArchetype(*EM);                          // ← 볼리당 1회

    TArray<FMassEntityHandle> Entities;
    Entities.Reserve(Params.Num());
    EM->BatchCreateEntities(BulletArchetype, Params.Num(), Entities);

    for (int32 i = 0; i < Entities.Num(); ++i)
    {
        const FBulletSpawnParams& P = Params[i];
        EM->GetFragmentDataChecked<FTransformFragment>(Entities[i])
          .GetMutableTransform().SetLocation(P.Location);

        FBulletSimFragment& Sim = EM->GetFragmentDataChecked<FBulletSimFragment>(Entities[i]);
        Sim.Velocity = P.Velocity;
        Sim.Lifetime = P.Lifetime;
        Sim.ColorSel = P.ColorSel;
        // FBulletRenderFragment.InstanceIndex 는 기본값 INDEX_NONE 유지 (#17 에서 할당)
    }
}
```

> **⚠ 구현 세션 확인 사항 (미검증):**
> `FMassEntityManager::BatchCreateEntities` 의 **정확한 시그니처를 엔진 헤더에서 확인할 것.**
> UE 5.x 계열에서 `FMassArchetypeSharedFragmentValues` 인자 유무와
> `TSharedRef<FEntityCreationContext>` 반환 여부가 버전에 따라 갈린다.
> 확인 경로: `<Engine>/Plugins/Runtime/MassEntity/Source/MassEntity/Public/MassEntityManager.h`
> 이 프로젝트의 엔진은 **UE 5.8.1 소스 빌드**다.
> 배치 API 가 기대와 다르면 **최소 목표는 `GetEntityManager()` + `EnsureArchetype()` 를
> 루프 밖으로 빼는 것**이고, 그것만으로도 N회 → 1회가 된다.

**호출자 계약 확인 (선행 작업):**

```bash
grep -rn "SpawnBullet(\|SpawnArcBullet(" Source/Project_RE --include=*.cpp | grep -v "REBulletSpawnSubsystem.cpp"
```

개별 `SpawnBullet` 의 **반환 핸들을 쓰는 호출자가 있는지** 먼저 확인한다.
없으면 개별 함수는 배치의 얇은 래퍼로 남기고, 있으면 배치 경로가 핸들 배열을 돌려준다.

### 게이트 — 측정이 판정했고, **기각했다**

전문은 [`baseline/perf-r06.md`](baseline/perf-r06.md). 요약:

**GT mean (ms), 같은 세션에서 원본/A/B 를 번갈아 측정**

| 구성 | 원본 | A 배치 | B 조회 호이스트 |
|---|--:|--:|--:|
| 클로즈드루프 `re.Bullets.Count 5000` | **4.52 / 4.58** | 4.86 / 4.98 | 4.80 / 4.70 |
| 오픈루프 `BossPattern 0` (Spiral) | **4.48** | 4.69 | 4.56 |
| 오픈루프 `BossPattern 6` (3회 중앙값) | **4.90** | 4.99 | — |

세 구성 전부, 두 안 전부 원본보다 느리다. 부호가 일관돼 노이즈가 아니다.
열 드리프트도 아니다 — B 의 클로즈드루프는 빌드 직후 **가장 차가운 상태의 첫 두 런**이었고
원본은 앞서 네 런을 돈 뒤였는데도 원본이 빨랐다.

- [x] GT mean / p99 가 나빠지지 않을 것 → **실패**
- [x] 나빠지면 되돌린다 → **되돌렸다.** 소스는 원본 그대로다

**A 가 느린 이유:** 배치 *생성*만 하고 프래그먼트 기입은 여전히 엔티티당
`GetFragmentDataChecked` 다. 배치 API 의 고정 비용(생성 컨텍스트 `TSharedRef` 힙 할당,
옵저버 통지 기구, 핸들 `TArray`)만 얻고 정작 이득인 **연속 청크 기입은 못 얻었다.**
볼리가 48발이라 그 고정 비용이 절약분을 넘는다.

**근본 이유 — 스폰은 병목이 아니었다.** 오픈루프 게임플레이 스폰율은 48/0.15 = **320발/s**
인데 프레임마다 도는 것은 체공 **약 4,800발**의 Sim / Render / Hit 이다. 스폰은 프레임
작업량의 1% 미만이라 그 경로를 몇 배 빠르게 해도 GT 에 안 보인다.

> **측정 방법 교훈 — 이게 이 항목의 진짜 산출물이다.**
> 처음엔 STEP 0 기준선(2시간 전)과 비교해 p0 GT p99 9.97→6.88(−3.09),
> p3 11.78→9.31(−2.47) 로 "뚜렷한 개선"이 나왔다. **전부 거짓이었다.** 같은 세션에서
> R-06 만 빼고 다시 재니 원본이 4.48 / 6.47 이었다 — 두 시점 사이에 R-01~R-07 과 머신
> 상태가 같이 바뀌어 있었고 그 차이가 R-06 의 성과로 보였을 뿐이다.
> **두 시점 사이에 다른 변경이 끼어 있으면 그 비교는 그 항목의 측정이 아니다.**

---

<a name="r-07"></a>
## R-07 — 히트 반경 결합 해소 + 탄 데미지 이관

### Before — 3파일이 손으로 동기화돼 있다

```cpp
// Mass/REBulletRenderProcessor.cpp:19~27
/**
 *  탄환 인스턴스 스케일 — 엔진 Sphere(지름 100cm)를 지름 50cm로.
 *  …
 *  바꾸면 REBulletHitProcessor 의 HitRadius 와 Baseline/REBulletActor 의
 *  ActorBulletScale 도 같이 맞춰야 한다.
 */
constexpr float BulletScale = 0.5f;
```

```cpp
// Mass/REBulletHitProcessor.cpp:20~26
/**
 *  히트 반경(cm) — 탄환 시각 반경 25(BulletScale 0.5 × Sphere 반경 50) + 플레이어 캡슐 반경 ~35.
 *  BulletScale 을 바꾸면 여기도 같이 바꿔야 한다. 안 그러면 눈에 안 닿았는데 맞거나
 *  닿았는데 안 맞아 공정성이 깨진다 (#97).
 */
constexpr float HitRadius = 60.f;
/** 탄환 1발 데미지 — 100 HP 기준 10발 사망. */
constexpr float BulletDamage = 10.f;
```

```cpp
// Baseline/REBulletActor.cpp:10~11
/** Mass와 동일 — REBulletRenderProcessor.cpp:14 BulletScale. */
constexpr float ActorBulletScale = 0.5f;
```

### 이미 어긋나기 시작했다

`REBulletActor.cpp:10` 은 `BulletScale` 이 `REBulletRenderProcessor.cpp:14` 에 있다고 말한다.
**실제로는 27번 줄이다.** 값은 아직 맞지만 참조는 이미 썩었다 —
"주석으로 관리하는 결합"이 어떻게 붕괴하는지의 실물 증거다.

### After — 유도식 하나로 묶는다

**신설** `Source/Project_RE/Mass/REBulletGeometry.h`

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 *  탄환 지오메트리 단일 출처.
 *
 *  전에는 BulletScale(렌더) / HitRadius(판정) / ActorBulletScale(Actor 비교군)이
 *  세 파일에 흩어져 **주석으로만** 묶여 있었다. 주석은 컴파일러가 검사하지 않는다 —
 *  실제로 REBulletActor.cpp 의 참조 줄번호가 이미 썩어 있었다.
 *
 *  판정 반경이 시각 반경과 어긋나면 "눈에 안 닿았는데 맞거나, 닿았는데 안 맞는" 상태가 된다.
 *  회피 게임에서 이건 공정성 버그다 (#97). 그래서 유도식으로 묶는다.
 *
 *  **네임스페이스를 쓰는 이유:** 익명 네임스페이스로 두면 유니티 빌드에서
 *  동명 변수가 C4459 로 충돌한다 — 이 프로젝트에 전례가 있다(REBulletActor.cpp:11 주석).
 */
namespace REBulletGeometry
{
    /** /Engine/BasicShapes/Sphere 원본 반경(cm). 지름 100cm 구. */
    inline constexpr float EngineSphereRadius = 50.f;

    /**
     *  탄환 인스턴스 스케일. 지름 50cm.
     *  탄이 서로 겹친다: 간격 = BulletSpeed(200) × BossFireInterval(0.15) = 30uu < 지름 50uu.
     *  겹침은 의도적 허용 — 축소하면(0.2 시도) 탄이 너무 작아 탄막의 인상이 사라진다.
     *  대신 인접 탄을 다른 색으로 교차시켜 가른다 (#97).
     */
    inline constexpr float BulletScale = 0.5f;

    /** 탄환 시각 반경(cm) — 유도값. 25. */
    inline constexpr float BulletVisualRadius = EngineSphereRadius * BulletScale;

    /** 플레이어 캡슐 반경(cm). ARECharacterBase 의 CapsuleComponent 설정과 일치해야 한다. */
    inline constexpr float PlayerCapsuleRadius = 35.f;

    /**
     *  탄환 히트 반경(cm) — 유도값. 60.
     *  BulletScale 을 바꾸면 여기가 **자동으로** 따라간다. 이게 이 헤더의 목적 전부다.
     */
    inline constexpr float HitRadius = BulletVisualRadius + PlayerCapsuleRadius;
}
```

**치환** — `Mass/REBulletHitProcessor.cpp`

```cpp
#include "REBulletGeometry.h"

// 이행 검증(한 커밋 동안만): 현행 값과 동일함을 컴파일 타임에 증명한다.
// 판정 반경이 1이라도 바뀌면 게임플레이 회귀다.
static_assert(REBulletGeometry::HitRadius == 60.f, "히트 반경이 바뀌었다 — 게임플레이 회귀");

// … 사용처
if (FVector::DistSquaredXY(BulletLoc, T.Location)
    <= REBulletGeometry::HitRadius * REBulletGeometry::HitRadius)
```

`Mass/REBulletRenderProcessor.cpp` 와 `Baseline/REBulletActor.cpp` 의 로컬 상수는 삭제하고
`REBulletGeometry::BulletScale` 을 쓴다. 유니티 빌드 C4459 가 **원인부터 사라진다.**

### 곁들여 — 탄 데미지 Settings 이관

**Before** — `Mass/REBulletHitProcessor.cpp:26`

```cpp
/** 탄환 1발 데미지 — 100 HP 기준 10발 사망. */
constexpr float BulletDamage = 10.f;
```

플레이어 HP·공격 데미지·보스 HP 는 전부 `UREStatsSettings`(ini)인데 **탄 데미지만 cpp 상수**다.
밸런스 축 하나가 리빌드를 요구한다.

**After** — `Core/REStatsSettings.h`

```cpp
    /** 보스 탄환 1발이 주는 데미지. PlayerMaxHealth 100 기준 10발 사망. */
    UPROPERTY(EditAnywhere, Config, Category = "Boss")
    float BulletDamage = 10.f;
```

`Config/DefaultGame.ini`

```ini
BulletDamage=10.0
```

사용처

```cpp
// Execute() 진입부에서 1회 조회 — 엔티티 루프 안에서 GetDefault 를 부르지 않는다
const float BulletDamage = GetDefault<UREStatsSettings>()->BulletDamage;
```

**주석 드리프트도 같이 고친다** — `Core/REStatsSettings.h:60`

```cpp
// Before: "동시 탄수는 발수 × 수명/주기로 자연 결정 (기본 16 × 15/0.1 = 2400)"
// 실제 ini: BulletsPerShot=48, BulletLifetime=15.0, BossFireInterval=0.15
// After:    "(ini 기본 48 × 15/0.15 = 4,800)"
```

### 제안과 달라진 것 — `PlayerCapsuleRadius` 는 35 가 아니라 34다

계획이 미리 경고한 그 경우가 실제로 나왔다. `ARECharacterBase` 는 캡슐 크기를 지정하지
않아 `ACharacter` 기본값을 그대로 쓰고, 그 값은 `Engine/Private/Character.cpp:78` 의
`InitCapsuleSize(34.0f, 88.0f)` 다.

**값을 34로 내리면 `HitRadius` 가 59 가 되어 판정이 좁아진다 — 게임플레이 변경이다.**
계획 규칙대로 값은 그대로 두고 **이름을 실제에 맞췄다**:

```cpp
/**
 *  플레이어 쪽 판정 여유(cm).
 *  ARECharacterBase 는 캡슐 크기를 지정하지 않아 ACharacter 기본값을 그대로 쓴다 —
 *  반경 34 / 반높이 88 (Engine/Private/Character.cpp: InitCapsuleSize(34.f, 88.f)).
 *  이 값은 그 34 를 35 로 올림한 것이다. **34 로 내리지 마라** — HitRadius 가 59 가 되어
 *  판정이 1uu 좁아지고, 그건 게임플레이 변경이다. 캡슐을 실제로 바꾸는 날 같이 바꾼다.
 */
inline constexpr float PlayerCapsuleAllowance = 35.f;
```

`ArcBulletScale`(0.7f, `REArcRenderProcessor`)은 범위 밖으로 남겼다 — 곡사탄 전용 축척이라
직선탄 지오메트리와 묶이지 않고, 계획의 3파일 결합에도 없다.

이행용 `static_assert` 두 개는 다음 커밋에서 제거했다. **남겨 두면 안 되는 이유:**
`static_assert(HitRadius == 60.f)` 는 `BulletScale` 을 바꾸는 것 자체를 막는다. 이 헤더의
목적이 "BulletScale 을 바꾸면 HitRadius 가 따라간다" 인데 그 변경을 컴파일 에러로 만들면
목적과 정반대가 된다.

### 게이트 — 전부 통과

- [x] `static_assert(REBulletGeometry::HitRadius == 60.f)` 통과 (이행 커밋에서), 이후 제거
- [x] **풀 유니티 빌드** 통과 — 트리를 clean 한 뒤 전 소스를 touch 해 adaptive non-unity
      제외를 0건으로 만들고 빌드 → `Module.Project_RE.cpp` 단일 blob 컴파일, **C4459 0건**.
      익명 네임스페이스 동명 상수라는 원인 자체가 사라졌다
- [x] 헤드리스 피격 게이트 PASS — `[RE] BulletHit: Applied=10` (ini 조회 경로로도 데미지 불변)
- [x] `dedi-verify.ps1 -Clients 2` PASS

---

<a name="r-08"></a>
## R-08 — 헤드리스 프로브 분리

### Before

```
Core/REPlayerController.cpp   714줄
  RunHeadlessMoveProbe()      579~618
  RunHeadlessFireProbe()      619~657
  RunHeadlessDashProbe()      658~714
                              ─────── 135줄 (19%)

Core/REPlayerController.h
  FTimerHandle ProbeFireTimer / ProbeDashTimer / ProbeMoveTimer / ProbeLogTimer
  FVector      ProbeDashStart / ProbeTarget
  void         RunHeadlessMoveProbe() / RunHeadlessFireProbe() / RunHeadlessDashProbe()
```

게이트는 런타임 하나뿐이다 — `Core/REPlayerController.cpp:176`

```cpp
// 헤드리스(-unattended) 서버권위 이동 프로브. 실플레이(PIE/에디터)엔 무발동.
if (HasAuthority() && FApp::IsUnattended())
{
    RunHeadlessMoveProbe();
    RunHeadlessFireProbe();
    RunHeadlessDashProbe();
}
```

**문제:** `FApp::IsUnattended()` 는 **런타임** 검사다. 프로브 코드는 쉬핑 빌드에 그대로 들어가고,
`-unattended` 는 쉬핑에서도 커맨드라인으로 넘길 수 있다. 즉 **출시 빌드에서 프로브가 켜진다.**
피해 자체는 크지 않지만(프로브가 자기 폰을 움직이는 정도), 리뷰어가 즉시 지적할 지점이다.

### After

**신설** `Source/Project_RE/Debug/REHeadlessProbeComponent.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "REHeadlessProbeComponent.generated.h"

/**
 *  헤드리스 검증 프로브 (#77 / #82 / #112).
 *
 *  PIE 없이 `-game -nullrhi -unattended` 로 게임루프를 관측한다.
 *  이동 → 발사 → 대쉬를 순차 실행하고 각 단계 결과를 로그로 남기면,
 *  scripts/dedi-verify.ps1 이 그 로그를 판정해 PASS/FAIL 을 낸다.
 *
 *  **로그 문자열이 곧 판정 계약이다.** 메시지를 바꾸면 스크립트가 조용히 무판정이 된다.
 *
 *  프로덕션 컨트롤러에서 분리한 이유: 이건 테스트 하네스이고,
 *  전에는 런타임 게이트(FApp::IsUnattended)만 있어 쉬핑 빌드에도 실려 있었다.
 */
UCLASS(ClassGroup = (RE), meta = (BlueprintSpawnableComponent))
class UREHeadlessProbeComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 프로브 3종 순차 시작. 소유 액터는 AREPlayerController 여야 한다. */
    void StartProbes();

private:
#if !UE_BUILD_SHIPPING
    void RunMoveProbe();
    void RunFireProbe();
    void RunDashProbe();

    FTimerHandle ProbeMoveTimer;
    FTimerHandle ProbeLogTimer;
    FTimerHandle ProbeFireTimer;
    FTimerHandle ProbeDashTimer;
    FVector      ProbeTarget    = FVector::ZeroVector;
    FVector      ProbeDashStart = FVector::ZeroVector;
#endif
};
```

**`Source/Project_RE/Debug/REHeadlessProbeComponent.cpp`**

```cpp
void UREHeadlessProbeComponent::StartProbes()
{
#if !UE_BUILD_SHIPPING
    RunMoveProbe();
    RunFireProbe();
    RunDashProbe();
#endif
}
```

> **UHT 주의:** `UCLASS` 자체는 항상 컴파일한다. `#if !UE_BUILD_SHIPPING` 으로
> **멤버와 본문만** 감싼다. `GENERATED_BODY()` 를 조건부 블록 안에 넣으면 UHT 가 깨진다.
> 쉬핑에서는 컴포넌트가 존재하되 `StartProbes()` 가 빈 함수가 된다.

**호출부** — `Core/REPlayerController.cpp:176`

```cpp
// 헤드리스(-unattended) 서버권위 검증 프로브. 실플레이(PIE/에디터)엔 무발동.
// 쉬핑 빌드에서는 컴포넌트 본문이 컴파일 아웃되어 이 경로가 no-op 다.
if (HasAuthority() && FApp::IsUnattended())
{
    UREHeadlessProbeComponent* Probe = NewObject<UREHeadlessProbeComponent>(this);
    Probe->RegisterComponent();
    Probe->StartProbes();
}
```

**본문은 그대로 옮긴다.** 바뀌는 것은 `this`(컨트롤러 → 컴포넌트) 참조뿐이다:

```cpp
// Before (컨트롤러 안)
APawn* P = GetPawn();
Server_RequestMove(ProbeTarget);
GetWorld()->GetTimerManager().SetTimer(ProbeMoveTimer, MoveDel, 1.0f, false);

// After (컴포넌트 안)
AREPlayerController* PC = Cast<AREPlayerController>(GetOwner());
if (!ensureMsgf(PC, TEXT("[RE] HeadlessProbe: 소유자가 AREPlayerController 가 아니다")))
{
    return;
}
APawn* P = PC->GetPawn();
PC->Server_RequestMove(ProbeTarget);
PC->GetWorldTimerManager().SetTimer(ProbeMoveTimer, MoveDel, 1.0f, false);
```

> `Server_RequestMove` / `Server_Dash` / `Server_RequestFire` 의 접근 지정자를 확인한다.
> `private` 이면 **`public` 확대가 아니라 `friend class UREHeadlessProbeComponent`** 를 쓴다 —
> 테스트 하네스 때문에 프로덕션 API 표면을 넓히지 않는다.

### 로그 문자열 — 한 글자도 바꾸지 않는다

`dedi-verify.ps1` 과 프로브 판정이 grep 하는 문자열:

```
[Move] probe start: pawn=%s target=%s
[Move] probe dist=%.1f loc=%s
[Move] probe: no pawn
```

카테고리는 R-02 에서 `LogRENet` 으로 바뀌지만 **메시지 본문은 불변**이다.

### 규모

| | Before | After |
|---|---:|---:|
| `REPlayerController.cpp` | 715줄 | **583줄** |
| `REPlayerController.h` 프로브 멤버 | 6 + 함수 3 | **0** |
| 쉬핑 빌드에 포함 | 예 | **아니오** |

### 제안대로 간 것 — `friend` 와 UHT 조건부

`Server_RequestMove` / `Server_RequestFire` 는 `protected` 였다. **public 확대가 아니라
`friend class UREHeadlessProbeComponent`** 를 썼다 — 테스트 하네스 하나 때문에 프로덕션
API 표면을 넓히면 그 뒤로는 누구나 그 RPC 를 부를 수 있게 된다.

`UCLASS` 와 `GENERATED_BODY()` 는 항상 컴파일하고 **멤버와 함수 본문만** `#if
!UE_BUILD_SHIPPING` 으로 감쌌다. 쉬핑에서는 컴포넌트가 존재하되 `StartProbes()` 가 빈 함수다.

R-03 정책도 여기 적용했다: 소유자 캐스트 실패는 부류 2(이 컴포넌트는
`AREPlayerController::BeginPlay` 에서만 생성된다) → `ensureMsgf` + 조기 반환.

### 게이트 — 전부 통과

- [x] `dedi-verify.ps1 -Clients 2` PASS (21/21) — 프로브 3종 완주 로그 전부 동일
- [x] 프로브 로그 문자열 diff: **없음** (server 15종 / client 18종 × 2)
- [ ] Shipping 구성 빌드 — 실행 중
- [x] `AREGameMode::NotifyProbeComplete` 호출 횟수·순서 동일 — `dedi-verify` 의
      "프로브 완주 (2건)" 판정이 그것이다

---

## 전체 완료 판정

| # | 게이트 | 통과 | 근거 |
|---|---|:--:|---|
| 0 | 기준선 캡처가 리팩터 시작 전에 존재 | ☑ | `baseline/` — STEP 0 커밋 `9cead85` |
| 1 | Development Editor 빌드 (신규 경고 0) | ☑ | 전 항목 커밋마다 확인 |
| 2 | **풀 유니티 빌드** | ☑ | `Module.Project_RE.cpp` 단일 blob, Adaptive 제외 0건, C4459 0건 |
| 3 | Shipping 빌드 | ⏳ | 실행 중 |
| 4 | 헤드리스 프로브 3종 완주, 로그 문자열 동일 | ☑ | server 15종 / client 18종 × 2, diff 0 |
| 5 | `dedi-verify.ps1 -Clients 2` PASS | ☑ | **21/21** |
| 6 | 패턴 15종 스폰 수·페이즈 로그 동일 | ☑ | **15/15** |
| 7 | 곡사 착지점(고정 입력) 동일 | ☑ | **1,767 샷 바이트 단위 동일** |
| 8 | `profile.ps1` GT mean/p99 나빠지지 않음 | ☑ | R-06 기각으로 달성 — 성능 변경 0 |
| 9 | `UE_LOG(LogTemp` 0건 | ☑ | **0** |
| 10 | 워커 스레드 프로세서에 `ensure` 없음 | ☑ | `REBulletSimProcessor` / `REArcSimProcessor` 0건 |

### 규모 (실측)

| | Before | After |
|---|---:|---:|
| `Source/Project_RE` 파일 | 142 | **71** |
| LOC | 14,257 | **8,481** |
| `REBossCharacter.h` | 719 | **516** |
| `REPlayerController.cpp` | 715 | **583** |
| 최대 함수(`Multicast_FireArtillery_Implementation`) | **259줄** | **50줄** |
| `UE_LOG(LogTemp` | 84 | **0** |
| `ensure`/`check` | 0 | 7 (게임 스레드 경로만) |
| 패턴 지식이 사는 곳 | 7곳 | **1곳** |

> **6·7번은 리팩터를 시작하기 전에 기준 로그를 먼저 캡처해야 한다.**
> 캡처를 빠뜨리면 회귀를 판정할 근거가 사라진다 — 이 프로젝트가 #88 에서 겪은 실패와 같은 종류다.
> 이번에는 STEP 0 에서 먼저 캡처했고, 그 덕에 R-04·R-05 의 동작 불변을 **바이트 단위로**
> 증명할 수 있었다. 반대로 R-06 은 그 기준선을 **잘못 쓸 뻔했다** — 두 시점 사이에 다른
> 항목이 끼어 있었기 때문이다. 기준선은 있는 것만으로 충분하지 않고, **무엇과 무엇 사이의
> 차이인지**가 맞아야 한다.
