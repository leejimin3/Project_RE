# 프로파일링 측정 절차 (M3)

측정이 매번 달라지면 증거가 안 된다. 아래 조건을 고정한다.

## 실행

```powershell
scripts/profile.ps1 -Bullets 1000          # 기본 720프레임 캡처
scripts/profile.ps1 -Bullets 5000 -Frames 720
```

산출물: `Saved/Profiling/RE_<탄환수>_<타임스탬프>/`
- `trace.utrace` — Unreal Insights 트레이스
- `frames.csv` — 엔진 CSV 프로파일러의 프레임별 시간

## 고정 조건

| 항목 | 값 | 이유 |
|---|---|---|
| 렌더 | **실 RHI**, `-windowed 1280x720` | `-nullrhi` 는 렌더 비용이 사라져 GT 병목을 못 본다. 과거 ISM Bounds=0 오진 전례 있음 |
| 워밍업 | **앞 120프레임 버린다** | 레벨 로드 / 셰이더 컴파일 스파이크 구간 |
| 측정 구간 | **600프레임** (캡처 총 720 = 120 + 600) | steady-state 만 읽는다 |
| 바이너리 | `UnrealEditor.exe -game` | 패키징(cook)은 수십 분 + 코드 수정마다 재쿡. **절대치가 아니라 상대 비교용**이다 — 에디터 오버헤드가 섞여 있음을 알고 읽어라 |
| `-unattended` | **금지** | `FApp::IsUnattended()` 가 켜지면 `REPlayerController` 의 headless 프로브가 발동해 약 4초 뒤 `RequestExit` 로 게임을 스스로 끈다 → 캡처 프레임을 못 채운다 |

워밍업 컷은 스크립트가 하지 않는다. `frames.csv` 원본을 그대로 남기고, **읽을 때 앞 120행을 버린다.** (하네스는 수집만 하고 해석하지 않는다 — 해석은 #46.)

## Insights 로 읽는 법

1. `E:\UE_5.8\Engine\Binaries\Win64\UnrealInsights.exe` 실행
2. `trace.utrace` 열기 (Open Trace File)
3. Timing Insights 뷰 → 필터에 이벤트 이름 입력:

| 이벤트 | 프로세서 | 잡히는 스레드 |
|---|---|---|
| `RE_BulletSim` | 이동 / 수명 | 워커 스레드 |
| `RE_BulletRender` | ISM 동기화 | **게임 스레드 고정** (`bRequiresGameThreadExecution = true`) |
| `RE_BulletHit` | 피격 판정 | **게임 스레드 고정** |

Render / Hit 가 GT 에 고정돼 있으므로 탄환 수를 올렸을 때 GT 병목의 최우선 용의자는 `RE_BulletRender` 다. Sim 이 워커 스레드로 빠지는 그림이 "왜 Mass 를 썼나"의 증거다.

세 스코프는 **트레이스에만** 있다. `frames.csv` 에는 안 나온다 (CSV 프로파일러는 별개 stat 집합을 찍는다).

## CSV 컬럼

`frames.csv` 의 프레임 시간 컬럼 (1000발 실행에서 헤더로 **실측**한 이름, 단위 ms):

- `FrameTime`
- `GameThreadTime`
- `RenderThreadTime`
- `GPUTime`
- `RHIThreadTime`
- `MaxFrameTime`
- `GameThreadTime_CriticalPath` / `RenderThreadTime_CriticalPath`

행 수 = 헤더 1 + 캡처 프레임 수(720).

## 알려진 것

- CSV 는 프로젝트 `Saved/` 가 아니라 **엔진 유저 디렉터리**(`%LOCALAPPDATA%/UnrealEngine/5.8/Saved/Profiling/CSV`)에 떨어진다. 스크립트가 거기서 집어와 run 폴더로 옮긴다. 캡처 **시작** 시점에 0바이트 파일이 먼저 생기므로 "파일 존재" 만으로는 완료 신호가 안 된다 (크기까지 봐야 한다).
- `re.Bullets.Count` CVar 는 #43 이 만든다. 머지 전에는 `-ExecCmds` 가 경고만 찍고 무시된다 (하네스 동작 자체엔 문제 없다).
- 실제 수치 수집·리포트는 #46 의 몫이다.
