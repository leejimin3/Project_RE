# 프로파일링 측정 절차 (M3)

측정이 매번 달라지면 증거가 안 된다. 아래 조건을 고정한다.

## 실행

```powershell
scripts/profile.ps1 -Bullets 1000          # 기본 720프레임 캡처 (Mass 경로)
scripts/profile.ps1 -Bullets 5000 -Frames 720
scripts/profile.ps1 -Bullets 5000 -Actor   # Actor 베이스라인 (#45 비교군)

# 엔진 CVar를 주입해 A/B 소거 측정 (#50). -Label 은 run 디렉터리 이름에 들어간다.
scripts/profile.ps1 -Bullets 5000 -Label noshadow -ExtraExec "r.ShadowQuality 0"
```

산출물: `Saved/Profiling/RE_<경로>_<탄환수>[_<라벨>]_<타임스탬프>/`
- `trace.utrace` — Unreal Insights 트레이스
- `frames.csv` — 엔진 CSV 프로파일러의 프레임별 시간 (프로세서 3분해 컬럼 포함)
- `run.log` — 실제 유지 탄환 수 대조용 (`RenderProbe` / `ActorBulletProbe`)

## 통계 뽑기

```powershell
scripts/profile-stats.ps1 -RunDir (Get-ChildItem Saved\Profiling\RE_Mass_5000_* -Directory).FullName
```

run 디렉터리들을 받아 컬럼별 mean/p99 를 마크다운 표로 낸다. 총량 4개(Frame/GT/RT/GPU)에
더해 패스별 렌더스레드 시간(`RenderBasePass` / `RenderShadows` / `RenderPostProcessing` /
`RenderTranslucency` / `UpdateGPUScene` / `UpdatePrimitiveInstances`)과
`GPUSceneInstanceCount` · `RHI/DrawCalls` 를 함께 뽑는다.

**손으로 `frames.csv` 를 파싱한다면 두 가지를 알아야 한다** (스크립트는 둘 다 처리한다):

- **말미 2행을 잘라라.** `[HasHeaderRowAtEnd]` 때문에 데이터 뒤에 헤더 1행 + 메타 1행이 더 붙는다. 안 자르면 숫자 컬럼에 문자열이 섞여 통계가 조용히 오염된다.
- **컬럼명이 중복될 수 있다.** 실측으로 그림자를 끈 런의 `ShadowCacheUsageMB` 가 2회 나왔다. `ConvertFrom-Csv` 는 중복 헤더를 만나면 던진다.

**패스별 컬럼은 렌더스레드 CPU 시간이지 GPU 시간이 아니다.** 두 축은 크게 어긋날 수 있다 —
실측에서 `RenderShadows` 는 0.17 ms인데 그림자를 끄면 GPU가 2.04 ms 줄었다(#50 리포트 §4).

## `-ExtraExec` 로 CVar를 주입했으면 적용됐는지 확인하라

커맨드라인에 들어간 것과 런타임에 먹은 것은 다르다. 확인 없이 "껐는데 차이가 없다"를 얻으면
**그 기능이 원인이 아닌 것인지 노브가 안 먹은 것인지 구분할 수 없다.** 결론을 통째로 뒤집는 모호함이다.

```powershell
Select-String -Path .\Saved\Profiling\<run>\run.log -Pattern 'r\.BloomQuality = '
```

부팅 시 `LogConfig: Set CVar [[r.BloomQuality:5]]` 로 설정 기본값이 잡히고, `-ExecCmds` 가 그 뒤에
`r.BloomQuality = "0"` 으로 덮는 줄이 보여야 한다. 로그 줄에 타임스탬프 접두사가 붙으므로
정규식에 `^` 앵커를 쓰면 안 걸린다.

`-Actor`는 Mass boss(기본 480)를 죽이고 액터 스포너만 켠다. 두 경로 모두 `re.Profiling.KeepFiring 1`을 주입한다(아래).

## 고정 조건

| 항목 | 값 | 이유 |
|---|---|---|
| 렌더 | **실 RHI**, `-windowed 1280x720` | `-nullrhi` 는 렌더 비용이 사라져 GT 병목을 못 본다. 과거 ISM Bounds=0 오진 전례 있음 |
| 트레이스 채널 | `cpu,frame,counters,gpu` | `gpu` 는 Insights GPU 트랙용(#50). 트레이스는 어차피 뜨므로 추가 비용 사실상 0 |
| 캡처 시작 | **탄환 채움 완료 시점** (`-csvStartOnEvent=REBulletsFilled`) | 부팅 시점에 시작하면 레벨 로드·셰이더 컴파일·보스 발사 전 구간이 창을 먹는다. 실측으로 17.9초 창 중 **13.3초가 빈 씬**이었다 (#88) |
| 측정 구간 | **캡처 720프레임 전량** | 시작이 이미 정상상태라 버릴 구간이 없다 |
| 바이너리 | `UnrealEditor.exe -game` | 패키징(cook)은 수십 분 + 코드 수정마다 재쿡. **절대치가 아니라 상대 비교용**이다 — 에디터 오버헤드가 섞여 있음을 알고 읽어라 |
| `-unattended` | **금지** | `FApp::IsUnattended()` 가 켜지면 `REPlayerController` 의 headless 프로브가 발동해 약 4초 뒤 `RequestExit` 로 게임을 스스로 끈다 → 캡처 프레임을 못 채운다 |
| `re.Profiling.KeepFiring` | **1 (필수)** | 자동사격이 보스를 ~2.5s에 죽이거나(VICTORY) 정지 플레이어가 탄막에 죽으면(DEFEAT) `EndGame`이 보스 `DemoFireTimer`를 꺼 **Mass 탄환이 0발**로 측정이 무효화된다. 이 CVar가 게임오버를 무력화 + 보스를 무적으로 해 탄막을 계속 유지시킨다. 스크립트가 자동 주입 |
| `re.Cheat.PlayerInvincible` | **1 (필수)** | #86부터 사망한 플레이어는 `GatherHitTargets`의 판정 대상에서 빠지고, 대상이 비면 두 히트 프로세서가 청크 순회 전에 조기 반환한다 — 즉 DEFEAT 이후로는 탄이 플레이어 근처에서 소멸하는 일 자체가 없어져 캡처 후반부가 "타겟 없음" 원가만 재게 된다. `profile.ps1`이 이 CVar를 같이 주입해 플레이어가 죽지 않게 고정한다 |

**워밍업 컷은 이제 필요 없다** (#88 이전에는 앞 120행을 버리라고 했다). 캡처가 보스의 라이브 탄환이 목표치에 도달한
뒤에 시작하므로 첫 행부터 정상상태다. `frames.csv` 원본을 그대로 읽어라. (하네스는 여전히 수집만 하고 해석하지 않는다 — 해석은 #46.)

캡처 시작 신호는 `CSV_EVENT_GLOBAL(TEXT("REBulletsFilled"))` 이고, **두 경로가 각각 쏜다**:

| 경로 | 발화 지점 | 채움 완료 조건 |
|---|---|---|
| Mass | `AREBossCharacter::ResolveSpiralCount()` | 피드포워드 채움 구간을 벗어나는 샷 |
| Actor (`-Actor`) | `UREActorBulletSpawner::Fire()` | `Lifetime / 0.1s` 번째 샷 |

한 실행에서 한 경로만 돈다 — `profile.ps1` 이 반대쪽 CVar를 0으로 죽이고, 죽은 쪽은 발사 함수가 즉시 return 한다.
**한쪽에만 신호를 넣으면 다른 경로의 캡처는 영원히 시작되지 않는다** (`-Actor` 가 그렇게 한 번 죽었다).
이벤트 이름을 바꾸면 `profile.ps1` 의 `-csvStartOnEvent` 와 두 발화 지점을 **전부** 같이 바꿔야 한다 — 엔진은 **대소문자 무시 완전일치**로만 건다.

## Insights 로 읽는 법

1. `UnrealInsights.exe` 실행 — **소스 엔진에는 기본으로 없다.** 별도 타겟이라 한 번 빌드해야 한다:

   ```powershell
   & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat" UnrealInsights Win64 Development
   ```

   빌드 후 `E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealInsights.exe`
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

프로세서 3분해 컬럼(`CSV_SCOPED_TIMING_STAT`, 프레임별 ms) — 컬럼 이름이 스레드 배치를 드러낸다:
- `REBullet/AllWorkers/BulletSim` — 이동/수명 (워커 스레드)
- `REBullet/GameThread/BulletRender` — ISM 동기화 (게임 스레드 고정)
- `REBullet/GameThread/BulletHit` — 피격 판정 (게임 스레드 고정)

행 수 = 헤더 1 + 캡처 프레임 수(720).

## 알려진 것

- **CSV 가 떨어지는 곳은 고정이 아니다.** 엔진 빌드 종류와 캡처 시작 시점에 따라 세 군데로 갈린다:
  `%LOCALAPPDATA%/UnrealEngine/5.8/Saved/Profiling/CSV` (런처 바이너리 빌드), 엔진 트리의 `Engine/Saved/Profiling/CSV`
  (소스 빌드 + 부팅 시점 캡처), 프로젝트 `Saved/Profiling/CSV` (프로젝트 경로가 잡힌 뒤 시작한 캡처).
  `profile.ps1` 은 셋 다 훑는다. 한 군데만 보면 **CSV 가 정상 기록됐는데도 "안 나옴" 으로 오진한다** — #88 이 정확히 그 사고였고,
  M4의 런처→소스 빌드 교체가 방아쇠였다. 캡처 **시작** 시점에 0바이트 파일이 먼저 생기므로 "파일 존재" 만으로는 완료 신호가 안 된다 (크기까지 봐야 한다).
- **적분 루프게인은 무차원이다** (`re.Bullets.SpawnKi`, 기본 3.6). 코드가 N²(N = 수명/발사간격)으로 나눈다.
  이 정규화 전에는 `BulletLifetime` 이나 `BossFireInterval` 을 바꾸는 것만으로 루프가 조용히 진동했다 —
  실제로 3.0/0.1 에서 튜닝한 값이 15.0/0.15 로 바뀌며 루프게인이 3.6→40 이 되어 라이브 카운트가 388~1802 리밋사이클에 빠졌다 (#88).
  지금은 설정을 바꿔도 안정성이 불변이다. 실측: 1000 목표에서 평균 998, 진폭 ±2.2%.
- `re.Bullets.Count` CVar 는 #43 이 만든다. 머지 전에는 `-ExecCmds` 가 경고만 찍고 무시된다 (하네스 동작 자체엔 문제 없다).
- 실제 수치 수집·리포트는 #46 의 몫이다.
