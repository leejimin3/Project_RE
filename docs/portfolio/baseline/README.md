# 리팩터링 기준선 (STEP 0)

**캡처일:** 2026-08-29 · **커밋:** `feature/M8-portfolio-hardening` 의 STEP 0 커밋
**목적:** R-01~R-08 리팩터링이 동작을 바꾸지 않았음을 판정할 유일한 근거 (게이트 6·7·8).

기준선 없이 리팩터를 시작하면 회귀를 판정할 수 없다 — #88 이 정확히 그 실패였다.

## 산출물

| 파일 | 무엇 | 어느 게이트 |
|---|---|---|
| `arc-shots-before.txt` | 곡사 8종 × 전 Shape × 스윕 3회의 **생성 샷 전량**(Start/Target/체공/고도/데미지/반경/경과/제어점 3쌍) | 7 |
| `pattern-signature-before/` | 패턴 15종 각각의 페이즈 이름·길이 + 볼리당 스폰 수 | 6 |
| `perf-before.md` | Spiral(0)/ArtilleryStorm(3)/LissajousStorm(6) GT mean·p99 | 8 |
| `dedi-verify-before.txt` | `dedi-verify.ps1 -Clients 2` 전 항목 판정 — **21/21 PASS** | 5 |
| `probe-strings-before/` | 서버·클라 로그의 프로브 문자열 골격(수치 마스킹) | 4 |

## 게이트 7 이 왜 특별한가 — 라이브 로그로는 판정할 수 없다

곡사 8종 중 5종이 착지점을 `ServerTime` 에서 유도한다:

| 패턴 | 시간 의존 항 | 위치 |
|---|---|---|
| `LissajousStorm` | `LissaDeltaDegPerSec * ServerTime` | `REBossCharacter.cpp:698` |
| `BezierVortex` | `Frac(ServerTime / VortexExpandSec)`, `VortexSpinDegPerSec * ServerTime` | `:707` |
| `RoseField` | `RoseFieldSpinDegPerSec * ServerTime` | `:713` |
| `AerialDome` | `Frac(ServerTime / DomeExpandSec)`, `DomeSpinDegPerSec * ServerTime` | `:721` |
| `Spirograph` | `SpiroSpinDegPerSec * ServerTime` | `:727` |

`Origin`/`AimLoc` 도 라이브 위치다. 즉 **리팩터를 하지 않아도 실행마다 좌표가 다르다** —
고정 `CallSeed` 만으로는 "바이트 단위 동일"이 성립하지 않는다.

그래서 입력을 전부 고정한 임시 결정론 덤프를 STEP 0 에서 추가했다
(`re.Debug.DumpArcTargets`, `AREBossCharacter::DebugDumpArcShots`):

- `Origin = (600, 0, 90)` — GameMode 데모 스폰 좌표
- `AimLoc = (0, 300, 90)`
- `CallSeed = 12345`, `SweepIdx = 0..2`
- `ServerTime = 1000` — `GetElapsedSince` 가 `[0,1]` 로 클램프하므로 `Elapsed` 가 항상 0 이 되어
  어긋내기(`P.Elapsed +=`)까지 결정론이 된다

비교 대상은 착지점만이 아니라 **함수의 실제 출력인 `Shots` 전량**이다 —
R-05 분해가 파라미터 결정·착지 지오메트리·샷 성형 어느 단계를 어긋내도 잡힌다.

**이 덤프 코드는 PR 직전 마지막 커밋에서 되돌린다.**

### 재현

```powershell
$Ed = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
& $Ed "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main `
  -game -nullrhi -unattended -nosplash -NoSound `
  -ExecCmds="re.Debug.DumpArcTargets" -abslog="...\arcdump_after.log"
```

```bash
grep -o "\[RE\] ArcDump P=.*" arcdump_after.log > arc-shots-after.txt
diff docs/portfolio/baseline/arc-shots-before.txt arc-shots-after.txt   # 출력 없어야 통과
```

결정론은 캡처 시점에 2회 실행 diff 로 확인했다 — 1,767줄 전량 일치.

## 게이트 6 재현

```bash
scripts/extract-pattern-signature.sh <패턴이름> <run.log>
```

첫 페이즈는 `-ExecCmds` 도착 전이라 `FMath::Rand()` 로 뽑힌 랜덤 패턴이다
(`REGameMode.cpp:267`). 추출기가 고정 패턴의 첫 페이즈부터 잘라 쓰는 이유가 이것이다.
`Angle`/`Elapsed`/볼리 횟수도 시간·위치 함수라 제외한다.

**`Shape=` 열은 전 패턴에서 비교에서 제외한다.** `CurrentArtilleryShape` 는 페이즈 패턴이
`Artillery`(`PhaseRng.RandRange`) 또는 `ArtilleryStorm`(`Spiral` 고정)일 때**만** 대입되고
(`REBossCharacter.cpp:304,310`), 나머지 곡사 6종은 **직전 페이즈가 남긴 값을 그대로
페이로드에 싣는다.** 첫 페이즈가 랜덤이라 그 잔류값이 실행마다 달라진다 — 실측:

| 런 | 첫(랜덤) 페이즈 | 찍힌 Shape |
|---|---|--:|
| baseline p7 BezierVortex | BezierVortex | 0 |
| R-04 p7 BezierVortex | **Artillery** | 4 |
| baseline p12 AerialDome | SuperformulaBloom | 0 |
| R-04 p12 AerialDome | **ArtilleryStorm** | 3 |

그 값은 `Pattern == Artillery` 분기에서만 읽히므로 다른 패턴에서는 **로그 표기일 뿐**이다
(N·Flight 는 세 건 다 동일했다). Shape 6종의 결정론 비교는 게이트 7
(`arc-shots-before.txt`)이 고정 입력으로 전량 덮는다.


## 게이트 4 재현 — 로그 문자열 diff

```bash
scripts/extract-probe-strings.sh <server.log> > after.txt
diff docs/portfolio/baseline/probe-strings-before/server.txt after.txt
```

추출기가 카테고리 접두어(`LogTemp:` 등)와 수치를 지우고 **메시지 본문 골격만** 남긴다 —
R-02 가 바꾸는 것이 카테고리뿐임을 이 diff 가 증명한다.

**프로브 로그(`[Move]`/`[Attack]`/`[Dash]`)만 본다.** `[RE]` 게임루프 로그를 넣으면
diff 가 성립하지 않는다 — 보스 패턴 로테이션 시드가 `FMath::Rand()`(`REGameMode.cpp:267`)
라 실행마다 다른 패턴이 돌고, 플레이어 사망 타이밍에 따라 `[RE] Player died` /
`[RE] EndGame: DEFEAT` 이 있기도 없기도 하다. 프로브는 고정 타이머 시퀀스라 결정론이고,
게이트 4 가 지키려는 계약이 바로 그 문자열이다.

## 쿡 기준선

`BuildCookRun -server -cook -stage -pak -build` → **0 error / 0 warning**
(`.../AutomationTool/Saved/Logs/Log.txt`). R-01 이 이 값을 유지해야 한다.
