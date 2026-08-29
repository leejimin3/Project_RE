# GOAL — 포트폴리오 하드닝 리팩터링 (R-01 ~ R-08)

> 이 문서는 **제로컨텍스트 세션에 그대로 붙여넣는 목표**다.
> 원본: `docs/portfolio/refactor-plan.md` (왜) + `docs/portfolio/refactor-before-after.md` (코드 전후)

---

## 목표

`E:\UnrealProjects\Project_RE` (UE 5.8.1 소스 빌드, Gitflow, 현재 `dev`) 에서
포트폴리오 공개를 앞두고 **동작을 바꾸지 않는 리팩터링 8건**을 수행한다.

**정본 문서 두 개를 먼저 끝까지 읽어라. 이 goal 은 요약이고, 그 둘이 사양이다.**

1. `docs/portfolio/refactor-plan.md` — 항목별 근거·위험·게이트·실행 순서
2. `docs/portfolio/refactor-before-after.md` — 항목별 Before(현행 실물) / After(제안 코드)

---

## 성공 기준 (전부 통과해야 완료)

| # | 게이트 | 판정 |
|---|---|---|
| 0 | **기준선 로그 캡처가 리팩터 시작 전에 존재** | 아래 STEP 0 산출물 |
| 1 | Development Editor 빌드 | 신규 경고 0 |
| 2 | **풀 유니티 빌드** | 통과 (adaptive non-unity 로는 C4459 를 못 잡는다 — 전례 있음) |
| 3 | Shipping 빌드 | 통과 |
| 4 | 헤드리스 프로브 3종 | 완주 + **로그 문자열이 기준선과 동일** |
| 5 | `scripts/dedi-verify.ps1 -Clients 2` | PASS |
| 6 | 패턴 15종 스폰 수·페이즈 길이 로그 | 기준선과 동일 |
| 7 | 곡사 8종 착지점 좌표 (고정 CallSeed) | 기준선과 **바이트 단위** 동일 |
| 8 | `scripts/profile.ps1` GT mean / p99 | 기준선보다 나빠지지 않음 |
| 9 | `grep -rc "UE_LOG(LogTemp" Source/Project_RE --include=*.cpp` | 0 |
| 10 | 워커 스레드 프로세서(`REBulletSimProcessor`, `REArcSimProcessor`)에 `ensure` | 없음 |

---

## 절대 규칙

1. **동작 불변이 게이트다.** 게임플레이 수치를 하나라도 바꾸면 실패다.
   유일한 예외는 R-06(성능만 변경)이다.
2. **값은 복사만 한다.** 상수를 옮길 때 이름·계산식·값을 바꾸지 않는다.
   옮긴 뒤 원본을 즉시 지우지 말고, `static_assert` 로 동일함을 한 커밋 동안 증명한 뒤 제거한다.
3. **로그 메시지 본문과 `[RE]` 접두어를 한 글자도 바꾸지 않는다.**
   `dedi-verify.ps1` 과 헤드리스 프로브가 이 문자열로 PASS/FAIL 을 판정한다.
   바꾸는 것은 `UE_LOG` 의 첫 인자(카테고리)뿐이다.
4. **로그 없는 early-return 금지.** 조용한 실패가 이 프로젝트의 측정을 두 번 망쳤다(#50, #88).
5. **모호하면 멈추고 물어라.** 추측해서 진행하지 마라 (`CLAUDE.md` 규칙 5).
6. **범위 밖:** 새 기능, 클래스 분해/인터페이스 도입, 파일 대이동, 테스트 프레임워크 도입,
   `Baseline/`(Actor 비교군 — 소재 증거물) 정리, 패턴 파라미터의 DataAsset/ini 이관.

---

## 브랜치 · 커밋 · PR

- 브랜치: `dev` 에서 `feature/M8-portfolio-hardening` 분기 (Gitflow, `main` 직접 커밋 금지)
- 항목(R-xx)당 **커밋을 분리**한다. R-01 은 3커밋(파일 삭제 / Build.cs / uproject)으로 더 쪼갠다
- PR: **base = `dev`**, 이슈 메타(label·milestone·assignee·project) 미러링, Reviewer 생략, 본문 6개 필드 전부 채움
- 이슈를 새로 만들지, 만든다면 어느 마일스톤에 붙일지는 **작업 시작 전에 사용자에게 물어라.**
  현재 열린 마일스톤은 M5·M6·M7 이고 M8 은 없다

---

## STEP 0 — 기준선 캡처 (코드를 한 줄도 고치기 전에)

**이걸 빠뜨리면 게이트 4·6·7·8 을 판정할 근거가 사라진다.** #88 이 정확히 그 실패였다.

```powershell
# (a) 패턴 15종 — 스폰 수 / 페이즈 길이 / 패턴 이름 로그
foreach ($n in 0..14) {
  scripts/profile.ps1 -Bullets -1 -Frames 300 `
    -ExtraExec "re.Fx.Explosions 1,re.Debug.BossPattern $n" -Label "baseline_p$n"
}

# (b) 성능 기준선 — Spiral(0) / ArtilleryStorm(3) / LissajousStorm(6, 최악)
foreach ($n in 0,3,6) {
  scripts/profile.ps1 -Bullets -1 -Frames 720 `
    -ExtraExec "re.Fx.Explosions 1,re.Debug.BossPattern $n" -Label "baseline_perf_p$n"
}
scripts/profile-stats.ps1

# (c) 데디 검증 기준선 (프로브 로그 문자열 포함)
scripts/dedi-verify.ps1 -Clients 2
```

산출물을 `docs/portfolio/baseline/` 에 모아 커밋한다 (로그 발췌 + p99 표).
곡사 착지점 좌표(게이트 7)를 로그에서 못 얻으면, **임시로 좌표를 찍는 로그를 먼저 추가하고
그 커밋을 기준선으로 삼은 뒤 마지막에 되돌린다.**

---

## 실행 순서 (의존 있음 — 지켜라)

```
STEP 0  기준선 캡처
  ↓
R-01  템플릿 잔재 제거          독립. 먼저 하면 이후 빌드가 빨라진다
  ↓
R-02  로그 카테고리             독립. dedi-verify.ps1 의 -LogCmds 도 같은 커밋에서 갱신
  ↓
R-04  패턴 테이블 단일 출처     ★ R-05 의 전제
  ↓
R-05  FireArtillery 259줄 분해  R-04 의존
  ↓
R-07  히트 반경 결합 해소       R-04 와 같은 파일군이라 뒤에
  ↓
R-06  Mass 배치 스폰            성능 측정이 게이트 → 다른 변경이 멎은 뒤에 잰다
  ↓
R-03  널·불변식 계약            앞 리팩터로 생긴 새 코드에도 정책 적용
  ↓
R-08  헤드리스 프로브 분리      파일 이동이라 충돌 위험이 가장 크다 → 마지막
```

---

## 항목 요약 (상세는 정본 문서)

| ID | 무엇 | 핵심 근거 |
|---|---|---|
| **R-01** | `Variant_Combat/`·`Variant_Platforming/`·`Variant_SideScrolling/` 32파일 삭제 + `Build.cs` IncludePaths 13줄·`StateTreeModule`·`GameplayStateTreeModule` 제거 + 미사용 플러그인 비활성화 | 실사용 코드 참조 0. **`AIModule` 은 살려라** — `REPlayerController.cpp:501` 이 `UAIBlueprintHelperLibrary::SimpleMoveToLocation` 을 쓴다 |
| **R-02** | `UE_LOG` 77곳 전부 `LogTemp` → `LogRE` / `LogREBullet` / `LogRENet` | 지금은 Verbose 하나 켜려면 엔진 전체가 쏟아진다. 스크립트 `-LogCmds` 도 같이 갱신 |
| **R-03** | `check`/`ensure` **0건** → 정책 수립 후 적용. `GetWorld()` 17곳 무가드 역참조 정리 | 부류 1(정상 부재)=`if` 폴백+로그 / 부류 2(프로그래머 실수)=`ensureMsgf`. **워커 스레드엔 `ensure` 금지** |
| **R-04** ★ | 패턴 지식이 **7곳**에 병렬 나열 → `REBossPatternTable.h` 단일 테이블 + `static_assert` | 패턴 추가 시 7곳 수정, 하나 빠져도 컴파일 통과 → 조용한 오동작. `EBulletPattern` 에 `Count UMETA(Hidden)` 센티널 추가(기존 값 불변 = 네트워크 호환 유지) |
| **R-05** | `Multicast_FireArtillery_Implementation` **259줄** / 병렬 bool 7개 / 같은 조건 3회 재분기 → 3단 분리. `EArtilleryShape` switch `default:` 방어 추가 | **결정론 경로다.** `FRandomStream CallRng` 소비 순서·횟수를 바꾸면 서버/클라가 갈린다. 루프 인덱스 `i` 는 `Shots` 가 아니라 **`Targets` 기준** |
| **R-06** | `SpawnBulletBatch` 가 N회 개별 `SpawnBullet` → `GetEntityManager()`·`EnsureArchetype()` 1회 + `BatchCreateEntities` | **성능 측정이 판정한다.** 배치 API 시그니처는 엔진 헤더에서 확인할 것. 기대와 다르면 최소 목표는 조회를 루프 밖으로 빼는 것. 나빠지면 되돌린다 |
| **R-07** | `BulletScale`(렌더) ↔ `HitRadius`(판정) ↔ `ActorBulletScale`(Baseline) 3파일 수동 동기 → `REBulletGeometry.h` 유도식. + `BulletDamage` 를 `REStatsSettings` 로 | 참조가 이미 썩었다(`REBulletActor.cpp:10` 이 가리키는 줄번호가 틀림). 익명 네임스페이스 금지 — 유니티 빌드 C4459 전례 |
| **R-08** | 헤드리스 프로브 135줄(컨트롤러의 19%) → `UREHeadlessProbeComponent` + `#if !UE_BUILD_SHIPPING` | 지금은 런타임 게이트뿐이라 쉬핑 빌드에 실린다. **`GENERATED_BODY()` 를 조건부 블록에 넣지 마라** — 멤버와 본문만 감싼다 |

---

## 완료 후 해야 할 일

1. `docs/portfolio/refactor-before-after.md` 의 각 항목 `After` 블록을
   **실제 적용된 코드로 교체**하고, 각 항목 끝 게이트 체크박스를 결과로 채운다.
2. R-06 의 성능이 개선됐으면 그 수치를 `docs/portfolio/portfolio-source.md` 소재 ① 에 추가한다.
3. 규모 표(Before/After 줄 수, 수정 지점 수)를 실측값으로 갱신한다.
4. PR 을 열고(base `dev`) 게이트 10개 결과를 본문에 싣는다.

---

## 참고 문서

| 무엇 | 어디 |
|---|---|
| 리팩터링 근거·위험·게이트 | `docs/portfolio/refactor-plan.md` |
| 코드 전후 | `docs/portfolio/refactor-before-after.md` |
| 포트폴리오 소재 (왜 이 리팩터가 필요한지의 맥락) | `docs/portfolio/portfolio-source.md` |
| 프로파일링 조건 고정 | `docs/guides/profiling.md` |
| 데디 서버 운용 | `docs/guides/dedicated-server.md` |
| 정본 성능 리포트 | `docs/profiling/M6-gpu-breakdown.md` |
| 패턴별 p99 | `docs/profiling/M7-pattern-p99.md` |
| 프로젝트 규칙 (Gitflow·질문 우선) | `CLAUDE.md` |
