# M3 프로파일링 하네스 설계 (#44)

- 이슈: #44 `[M3] 프로파일링 하네스 — Insights CPU 스코프 + 캡처 스크립트`
- 브랜치: `feature/M3-profiling-harness` (worktree `E:/UnrealProjects/Project_RE-profiling`, dev 분기)
- 날짜: 2026-07-15

## 목표

**"실행 한 번 → 수치 파일 하나"** 가 나오는 재현 가능한 측정 하네스.

M3 산출물은 "프로파일 수치 스크린샷 — 면접 '왜 Mass 썼나' 질문의 증거"다. 증거를 만들려면 측정 절차가 먼저 있어야 한다. 지금 측정 인프라는 0이다.

**범위 밖**: 실제 수치 수집·리포트(#46), 최적화, CI 성능 게이트, 커스텀 프로파일러 UI.

## 현재 상태

프로세서 3개 중 계측 스코프가 있는 것은 없다.

| 프로세서 | 파일 | 스레드 |
|---|---|---|
| Sim (이동/수명) | `Mass/REBulletSimProcessor.cpp` `Execute` | 워커 |
| Render (ISM 동기화) | `Mass/REBulletRenderProcessor.cpp` `Execute` | 게임 스레드 고정 (`bRequiresGameThreadExecution = true`) |
| Hit (피격 판정) | `Mass/REBulletHitProcessor.cpp` `Execute` | 게임 스레드 고정 (`bRequiresGameThreadExecution = true`) |

Render는 GT 고정이라 5000발에서 GT 병목의 최우선 용의자다. 계측 없이는 추측일 뿐이다.

## 설계

### 1. 프로세서별 CPU 스코프

각 `Execute` 본문 첫 줄에 엔진 내장 매크로 1줄씩:

```cpp
void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletSim);
    ...
}
```

- `REBulletSimProcessor.cpp` → `RE_BulletSim`
- `REBulletRenderProcessor.cpp` → `RE_BulletRender`
- `REBulletHitProcessor.cpp` → `RE_BulletHit`

매크로가 기존 include 체인으로 안 들어오면 각 파일에 `#include "ProfilingDebugging/CpuProfilerTrace.h"` 추가. **빌드로 확인한다 — 추측하지 않는다.**

커스텀 프로파일러/타이머 클래스는 만들지 않는다.

Sim은 워커 스레드, Render/Hit은 GT에 잡히므로 Insights 타임라인에서 스레드 분리가 그대로 보인다. 이것이 "왜 Mass 썼나"의 증거다.

### 2. 캡처 스크립트 `scripts/profile.ps1`

```
scripts/profile.ps1 -Bullets 1000 [-Frames 720]
```

동작:

1. `Saved/Profiling/RE_<Bullets>_<yyyyMMdd-HHmmss>/` 생성
2. 아래 커맨드라인으로 에디터를 `-PassThru` 로 띄운다:

```
UnrealEditor.exe <uproject> /Game/Level/Main
  -game -windowed -ResX=1280 -ResY=720
  -trace=cpu,frame,counters
  -statnamedevents
  -tracefile=<run>/trace.utrace
  -csvCaptureFrames=<Frames>
  -ExecCmds="re.Bullets.Count <Bullets>"
  -unattended -nosplash
```

3. `Saved/Profiling/CSV/` 에 새 `.csv` 가 생길 때까지 폴링 (하드 타임아웃 5분)
4. csv 감지 → 3초 대기(flush 여유) → `Stop-Process`
5. csv 를 run 폴더로 `frames.csv` 로 이동
6. run 폴더 경로 출력

**설계 결정과 근거:**

| 결정 | 선택 | 근거 |
|---|---|---|
| 측정 바이너리 | `UnrealEditor.exe -game -windowed` | 패키징(cook)은 수십 분 + 코드 수정마다 재쿡. M3 목적은 프로세서 3개 **상대 비교 + 탄환 수 스케일링**이라 절대치는 안 중요. 에디터 오버헤드가 섞이는 것은 알려진 한계이며 문서에 기록한다 |
| 종료 방식 | csv 파일 감지 후 `Stop-Process` | `-csvCaptureFrames=N` 은 N프레임 채우면 csv를 쓰지만 **게임은 계속 돈다**. 자동 종료 엔진 플래그는 확인된 바 없다. 고정 시간 kill은 5000발 저FPS에서 프레임을 못 채워 측정이 무효가 된다. kill 해도 `.utrace` 는 스트리밍 기록이라 유효하고 `.csv` 는 이미 flush 된 뒤다 |
| 워밍업 처리 | 문서 규칙만 (원본 csv 보존) | 캡처 지연은 게임 코드에 프레임 카운터가 필요 → `Core/` 는 #43 소유. csv 트리밍 코드는 이슈가 금지("프레임 시간 파싱 코드를 직접 쓰지 않는다"). 하네스는 **수집만** 하고 해석은 #46이 한다 |
| 산출물 경로 | run 폴더 1개에 모음 | 기본값이면 `.utrace` 는 엔진 Trace Store, `.csv` 는 `Saved/Profiling/CSV/` 로 흩어진다. 실행 1회 = 폴더 1개여야 #46이 증거를 모을 수 있다 |
| `Config/DefaultEngine.ini` | **건드리지 않음** | 트레이스 채널은 커맨드라인 `-trace=` 로 충분 |

**만들지 않는 것**: CSV 파싱, 커스텀 타이머, Editor/Packaged 타깃 스위치, ini 수정.

### 3. 측정 조건 문서 `docs/guides/profiling.md`

측정이 매번 달라지면 증거가 안 된다. 문서에 못 박는다:

- **실 RHI + `-windowed` 1280x720 고정**
  - `-nullrhi` 금지: 렌더 비용이 사라져 GT 병목을 못 본다. 과거 ISM Bounds=0 오진 전례 있음
- **워밍업 앞 120프레임 버림** (레벨 로드/셰이더 컴파일 스파이크 구간)
- **측정 구간 600프레임** (캡처 총 720 = 120 + 600)
- 에디터 바이너리 사용에 따른 오버헤드 — 절대치가 아닌 **상대 비교용**임을 명시
- Insights 여는 법 + `RE_BulletSim` / `RE_BulletRender` / `RE_BulletHit` 3개 이벤트 읽는 법

## 의존성

`re.Bullets.Count` CVar 는 **#43이 만드는 중**이라 아직 없다. UE는 모르는 CVar 에 경고만 찍고 넘어가므로 하네스 자체(트레이스/CSV 산출 여부) 검증은 지금 가능하다. #43 머지되면 `-ExecCmds` 가 자동으로 동작한다. **기다리지 않는다.**

## 파일 소유권 (병렬 작업)

이 이슈가 건드리는 파일 **전부**:

- `Source/Project_RE/Mass/REBulletSimProcessor.cpp` — `Execute` 첫 줄 매크로 (+ 필요 시 include)
- `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` — 동일
- `Source/Project_RE/Mass/REBulletHitProcessor.cpp` — 동일
- `scripts/profile.ps1` (신규)
- `docs/guides/profiling.md` (신규)
- `docs/superpowers/specs/2026-07-15-profiling-harness-design.md` (이 문서)

**절대 건드리지 않음**: `Core/REGameMode.*`, `Core/REBossCharacter.*`, `PatternGenerator` (#43 소유) / `Baseline/` (#45 소유).

## 완료 조건

- [ ] 3개 프로세서에 `TRACE_CPUPROFILER_EVENT_SCOPE` 삽입 → `Build.bat Project_REEditor Win64 Development` 통과
- [ ] `scripts/profile.ps1 -Bullets 1000` 실행 → run 폴더에 `trace.utrace` + `frames.csv` 둘 다 생성
- [ ] `frames.csv` 헤더에 GameThread / RenderThread ms 컬럼 존재
- [ ] Insights 에서 `RE_BulletSim` / `RE_BulletRender` / `RE_BulletHit` 3개 이벤트가 각각 보이고 ms 가 읽힘 (스크린샷 — Insights 는 GUI 앱이므로 사용자가 직접 캡처)
- [ ] 측정 조건(해상도/워밍업/구간 길이/`-nullrhi` 금지 사유)이 `docs/guides/profiling.md` 에 기록됨
