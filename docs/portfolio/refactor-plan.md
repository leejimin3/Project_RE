# 포트폴리오용 리팩터링 계획 — 무엇을 왜

**작성일:** 2026-08-29 · **구현 완료:** 2026-08-30 · **브랜치:** `feature/M8-portfolio-hardening` (#141)
**코드 전후 비교 + 게이트 결과:** [`refactor-before-after.md`](refactor-before-after.md)
**소재 문서:** [`portfolio-source.md`](portfolio-source.md)

> **이 문서는 계획서 원본이다.** 실행하며 계획이 실물과 어긋난 곳이 넷 나왔고,
> 정정은 `refactor-before-after.md` 의 각 항목 "제안과 달라진 것"에 적었다.
> 본문은 계획 당시 그대로 두되, 어긋난 지점에만 아래처럼 표시를 달았다.
>
> | 계획이 말한 것 | 실물 |
> |---|---|
> | `Variant_*` 32파일 | **76파일** + Content 에셋 515개 |
> | `dedi-verify.ps1` 이 `-LogCmds="LogTemp Verbose"` 하드코딩 | **그런 줄이 없다** |
> | `PlayerCapsuleRadius = 35.f` 가 캡슐 반경 | 실제 캡슐 반경은 **34**(ACharacter 기본) |
> | R-06 이 성능을 개선할 것 | **기각** — 세 구성 전부 느려졌다 |

---

## 0. 이 리팩터링의 목적과 비목적

**목적**
포트폴리오 리뷰어(대체로 리드/시니어)가 **코드를 열었을 때** 소재 문서가 주장하는 수준과
실제 코드의 수준이 어긋나지 않게 만든다. 지금 어긋나는 지점이 실재한다 — 아래 8건이 그것이다.

**비목적**
- 새 기능 추가 (0건)
- 게임플레이 동작 변경 (0건 — R-06 을 제외한 전부가 **동작 불변**이 게이트다)
- 아키텍처 재설계 (클래스 분리·인터페이스 도입·파일 대이동은 범위 밖)

**전제**
`dev` 브랜치, UE 5.8.1 소스 빌드, 게이트는 로컬 수동 실행.

---

## 1. 항목 요약

| ID | 제목 | 티어 | 동작 변경 | 주 게이트 |
|---|---|---|---|---|
| R-01 | 템플릿 잔재 제거 (`Variant_*` + Build.cs + 플러그인) | 1 | 없음 | 빌드 + 쿡 + dedi-verify |
| R-02 | 로그 카테고리 신설 (`LogTemp` 77곳 → `LogRE*`) | 2 | 없음 | dedi-verify (로그 문자열 판정) |
| R-03 | 널·불변식 계약 명시화 (`check`/`ensure` 0건 → 정책 적용) | 2 | 없음 | 빌드 + 헤드리스 프로브 |
| R-04 | 보스 패턴 정의 단일 출처 테이블 | 1 | 없음 | 패턴 15종 스폰 수 동일성 |
| R-05 | `Multicast_FireArtillery_Implementation` 259줄 분해 | 1 | 없음 | 고정 시드 착지점 동일성 |
| R-06 | Mass 배치 스폰 (`BatchCreateEntities`) | 1 | **성능만** | profile.ps1 전후 p99 |
| R-07 | 히트 반경 ↔ 렌더 스케일 숨은 결합 해소 + 탄 데미지 Settings 이관 | 1 | 없음 | 히트 반경 수치 동일성 |
| R-08 | 헤드리스 프로브를 프로덕션 클래스에서 분리 | 2 | 없음 | dedi-verify 전 항목 PASS |

티어 1 = 포폴 서사 직결. 티어 2 = 코드 품질 신호.

---

## 2. R-01 — 템플릿 잔재 제거

### 현상 (실측)

```
Source/Project_RE/  142 파일 중
  Variant_Combat/         18 파일   ← 코드 참조 0
  Variant_Platforming/     8 파일   ← 코드 참조 0
  Variant_SideScrolling/   6 파일   ← 코드 참조 0
```

`Variant_*` 밖에서 이 심볼들을 참조하는 `.cpp`/`.h` 가 **하나도 없다.** 확인:

```bash
grep -rl "Variant_Combat\|Variant_Platforming\|Variant_SideScrolling" \
  Source/Project_RE --include=*.cpp --include=*.h | grep -v "^Source/Project_RE/Variant_"
# → 출력 없음
```

파생 잔재도 같이 있다.

| 위치 | 잔재 |
|---|---|
| `Project_RE.Build.cs` | `PublicIncludePaths` 20줄 중 **13줄이 Variant 경로** |
| `Project_RE.Build.cs` | `StateTreeModule`, `GameplayStateTreeModule` 의존 — 실사용 코드에서 참조 0 |
| `Project_RE.uproject` | `StateTree`, `GameplayStateTree`, `CascadeToNiagaraConverter` 플러그인 |

`AIModule` 은 **살려야 한다** — `REPlayerController.cpp:501` 이
`UAIBlueprintHelperLibrary::SimpleMoveToLocation` 을 쓴다. 이름만 보고 지우면 안 되는 유일한 항목이다.

### 왜 고치나

1. 코드를 읽으러 온 사람이 **디렉터리 목록에서 제일 먼저 밟는 지뢰**다.
   "탄막 프로젝트"라고 소개했는데 `Variant_SideScrolling/AI/` 가 있으면 첫인상이 무너진다.
2. 실사용 코드 비율이 실제보다 낮게 보인다 — 142파일 중 32파일이 남의 코드다.
3. 죽은 모듈 의존은 **빌드 시간을 실제로 먹는다.**

### 위험과 예외

| 위험 | 대응 |
|---|---|
| `Content/` 안 템플릿 맵/BP 가 이 클래스를 참조 중일 수 있음 | 삭제 전 에디터에서 Reference Viewer 또는 쿡 로그로 확인. 참조하는 에셋도 같이 정리 |
| `DefaultEngine.ini` 에 `ActiveClassRedirects` 존재 (`TP_ThirdPersonGameMode` → `Project_REGameMode`) | **리다이렉트 대상은 남긴다.** 지우면 기존 에셋이 클래스를 못 찾는다 |
| `Project_RECharacter` / `Project_REGameMode` / `Project_REPlayerController` (모듈 루트) | Variant 와 별개다. 참조 여부 개별 확인 후 판단. **불확실하면 남긴다** |
| 플러그인 비활성화가 쿡에 영향 | 플러그인 변경은 **별도 커밋**으로 분리해 롤백 지점을 남긴다 |

### 게이트

1. `Development Editor` 빌드 통과
2. **풀 유니티 빌드** 통과 (adaptive non-unity 만으로는 C4459 류를 못 잡는다 — 실제 전례 있음)
3. `dedi-verify.ps1 -Clients 2` PASS
4. 에디터에서 `Main.umap` 열림 + 쿡 경고 없음

### 커밋 분할

```
chore(build): Variant_* 템플릿 잔재 제거 (32 파일)
chore(build): Build.cs IncludePaths/모듈 의존 정리
chore(uproject): 미사용 플러그인 비활성화
```

---

## 3. R-02 — 로그 카테고리 신설

### 현상 (실측)

```
UE_LOG 호출:        77
그중 LogTemp:       77   (100%)
전용 카테고리:       0
```

### 왜 고치나

1. **`LogTemp` 는 쉬핑에서 필터가 불가능하다.** 엔진 전체가 같은 카테고리를 쓴다.
2. **검증 스크립트가 로그로 판정한다.** `dedi-verify.ps1` 과 헤드리스 프로브가
   로그 문자열을 grep 해서 PASS/FAIL 을 낸다. 지금 Verbose 경로를 켜려면
   `-LogCmds="LogTemp Verbose"` 를 써야 하는데, 이건 **엔진 전체 Verbose** 다.
   대쉬 무적 판정 로그 하나 보려고 수만 줄을 뒤진다.
   카테고리가 있으면 `-LogCmds="LogREBullet Verbose"` 로 딱 그것만 켠다.
3. `LogTemp` 는 이름 그대로 "임시"다. 77곳이 임시면 아무것도 임시가 아니다.

### 설계

카테고리를 서브시스템 경계와 일치시킨다 — 판정 스크립트가 그 경계로 필터하기 때문이다.

| 카테고리 | 범위 |
|---|---|
| `LogRE` | 게임루프 공통 (GameMode, CharacterBase, PlayerController) |
| `LogREBullet` | Mass 탄막 (Sim/Render/Hit/Arc/Fx, SpawnSubsystem) |
| `LogRENet` | 복제·RPC 경로 (Multicast 구현, 프로브) |

### 위험과 예외

| 위험 | 대응 |
|---|---|
| **스크립트가 로그 문자열로 판정한다** | `[RE]` 접두어와 메시지 본문을 **한 글자도 바꾸지 않는다.** 바꾸는 건 첫 인자(카테고리)뿐 |
| 기본 verbosity 가 달라져 로그가 안 나옴 | `DECLARE_LOG_CATEGORY_EXTERN(LogRE, Log, All)` — 기본 `Log`, 컴파일 상한 `All` 로 `LogTemp` 와 동일하게 맞춘다 |
| `dedi-verify.ps1` 이 `-LogCmds="LogTemp Verbose"` 를 하드코딩 | 스크립트도 같은 커밋에서 갱신. **누락 시 Verbose 판정이 조용히 죽는다** |

### 게이트

- `dedi-verify.ps1 -Clients 2` PASS (로그 판정이 전부 살아 있어야 함)
- 헤드리스 프로브 3종 완주
- `grep -c "UE_LOG(LogTemp" Source/Project_RE --include=*.cpp -r` → 0

---

## 4. R-03 — 널·불변식 계약 명시화

### 현상 (실측)

```
check / checkf / ensure / ensureMsgf 호출:   0        ← 14,257 LOC 전체에서
Cast<> 호출:                                30
IsValid() 호출:                              7
GetWorld() 호출:                            40
  그중 널 검사 없이 즉시 역참조:            17
```

혼재 상태가 문제다. 같은 파일 안에서 한 줄은 막고 한 줄은 안 막는다:

```cpp
// REBossCharacter.cpp:613   — 막는다
UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<...>() : nullptr;

// REBossCharacter.cpp:910   — 안 막는다
if (const UREBulletRenderSubsystem* RS = GetWorld()->GetSubsystem<...>())
```

### 왜 고치나

지금은 널이면 `EXCEPTION_ACCESS_VIOLATION` 으로 **원인 메시지 없이** 죽는다.
데디 서버에서 이게 나면 남는 건 콜스택뿐이고, 그 콜스택이 `GetWorld()` 를 가리켜도
"왜 월드가 없었나"는 안 남는다.

리뷰어 관점에서 더 중요한 건 **정책이 없다는 것 자체**다.
`ensure` 가 0건이라는 건 "무엇이 불변식이고 무엇이 정상 폴백인지" 코드가 말하지 않는다는 뜻이다.

### 설계 — 정책부터 정하고 적용한다

| 상황 | 처리 | 이유 |
|---|---|---|
| **정상적으로 발생 가능** (데디에 ISM 없음, 폰 없음, 전원 사망) | `if` 폴백 + `UE_LOG` | 예외가 아니다. 설계된 경로다 |
| **프로그래머 실수로만 발생** (프로세서가 월드 없이 Execute, 아키타입 미생성) | `ensureMsgf` + 조기 반환 | 개발 중엔 콜스택 + 메시지, 쉬핑에선 계속 실행 |
| **여기서 계속 진행하면 데이터가 깨짐** | `checkf` | 지금 코드엔 해당 없음 — 억지로 만들지 않는다 |

핵심은 **`ensure` 를 늘리는 게 아니라 두 부류를 가르는 것**이다.
`GetWorld()` 17곳 중 대부분은 첫 번째 부류라 `ensure` 가 아니라 `if` 가 맞는 답이다.

### 위험과 예외

| 위험 | 대응 |
|---|---|
| Mass `Execute()` 가 워커 스레드에서 돌 수 있다 | `ensure` 는 스레드 세이프하지만 **메시지 순서가 보장되지 않는다.** 워커에서 도는 프로세서(`REBulletSimProcessor`)에는 `ensure` 대신 `if` 폴백만 쓴다 |
| `ensure` 가 매 프레임 도는 경로에 들어가면 첫 실패 후 계속 로그 | `ensure` 는 기본이 1회 발화다. `ensureAlways` 를 **쓰지 않는다** |
| 폴백을 넣으면서 조용한 실패를 만든다 | 모든 폴백에 로그를 단다. **로그 없는 early-return 금지** — 이 프로젝트가 #50/#88 에서 겪은 게 정확히 그것이다 |

### 게이트

- 빌드 통과 (Development + Shipping 양쪽 — `ensure` 는 쉬핑에서 컴파일 아웃된다)
- 헤드리스 프로브 3종 완주 (새 `ensure` 가 아무것도 발화하지 않아야 정상)
- `dedi-verify.ps1 -Clients 2` PASS

---

## 5. R-04 — 보스 패턴 정의 단일 출처 테이블 ★

### 현상 (실측) — 같은 지식이 7곳에 병렬로 나열돼 있다

| # | 위치 | 나열하는 것 |
|---|---|---|
| 1 | `REBossCharacter.cpp:53` `REBoss::IsArcPattern` | 곡사 계열 8종 |
| 2 | `REBossCharacter.cpp:70` `REBoss::IsBloomPattern` | 블룸 계열 3종 |
| 3 | `REBossCharacter.cpp:355` `Pool[15]` | 로테이션 풀 |
| 4 | `REBossCharacter.cpp:397` `BeginPhase` switch | 패턴별 `PhaseSec` + 로그 이름 (14 case) |
| 5 | `REBossCharacter.cpp:455` `FireIntervalFor` switch | 패턴별 발사 간격 (12 case) |
| 6 | `REBossCharacter.cpp:165` `LookForPattern` switch | 패턴별 외관 (14 case) |
| 7 | `REBossCharacter.cpp:632` `Multicast_FireArtillery_Impl` | 패턴별 체공/고도/발수 (7단 if-else) |

**패턴 16번째를 추가하려면 7곳을 고쳐야 하고, 하나를 빠뜨려도 컴파일이 통과한다** —
전부 `default:` 가 있기 때문이다. 그 결과는 크래시가 아니라 **조용한 오동작**이다:
새 패턴이 Spiral 의 발사 간격으로 돌거나, 곡사인데 직선탄 경로를 타거나, 외관이 안 바뀐다.

코드 자신이 이미 이 문제를 알고 있다:

> `// 두 곳에 각각 나열하면 패턴을 늘릴 때 한쪽만 고쳐 조용히 어긋난다.`
> — `REBossCharacter.cpp:56`

주석은 문제를 기록했지만 **막지는 못한다.** 컴파일러가 막아야 한다.

### 왜 고치나 (포폴 관점)

이건 "코드가 지저분하다" 수준이 아니라 **확장 지점의 설계 실패**다.
15종을 만들면서 7곳을 15번씩 손으로 동기화한 흔적 자체가 리뷰어에게 보인다.
반대로, 테이블 하나 + `static_assert` 로 정리해 두면
"패턴을 늘리는 비용을 O(7)에서 O(1)로 줄였다"가 그대로 서술 소재가 된다.

### 설계 — 왜 DataAsset 이 아니라 `constexpr` 테이블인가

세 안을 놓고 골랐다.

| 안 | 장점 | 채택 안 한 이유 |
|---|---|---|
| `UDataAsset` (패턴당 에셋 1개) | 디자이너가 에디터에서 튜닝 | 에셋 15개 추가 = 쿡·머지 충돌 표면 증가. **디자이너가 없다.** 런타임 로드 실패 경로가 새로 생긴다 |
| `UDeveloperSettings` (ini) | 리빌드 없이 튜닝 | 파라미터가 135개다. ini 에 다 넣으면 **주석 135개가 사라진다** — 이 프로젝트에서 그 주석이 자산이다 |
| **`constexpr` 테이블 (채택)** | 컴파일 타임 검증, 런타임 비용 0, 주석 보존 | 튜닝에 리빌드 필요 |

채택 근거: **지금 필요한 건 튜닝 편의가 아니라 누락 방지다.**
`static_assert` 로 "테이블 크기 == enum 개수"를 강제하면
패턴을 추가하면서 테이블 항목을 빠뜨리면 **컴파일이 실패한다.** 이게 목적 전부다.

> ini/DataAsset 로 올릴 시점: 튜닝하는 사람이 프로그래머가 아니게 될 때.
> 그 전에는 리빌드 비용보다 쿡·로드 경로 추가 비용이 크다.

### 위험과 예외

| 위험 | 대응 |
|---|---|
| 135개 `constexpr` 를 테이블로 옮기며 **값이 하나 바뀌면 게임플레이가 조용히 변한다** | 값은 **복사만** 한다. 이름·계산식 변경 금지. 옮긴 뒤 원본 상수를 지우지 말고 `static_assert(Table[i].PhaseSec == LissaPhaseSec)` 로 한 커밋 동안 이중 검증한 뒤 제거 |
| `Homing` 이 enum 에는 있는데 Pool 에는 없다 (백로그 스텁) | 테이블에 `bInRotation` 필드를 두고 `Homing` 은 `false`. **enum 에서 빼지 않는다** — 네트워크 페이로드가 enum 값을 싣는다 |
| `EArtilleryShape` 는 별개 축이다 | 이번 범위 밖. Shape 는 Artillery 한 패턴 안의 하위 선택이라 테이블 필드가 아니다 |

### 게이트

- 15종 각각에 대해 `re.Debug.BossPattern <N>` 고정 → **볼리당 스폰 수와 페이즈 길이 로그가 리팩터 전과 동일**
- 리팩터 전 로그를 먼저 캡처해 둔다 (이게 유일한 회귀 판정 근거다)

---

## 6. R-05 — `Multicast_FireArtillery_Implementation` 분해

### 현상 (실측)

`REBossCharacter.cpp:608~867` — **한 함수 259줄.** 안에 병렬 bool 이 7개 있다:

```cpp
const bool bStorm  = (Pattern == EBulletPattern::ArtilleryStorm);
const bool bLissa  = (Pattern == EBulletPattern::LissajousStorm);
const bool bVortex = (Pattern == EBulletPattern::BezierVortex);
const bool bRoseF  = (Pattern == EBulletPattern::RoseField);
const bool bDome   = (Pattern == EBulletPattern::AerialDome);
const bool bSpiro  = (Pattern == EBulletPattern::Spirograph);
const bool bMicro  = (Pattern == EBulletPattern::MicroMissile);
```

이 7개가 함수 안에서 **세 번 다시 분기한다** — 파라미터 결정, 착지점 생성, 샷 성형.
같은 조건을 세 번 쓰므로 한 곳만 고치면 어긋난다.

추가로 `EArtilleryShape` switch 에 **`default:` 가 없다**(`:734~756`).
Shape 를 늘리면 `Targets` 가 빈 채로 `SpawnArcBulletBatch(0)` 이 호출되고,
**아무 일도 안 일어난 것과 구별되지 않는다.**

### 왜 고치나

259줄 함수는 포트폴리오에서 단독으로 감점이다. 하지만 진짜 이유는 따로 있다:
**이 함수는 서버와 클라가 같이 실행하는 결정론 경로다.** 여기서 갈라지면 탄막이 어긋난다.
분기 조건이 세 번 흩어져 있다는 건 그 결정론이 구조로 보장되지 않는다는 뜻이다.

### 설계 — 3단 분리 (동작 불변)

```
Multicast_FireArtillery_Implementation
  ├─ ResolveArcParams(Pattern)        → FArcPhaseParams { FlightTime, MaxHeight, Count }   ← R-04 테이블 조회
  ├─ BuildArcTargets(...)             → TArray<FVector>                                     ← 착지 지오메트리
  └─ ShapeArcShots(...)               → TArray<FArcBulletSpawnParams>                       ← 제어점·어긋내기
```

7개 bool 이 사라지고 테이블 필드 조회로 바뀐다. R-04 에 의존한다 — **R-04 를 먼저 한다.**

### 위험과 예외

| 위험 | 대응 |
|---|---|
| **결정론 경로다.** 순서가 바뀌면 서버/클라가 갈린다 | `FRandomStream CallRng(CallSeed)` 를 **뽑는 순서**를 절대 바꾸지 않는다. `GenRandom` 호출 위치가 이동하면 난수열이 달라진다 |
| 어긋내기(`P.Elapsed +=`)의 `continue` 가 배열 인덱스와 얽혀 있다 | `Shots` 는 `Targets` 보다 짧을 수 있다. 인덱스 `i` 는 **`Targets` 기준**이며 `Shots.Num()` 이 아니다. 분해 시 이 대응을 깨지 않는다 |
| Shape switch 누락 방어를 추가하면 동작이 바뀐다 | 빈 `Targets` 는 **지금도 아무것도 안 한다.** 로그만 추가한다 — `ensureMsgf` + 조기 반환. 동작은 동일 |

### 게이트

- 패턴 8종(곡사 전부) 고정 + `CallSeed` 고정 → **착지점 좌표 로그가 리팩터 전과 바이트 단위로 동일**
- `dedi-verify.ps1 -Clients 2` — 서버/클라 착지점 일치

---

## 7. R-06 — Mass 배치 스폰

### 현상 (실측)

`REBulletSpawnSubsystem.cpp:51`

```cpp
void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
    for (const FBulletSpawnParams& P : Params)
    {
        SpawnBullet(P.Location, P.Velocity, P.Lifetime, P.ColorSel);   // ← N회
    }
}
```

`SpawnBullet` 하나가 매번 하는 일:

1. `GetWorld()` → `GetSubsystem<UMassEntitySubsystem>()` → `GetMutableEntityManager()`
2. `EnsureArchetype()` (`IsValid()` 체크지만 호출은 매번)
3. `CreateEntity()` — **엔티티 1개씩**
4. `GetFragmentDataChecked` 3회

현재 게임플레이 발수는 `BulletsPerShot = 48`(ini)이고, 곡사 폭풍은 `StormCount` 단위 볼리다.
`SpawnArcBulletBatch` 도 같은 구조다.

### 왜 고치나 — 이게 티어 1인 이유

**이 프로젝트의 주장 자체가 성능이다.** 리뷰어가 Mass 스폰 경로를 열었을 때
"배치 API 를 안 쓰고 루프로 개별 생성"이 보이면, 앞의 모든 수치가 의심받는다.

그리고 이건 **측정 가능한 리팩터**다. `profile.ps1` 이 이미 있으므로
전후 p99 를 붙일 수 있다 — 포폴에 그대로 들어가는 유일한 항목이다.

### 설계

```
GetEntityManager()  1회 조회  →  EnsureArchetype 1회  →  BatchCreateEntities(N)  →  프래그먼트 뷰로 일괄 기입
```

`FMassEntityManager::BatchCreateEntities` 가 아키타입 하나에 N개를 한 번에 만든다.
청크 배치가 연속이라 이어지는 프래그먼트 기입도 캐시 친화적이다.

### 위험과 예외

| 위험 | 대응 |
|---|---|
| **엔티티 핸들 반환 계약이 바뀐다** | 현재 `SpawnBullet` 은 핸들을 반환한다. 호출자가 그 값을 쓰는지 먼저 확인 — 안 쓰면 배치 경로에서 반환을 버린다. **쓰는 곳이 있으면 배열로 돌려준다** |
| `Params` 가 비었을 때 | `BatchCreateEntities(0)` 호출 금지. 조기 반환 |
| 스폰 실패(EntityManager 널)를 배치에서도 로그해야 함 | 지금 개별 스폰이 발당 1회 경고를 찍는다. 배치로 바꾸면 **볼리당 1회**가 된다 — 로그 폭발이 줄어드는 건 이득이지만, 판정 스크립트가 발당 로그를 세고 있는지 확인 |
| 프래그먼트 기입 순서 | `FTransformFragment` → `FBulletSimFragment` 순서와 초기값(`InstanceIndex = INDEX_NONE`)을 그대로 유지 |
| 성능이 **안 좋아질** 가능성 | 그럴 수 있다. **게이트가 판단한다** — 나빠지면 되돌린다. 되돌리는 것도 결과다 |

### 게이트

리팩터 전후 각각:

```powershell
scripts/profile.ps1 -Bullets -1 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.BossPattern 0" -Label spawn_before
scripts/profile.ps1 -Bullets -1 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.BossPattern 3" -Label storm_before
```

판정: **GT mean/p99 가 나빠지지 않을 것.** 좋아지면 그 수치를 문서에 싣는다.

---

## 8. R-07 — 히트 반경 ↔ 렌더 스케일 숨은 결합

### 현상 (실측) — 3파일이 손으로 동기화돼 있다

```cpp
// Mass/REBulletRenderProcessor.cpp:27
constexpr float BulletScale = 0.5f;

// Mass/REBulletHitProcessor.cpp:24
//   주석: "탄환 시각 반경 25(BulletScale 0.5 × Sphere 반경 50) + 플레이어 캡슐 반경 ~35"
constexpr float HitRadius = 60.f;

// Baseline/REBulletActor.cpp:11
//   주석: "Mass와 동일 — REBulletRenderProcessor.cpp:14 BulletScale."
constexpr float ActorBulletScale = 0.5f;
```

**이미 어긋나기 시작했다.** `REBulletActor.cpp:11` 주석은 `BulletScale` 이
`REBulletRenderProcessor.cpp:14` 에 있다고 말하지만 실제로는 **27번 줄**이다.
값은 아직 맞지만 참조는 이미 썩었다.

### 왜 고치나

이건 **게임플레이 공정성 버그의 씨앗**이다. `BulletScale` 을 바꾸면
"눈에 안 닿았는데 맞거나, 닿았는데 안 맞는" 상태가 된다 — 회피 게임에서 치명적이다.
그리고 컴파일러가 아무것도 안 잡아준다.

리뷰어에게 이 항목은 **"측정하는 사람이 판정 반경은 손으로 맞추고 있었나"** 로 읽힌다.
소재 ①·⑤ 의 신뢰도를 직접 깎는다.

### 설계 — 유도식 하나로 묶는다

```
공용 헤더 (Mass/REBulletFragments.h 또는 신설 REBulletGeometry.h)
  EngineSphereRadius   = 50.f      // /Engine/BasicShapes/Sphere 원본 반경
  BulletScale          = 0.5f      // 단일 출처
  BulletVisualRadius   = EngineSphereRadius * BulletScale     // 유도
  PlayerCapsuleRadius  = 35.f      // 캐릭터 캡슐 (출처 명시)
  HitRadius            = BulletVisualRadius + PlayerCapsuleRadius   // 유도
```

`static_assert(HitRadius == 60.f)` 를 **한 커밋 동안** 붙여 현재 값과 동일함을 컴파일 타임에 증명한 뒤 제거한다.

### 위험과 예외

| 위험 | 대응 |
|---|---|
| **유니티 빌드 변수 섀도잉** — `BulletScale` 이 익명 네임스페이스로 두 파일에 있어 이미 C4459 전례가 있다 | 공용 헤더의 `namespace REBulletGeometry` 안에 넣는다. 익명 네임스페이스 금지. **풀 유니티 빌드로 확인** (adaptive non-unity 는 이걸 못 잡는다) |
| `PlayerCapsuleRadius = 35.f` 의 출처가 주석뿐 | 캐릭터 캡슐 실제 값을 확인해서 맞춘다. 다르면 **값을 바꾸지 말고 상수 이름을 실제에 맞춘다** — 판정 반경 변경은 게임플레이 변경이다 |
| 히트 반경이 1이라도 바뀌면 회귀 | `static_assert` 로 60.f 고정. 바뀌면 컴파일 실패 |

### 곁들여: 탄 데미지 Settings 이관

```cpp
// REBulletHitProcessor.cpp:26 — 하드코딩
constexpr float BulletDamage = 10.f;
```

플레이어 HP·공격 데미지·보스 HP 는 전부 `UREStatsSettings`(ini)인데 **탄 데미지만 cpp 상수**다.
밸런스 축 하나가 리빌드를 요구한다. `UREStatsSettings::BossBulletDamage` 로 옮기고
기본값 `10.f`, `DefaultGame.ini` 에 명시한다.

> 주의: `REStatsSettings.h` 의 `BulletsPerShot` 주석이 "기본 16 × 15/0.1 = 2400" 이라고 하는데
> ini 실제 값은 `48` / `0.15` 다. **주석 드리프트** — 같이 고친다.

### 게이트

- `static_assert(HitRadius == 60.f)` 통과
- 풀 유니티 빌드 통과
- 헤드리스 프로브의 `hit boss` / 피격 게이트 PASS

---

## 9. R-08 — 헤드리스 프로브 분리

### 현상 (실측)

```
Core/REPlayerController.cpp   714줄
  그중 프로브    579~714       135줄 (19%)
Core/REPlayerController.h
  프로브 멤버    6개 (FTimerHandle 4, FVector 2) + 함수 3개
```

게이트는 **런타임 하나뿐**이다:

```cpp
// REPlayerController.cpp:176
if (HasAuthority() && FApp::IsUnattended())
{
    RunHeadlessMoveProbe();
    RunHeadlessFireProbe();
    RunHeadlessDashProbe();
}
```

### 왜 고치나

1. **컴파일 게이트가 없다.** 프로브 코드가 쉬핑 빌드에 그대로 들어간다.
   `-unattended` 는 쉬핑에서도 커맨드라인으로 넘길 수 있으므로 **출시 빌드에서 프로브가 켜진다.**
   실제 피해는 크지 않지만(프로브가 자기 폰을 움직이는 정도), 리뷰어가 보면 바로 지적할 지점이다.
2. 프로덕션 컨트롤러의 **19%가 테스트 하네스**다. 클래스 책임이 흐려진다.
3. 역설적으로 이 프로브는 **포폴 자산**이다(소재 ④). 별도 클래스로 서면 오히려 눈에 띈다 —
   `UREHeadlessProbeComponent` 라는 이름 자체가 "검증을 코드로 짰다"를 말한다.

### 설계

```
Source/Project_RE/Debug/REHeadlessProbeComponent.{h,cpp}
  #if !UE_BUILD_SHIPPING  로 전체를 감싼다
  AREPlayerController::BeginPlay 에서 조건부 생성 (기존 런타임 게이트 유지)
```

멤버 6개와 함수 3개가 컨트롤러에서 빠진다. 컨트롤러 `.cpp` 는 714 → ~580줄.

### 위험과 예외

| 위험 | 대응 |
|---|---|
| **`dedi-verify.ps1` 이 프로브 로그 문자열로 PASS/FAIL 을 판정한다** | 로그 문자열(`[Move] probe start:`, `[Move] probe dist=` 등)을 **한 글자도 바꾸지 않는다.** 이게 이 항목의 사실상 유일한 게이트다 |
| 프로브가 `Server_RequestMove` 등 컨트롤러 RPC 를 호출한다 | 컴포넌트에서 소유 컨트롤러를 통해 호출. 접근 지정자 조정 필요 — `public` 확대 대신 `friend` 또는 기존 RPC 가 이미 public 인지 확인 |
| `AREGameMode::NotifyProbeComplete` 완주 카운트 | 호출 위치가 프로브 내부다. 컴포넌트로 옮겨도 **호출 횟수와 순서 동일** |
| `#if !UE_BUILD_SHIPPING` 안에서 `GENERATED_BODY()` 사용 | UHT 가 조건부 UCLASS 를 처리하는 방식에 주의. 클래스는 항상 컴파일하고 **함수 본문만** 조건부로 비우는 편이 안전하다. 빌드로 확인 |

### 게이트

- `dedi-verify.ps1 -Clients 2` PASS — **프로브 3종 완주 로그가 전부 동일하게 나올 것**
- Shipping 구성 빌드 통과
- 프로브 로그 문자열 diff: 없음

---

## 10. 실행 순서와 의존

```
R-01  템플릿 잔재 제거          (독립, 먼저 — 이후 모든 빌드가 빨라진다)
  ↓
R-02  로그 카테고리             (독립)
  ↓
R-04  패턴 테이블 단일 출처     ★ R-05 의 전제
  ↓
R-05  FireArtillery 분해        (R-04 의존)
  ↓
R-07  히트 반경 결합 해소       (독립이지만 R-04 와 같은 파일군을 건드리므로 뒤에)
  ↓
R-06  Mass 배치 스폰            (독립, 측정 필요 — 앞 항목들이 끝난 안정 상태에서 재야 함)
  ↓
R-03  널·불변식 계약            (마지막 — 앞 리팩터로 생긴 새 코드에도 정책 적용)
  ↓
R-08  헤드리스 프로브 분리      (마지막, 파일 이동이라 충돌 위험이 가장 큼)
```

**R-06 을 뒤에 두는 이유:** 성능 측정이 게이트인데, 앞 항목들이 코드를 흔드는 중에 재면
무엇이 무엇을 바꿨는지 못 가른다. 다른 변경이 멎은 뒤에 잰다.

---

## 11. 전체 완료 판정

| # | 게이트 | 통과 조건 | 결과 |
|---|---|---|:--:|
| 1 | Development Editor 빌드 | 경고 0 (신규) | ☑ |
| 2 | **풀 유니티 빌드** | 통과 — adaptive non-unity 로는 C4459 를 못 잡는다 | ☑ |
| 3 | Shipping 빌드 | 통과 | ☑ |
| 4 | 헤드리스 프로브 3종 | 완주, 로그 문자열 리팩터 전과 동일 | ☑ |
| 5 | `dedi-verify.ps1 -Clients 2` | PASS | ☑ 21/21 |
| 6 | 패턴 15종 스폰 수 로그 | 리팩터 전과 동일 | ☑ 15/15 |
| 7 | 곡사 8종 착지점 좌표 (고정 시드) | 리팩터 전과 동일 | ☑ 1,767 샷 |
| 8 | `profile.ps1` GT mean/p99 | 나빠지지 않음 | ☑ (R-06 기각) |
| 9 | `grep -c "UE_LOG(LogTemp"` | 0 | ☑ |
| 10 | `ensure`/`check` 정책 적용 | 워커 스레드 프로세서에 `ensure` 없음 | ☑ |

**6·7번은 리팩터를 시작하기 전에 기준 로그를 먼저 캡처해야 한다.** 그게 유일한 회귀 판정 근거다.

> **실행하며 알게 된 것:** 7번은 라이브 실행 로그로는 성립하지 않는다. 곡사 8종 중
> 5종이 착지점을 `ServerTime` 에서 유도해 **리팩터를 안 해도 실행마다 좌표가 다르다.**
> 입력을 전부 고정하는 임시 덤프를 STEP 0 에서 넣어야 했다.
> 그리고 8번은 **기준선이 있는 것만으로 부족하다** — 두 시점 사이에 다른 항목이 끼어
> 있으면 그 비교는 그 항목의 측정이 아니다. R-06 을 그렇게 잘못 판정할 뻔했다.
> 자세한 것은 [`baseline/README.md`](baseline/README.md) 와
> [`baseline/perf-r06.md`](baseline/perf-r06.md).

---

## 12. 이 리팩터링에서 의도적으로 안 하는 것

| 안 하는 것 | 이유 |
|---|---|
| 패턴 파라미터를 DataAsset/ini 로 | §5 참조. 디자이너가 없다. 지금 필요한 건 튜닝 편의가 아니라 누락 방지다 |
| `AREBossCharacter`(1,150+622줄) 클래스 분해 | 범위 밖(아키텍처). R-04/R-05 로 줄어드는 만큼만 |
| Mass 프로세서 책임 재분할 | 스레드 계약이 얽혀 있어 회귀 위험이 이득보다 크다 |
| `Baseline/` (Actor 비교군) 정리 | **소재 ①의 증거물이다.** 지우면 비교 리포트를 재현할 수 없다 |
| `Project_RECharacter` 등 모듈 루트 템플릿 파일 | 참조 관계가 불확실하다. 확인되면 별도 이슈 |
| 테스트 프레임워크 도입 | 이미 헤드리스 프로브 + dedi-verify 가 그 역할을 한다. 중복 |
