# 세션 인계 — M7 보스 패턴 확장 (2026-08-23)

> **이 문서 성격**: 구현 goal 문서가 **아니다.** 세션 상태 인계다 — 지금까지 뭘 했고,
> 리포가 어떤 상태이고, 다음에 뭘 하면 되고, 이번에 알아낸 함정이 뭔지.
>
> `2026-08-19-M7-118-handoff.md` 를 대체한다. 아직 유효한 항목은 6절에 옮겨 담았다.
>
> **`2026-08-20-M7-121-handoff.md` 는 `feature/M7-hud-wbp` 브랜치에만 있다**(PR #135 미머지).
> 거기에 WBP 위젯 이름표가 있으니 #121 을 이어갈 때는 그 브랜치를 봐라. 7절에 요약본이 있다.

---

## 1. 프로젝트 한 줄

UE 5.8 소스빌드(`E:/UnrealEngine-5.8/UnrealEngine-5.8`) 위의 탑다운 탄막 슈터. Mass ECS 로
투사체 50,000발(60fps 상한 실측). 서버 권위 + 데디케이티드 서버 대응. 포트폴리오 목적.

**엔진이 소스빌드다.** `.uproject` 의 `EngineAssociation` 이 GUID(`{51F2F747-...}`)라 Epic Games
Launcher 가 이 프로젝트를 인식하지 못한다. Fab 애셋은 **에디터 내장 Fab 플러그인**으로 받는다.

---

## 2. 이번 세션에서 한 것 — #136 / PR #137 (`b4a86c0`, dev 머지 완료)

**곡사 폭풍(`EBulletPattern::ArtilleryStorm`)** 을 새 보스 페이즈로 추가했다. 곡사탄을 한 발씩
빠르게 쏘아 **단일 나선을 안쪽에서 바깥으로 한 번 훑는다.** 페이즈 = 스윕 1회.

기존 Artillery 는 손대지 않았다 — 마커 보고 피하는 게임성이 그대로 남는다.

### 최종 파라미터 (`Core/REBossCharacter.h`)

```cpp
StormPhaseSec      = 10.f     StormMinRadius     = 150.f
StormFireInterval  = 0.05f    StormMaxRadius     = 1500.f
StormCount         = 4        StormRadiusPerTurn = 200.f   → 6.75바퀴(역산)
StormArms          = 2        StormFlightTime    = 6.f
StormMaxHeight     = 400.f    MarkerGrowSec      = 0.15f   (REArcRenderProcessor.cpp)
Damage / Radius    = 15 / 120 (Artillery 와 공유 — 새 상수 안 만듦)
```

동시 체공 = `Count × Arms / FireInterval × FlightTime` = `4×2/0.05 × 6` = **960발**.

### 설계 결정 세 개 (다음 패턴에도 그대로 적용된다)

**나선 간격을 바퀴 수가 아니라 '한 바퀴당 반경 증가'로 잡았다.**
`SweepTurns` 를 상수로 두면 반경을 키울 때 간격이 같이 벌어져 나선이 통째로 성겨진다.
인과가 반대다 — 간격이 원인이고 바퀴 수가 결과다.

```cpp
StormRadiusPerTurn = 200.f;                                    // 이게 상수
StormSweepTurns    = (MaxRadius - MinRadius) / RadiusPerTurn;  // 역산
```

**단발 사격을 RPC 20/s 로 낸다.** 초당 160발을 그대로 멀티캐스트하면 초당 160회 Reliable RPC 다.
대신 볼리 하나가 "지난 발사 주기 동안 한 발씩 나간" **슬롯**들을 실어 나른다. 슬롯마다 나선 위
자기 위치를 갖고, 비행 경과를 `Interval × (Count-1-Slot)/Count` 만큼 어긋내 스폰한다.
화면 결과는 단발 사격과 같고 네트워크는 볼리 주기 그대로다.

**보스를 바닥 정중앙으로 옮겼다.**

```
보스 스폰   (600,0,90)  → (0,0,90)      REGameMode.cpp
PlayerStart (0,0,120)   → (-600,0,120)  Content/Level/Main.umap
SpawnOrigin (600,0,90)  → (0,0,90)      REActorBulletSpawner.cpp (측정 비교군 — 반드시 동일)
```

보스가 여전히 **플레이어 기준 +X 600** 이라 이격 거리와 상대 배치가 예전과 같다. #54 즉사 방지
근거가 그대로 서고 대쉬(+X 678)·이동(+Y 500) 프로브 지오메트리도 불변이다. 프로브 로그가 정확히
-600 평행이동만 된 것으로 확인됐다(빔 길이 641.3 = 이전 641.4).

### 늘어난 검증 수단

| 이름 | 용도 |
|---|---|
| `re.Debug.BossPattern <idx>` | 패턴 고정. -1=정상 로테이션, 0=Spiral 1=Fan 2=Artillery 3=ArtilleryStorm. **인덱스는 `BeginPhase` 의 `Pool` 배열 순서다**(enum 순서 아님 — Homing 이 풀에서 빠져 있다). 페이즈 진입 시점 조회라 재시작 없이 다음 페이즈부터 반영 |
| `[RE] ArcRenderProbe: live=N marker=N` | 동시 체공 곡사탄 수. `REArcRenderProcessor` 매 30회 실행 1회. 밀도 주장을 검증할 **유일한 관측점** |
| `Boss FireArtillery: Pattern=… N=… Sweep=…` | 볼리당 발수·스윕 진행도 |

---

## 3. ★ 다음 작업 — 패턴 더 추가

### 패턴 하나를 추가할 때 손대는 곳 (이번에 실제로 밟은 순서)

| # | 파일 | 할 일 |
|---|---|---|
| 1 | `Mass/REBulletPattern.h` | `EBulletPattern` **끝에 append**. 중간 삽입 금지 — RPC 페이로드가 uint8 enum 이라 기존 값이 밀리면 와이어 호환이 깨진다 |
| 2 | `Mass/REBulletPatternGenerator.h/.cpp` | 발사/착지점 수학을 **순수 함수**로. 엔진·액터 의존 금지(월드 없이 돌아야 한다). `Settings`(CDO) 조회만 예외 |
| 3 | `Core/REBossCharacter.h` | 상수 블록(`static constexpr`). RPC 인자 추가는 **클라가 유도할 수 없는 값만** |
| 4 | `Core/REBossCharacter.cpp:270` | `Pool[N]` 배열에 추가 + 배열 크기. `RandRange` 는 `UE_ARRAY_COUNT(Pool)-1` 이라 자동 |
| 5 | `Core/REBossCharacter.cpp:307` | `BeginPhase` switch — `PhaseSec` / `FireInterval` / `PhaseName` |
| 6 | `Core/REBossCharacter.cpp:331` | `FireCurrentPattern` 분기 — 곡사 계열이면 `FireArtillery()`, 직선이면 아래로 흘려보낸다 |
| 7 | `Core/REBossCharacter.cpp:133` | `LookForPattern` — 페이즈 외관(`Snow`/`Lava`/`Emis`). 색이 겹치면 페이즈 구분이 안 된다 |
| 8 | `Core/REBossCharacter.cpp:228` | `Multicast_BeginPhaseLook` — 인트로 예고 애님(현재 곡사 계열만 `PlayLeap()`) |

### RPC 경로는 둘뿐이다

```cpp
Multicast_FireDirect(EBulletPattern, Origin, AngleDeg, Count, ServerTime)      // 직선탄
Multicast_FireArtillery(EBulletPattern, Shape, Origin, AimLoc, CallSeed,
                        SweepIdx, ServerTime)                                  // 곡사탄
```

**클라는 페이즈 로직도 `PhaseRng` 도 돌리지 않는다 (#84).** 서버 전용 상태에서 파생되는 값은
전부 페이로드에 실어야 한다. 반대로 상수·페이로드에서 순수하게 파생되는 값은 절대 싣지 마라 —
양쪽이 같은 함수로 같은 답을 낸다.

**RPC 시그니처를 바꿨으면 서버 재빌드 + 재쿡이 필수다**(6절). 안 하면 데디 검증이 낡은 exe 를 돈다.

### 비용 모델 — 실측값

**비용은 체공 탄 수가 아니라 착지율이 쥔다.** 착지 프레임마다 폭발 Niagara 1개 + `TakeDamage` 가
돈다. 직선탄은 착지가 없어 압도적으로 싸다.

이번에 팔 수만 바꿔 잰 값(반경 1500 / 간격 200 / 체공 6초 고정):

| 팔 | 동시 체공 | 착지율 | 프레임 |
|---|---|---|---|
| 1 | 481 | 80/s | 7.57 ms |
| **2 (채택)** | **968** | **160/s** | **9.23 ms** |
| 3 | 1,446 | 240/s | 11.01 ms |

- 게이트 **16.6 ms**. 팔 3개도 안에 들지만 팔이 전부 보스 한 점에서 출발해 나선 개별성이 뭉개져 2로 정했다
- 같은 조건에서 `re.Fx.Explosions 0` 이면 **1.6 ms 가 빠진다** — 확장을 막는 유일한 항목이 폭발이다
- 직선탄은 **50,000발 p99 15.00 ms**(#95). 화면 탄을 싸게 늘리려면 이쪽이다

### 아직 안 쓴 손잡이 (다음 패턴 재료)

| 손잡이 | 현황 |
|---|---|
| **직선탄 병행** | 폭풍 페이즈에 `Multicast_FireDirect` 도 같이 부르면 현재 ini 설정으로 **+4,800발**(48/0.15 × 15s), 착지 비용 0. 이번에 제안했으나 채택 안 됨 |
| **폭발 솎기** | 착지 K발당 1개만. 풀면 착지율을 크게 올릴 수 있다 |
| **팔별 발사 지점 분리** | `FArcBulletSpawnParams::Start` 를 팔 방향으로 밀면 기둥이 갈라져 팔 3개 이상도 나선이 읽힌다. 비용 0 |
| **`EArtilleryShape` 6종** | Ring/Line/Grid/Spiral/PlayerAimed/Random — 일반 Artillery 만 쓴다. 폭풍은 shape 스위치 밖에서 자체 지오메트리를 만든다 |
| **`EBulletPattern::Homing`** | enum 슬롯만 있고 `TriggerBulletPattern` 은 경고 로그 후 early-return 스텁. `REBulletSimProcessor` 가 직선 이동만 한다(`Translation += Velocity * Dt`). 이슈 #67 백로그 |

### 아레나 실측치 (패턴 지오메트리를 짤 때 이걸 먼저 봐라)

```
바닥 StaticMeshActor  (0,0,-10)  extent (2000,2000,50)  → 윗면 Z = 40
NavMeshBoundsVolume   (0,0,0)    extent (2500,2500,100)
RecastNavMesh                    extent 1976
PlayerStart           (-600,0,120)
보스                  (0,0,90)   ← 바닥 정중앙. 반경 2000 까지가 바닥 안이다
곡사 착지 평면 GroundZ = BossLoc.Z + MarkerGroundOffset(-88) = 2   ← 바닥 윗면(40)보다 38uu 아래
탄 메시  /Engine/BasicShapes/Sphere (100uu) × 0.5(직선) / 0.7(곡사)
마커 메시 /Engine/BasicShapes/Plane, MarkerZOffset 55, Z스케일 1.0 (하한선 — 낮추면 인스턴스가 사라진다)
카메라 1500uu · FOV 90 · 1600px → 화면 1px ≈ 1.9uu
```

`Config/DefaultGame.ini` 가 C++ 기본값을 덮는다: `BulletSpeed=200`(헤더 300),
`BulletLifetime=15.0`(헤더 3), `BossFireInterval=0.15`, `BulletsPerShot=48`.
**사거리를 따질 일이 있으면 ini 를 먼저 봐라** — 실사거리 3000uu 다.

### 완료 게이트 (패턴 추가 시 매번)

1. 빌드 `Result: Succeeded` — `Target is up to date` 로 0 액션이면 아무것도 검증 안 된 것이다
2. 헤드리스 회귀 프로브 8줄 (6절)
3. 실 RHI 창 캡처로 **눈으로** 확인 — 로그 성공 신호는 룩을 보증하지 않는다(5절)
4. `ArcRenderProbe` 또는 HUD `PROJECTILES` 로 밀도 주장 대조
5. 프레임 게이트 16.6 ms
6. RPC 시그니처를 건드렸으면 서버 재빌드 + 재쿡 + 데디 21항목
7. 서버·클라 볼리 수와 파생값 시퀀스 일치(네트워크 결정성)

---

## 4. 리포 현재 상태

```
dev:    b4a86c0  Merge pull request #137 (곡사 폭풍)
현재:   feature/M7-boss-patterns (dev 에서 분기, 이 문서만)
남은 원격 브랜치:
  feature/M7-artillery-storm  ccfafd7  머지됨(삭제 금지 규칙)
  feature/M7-hud-wbp          cf30ab9  PR #135 열림 — #121 WBP 디자인 대기

미커밋(항상 이 상태다 — 커밋 금지):
  M Config/DefaultEditor.ini    ⚠ 애셋 뷰어가 덤프한 프리뷰 조명 프로필
  M Project_RE.uproject         ⚠ EngineAssociation GUID 만 dirty
  ?? .ignore / .vscode/ / Project_RE.code-workspace   에디터 로컬 설정
  ?? Content/__External{Actors,Objects}__/ThirdPerson/Maps/   템플릿 잔재
```

`Project_RE.uproject` 는 **에디터를 열 때마다** `EngineAssociation` 이 이 머신 GUID 로 덮인다.
커밋하면 다른 환경에서 엔진을 못 찾는다. 건드릴 일이 생기면 부분 스테이징해라:

```bash
cp Project_RE.uproject /tmp/keep
sed -i 's/"{51F2F747-442F-4E4C-1745-C98EB33B1AD6}"/"5.8"/' Project_RE.uproject
git add Project_RE.uproject
cp /tmp/keep Project_RE.uproject      # 워킹트리는 GUID 로 되돌린다
```

### gitignore 된 애셋 — 새 환경에서 손으로 해야 하는 것

| 팩 | 필요 절차 |
|---|---|
| `Content/Adventure_Pack/` (Sarah, SM_Pistol) | Compatible Skeletons 등록(Persona → Retarget Manager), `M_Hair` 노말맵 지정. 절차는 PR #125 본문 |
| `Content/Stone_Golem/` | 임포트만 (PR #131) |
| `Content/IthrisCemetery/` | 임포트만 — 레벨 참조가 살아난다 (PR #128) |
| `Content/Realistic_Starter_VFX_Pack_Vol2/` | 임포트 → `convert_explosion_fx.py` → `fix_vfx_pack_usage.py` (PR #129) |
| `Content/BlinkDash/` | 임포트만 (대쉬 잔상 VFX) |
| `Content/Characters/Mannequins/` (UE5 템플릿) | 없으면 사격/대쉬 애님만 생략된다 |

안 하면 **조용히** 깨진다 — 보스는 캡슐만, 폭발은 회색 사각형, 총은 사라진 채로 뜬다.
`FObjectFinder` 실패는 크래시 없이 넘어가는 것이 이 리포의 의도된 경로다.

### 스크립트

| 이름 | 용도 |
|---|---|
| `scripts/dedi-verify.ps1` | 데디 21항목 |
| `scripts/profile.ps1` / `profile-stats.ps1` | 프로파일 |
| `scripts/inspect_boss_mesh.py` | 스켈레탈 메시 실측 |
| `scripts/make_beam_material.py` / `make_bullet_material.py` / `make_explosion_fx.py` | 머티리얼·FX 생성기 |
| `scripts/make_hud_wbp.py` | WBP 껍데기 생성(이미 있으면 안 덮는다) |
| `scripts/make_arena_floor.py` / `make_arena_sky.py` / `make_arena_marker.py` / `setup_level_*.py` | 레벨 부트스트랩 |

---

## 5. ★ 이번 세션에서 알아낸 것 (함정)

### 마커가 '생성 단위'를 폭로한다 — 어긋내기는 위치가 시간의 함수인 것에만 먹는다

**이번 세션 최대 발견이다.**

```cpp
탄 위치   = f(Elapsed)   ← 어긋낸 Elapsed 가 그대로 반영 → 부드럽게 이어짐
마커 위치 = A[i].Target  ← Elapsed 와 무관한 상수 → 스폰 즉시 완성 크기로 등장
```

같은 프레임에 N발을 만들면서 `Elapsed` 만 어긋내면 **탄은 단발처럼 보이는데 마커는 N개가 통째로
튀어나온다.** 유저가 "탄은 하나씩 나오는데 착지 표시가 N개씩 생긴다"고 정확히 짚어서 잡혔다.

두 방향으로 고쳤다:
- 이미 어긋나 있는 `Elapsed` 로 마커 **스케일을 램프**(`MarkerGrowSec`). 판정 반경은 안 건드려 데미지 불변
- 생성 단위 자체를 잘게: 초당 발수 유지하고 `Count 12→4`, `Interval 0.15→0.05`

**교훈: 시각 요소를 시간으로 어긋내려면 그 요소의 위치가 실제로 시간의 함수인지 먼저 확인해라.**

### 겹침 계산 — "한 발씩"이 안 보이는 건 간격이 탄 지름보다 작아서다

이웃 탄 간격 = `비행진행도 t × 반경 r × 각스텝(rad)`. 탄 지름 70uu(Sphere 100 × 0.7).

| 위치 | 이웃 간격(당시 설정) | 70uu 안에 겹치는 탄 |
|---|---|---|
| 발사 직후 t≈0 | ~7 uu | 10발 이상 |
| 중반 t=0.5, r=800 | ~21 uu | 3~4발 |
| 착지 직전 t=0.9 | ~57 uu | 거의 분리 |

발사 지점 근처는 구조적으로 뭉친다 — 모든 탄이 한 점에서 출발하기 때문이다. **"덩어리로 보인다"는
신고가 오면 간격을 탄 지름과 비교해봐라.**

### 궤적이 물리가 아니라 정규화 보간이다 — 높이와 시간이 독립

```cpp
// REArcSimProcessor.cpp
t = Elapsed / FlightTime;
Z = Lerp(Start.Z, Target.Z, t) + 4 * MaxHeight * t * (1-t);
```

`MaxHeight` 를 낮춰도 **착지 타이밍은 1도 안 변한다.** 반대로 `FlightTime` 을 늘리면 동시 체공 탄이
정확히 비례해 는다(`체공 = 초당 발수 × FlightTime`). 물리 직관으로 "높이를 낮췄으니 시간을
보정해야지"라고 생각하면 틀린다.

### `re.Debug.ScreenshotFrame` 은 이 패턴에 못 쓴다

카운터가 **직선탄 렌더 프로세서**(`REBulletRenderProcessor`)에 붙어 있다. `re.Debug.BossPattern 3`
으로 곡사 패턴을 고정하면 직선탄 아키타입이 아예 생성되지 않아 그 프로세서가 페이즈에서
**프루닝된다** — 카운터가 0회 진행하고 스크린샷이 영영 안 찍힌다(2런 연속 확인).

→ 시각 검증은 **창 캡처**로 했다(6절).

### 화면 캡처는 `PrintWindow` 로 — `CopyFromScreen` 은 유저 데스크톱을 찍는다

`SetForegroundWindow` + `CopyFromScreen` 은 포커스를 뺏는데, 유저가 그 사이 다른 창을 만지면
**게임이 아니라 유저의 화면이 저장된다**(실제로 한 번 그랬다).

`PrintWindow(hwnd, hdc, 2)` 는 창 내용을 직접 가져와 포커스를 안 건드리고 가려져도 찍힌다.
UE `-game -windowed` 에서 정상 동작 확인.

### 슬로모션으로 관찰 창을 맞출 수 없다

`slomo 0.1` 은 월드 델타를 통째로 늦춰 **페이즈 타이머까지 10배 느려진다.** 60초를 기다려도 게임
시간으론 6초라 원하는 페이즈에 도달하지 못한다. 특정 순간을 보려면 슬로모션이 아니라
**여러 프레임을 연속 캡처해서 고르는 쪽**이 확실하다.

### 파생 constexpr 은 선언 순서에 걸린다

```cpp
static constexpr float StormSweepTurns  = (StormMaxRadius - StormMinRadius) / StormRadiusPerTurn;
static constexpr int32 StormVolleyCount = (int32)(StormPhaseSec / StormFireInterval) + 1;
```

클래스 내 `static constexpr` 초기화에서 다른 멤버를 참조하려면 그 멤버가 **위에** 있어야 한다.

### 에디터가 열려 있으면 빌드가 통째로 거부된다 (재확인)

```
Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11
```

`Get-CimInstance Win32_Process -Filter "Name LIKE '%Unreal%'"` 로 커맨드라인을 봐라.
**`-game` 이 없으면 사람이 연 에디터다 — 죽이지 말고 물어봐라.** 이번에 한 번 물렸다.

### 큰 문서는 bash heredoc 으로 쓰지 마라

PowerShell here-string(`@"` … `"@`) 같은 조각이 섞이면 따옴표 매칭이 깨져 heredoc 이 통째로
실패한다. 문서 파일은 Write 도구로 써라.

---

## 6. 작업 방식 (프로젝트 규칙)

### 빌드

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REEditor Win64 Development \
  -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -DisableAdaptiveUnity -WaitMutex
```

기대: `Result: Succeeded`. `Target is up to date` 로 0 액션이면 **아무것도 검증되지 않은 것이다** —
유니티 섀도잉(C4459)은 실제 컴파일에서만 잡힌다.

### 헤드리스 회귀 프로브

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor.exe" \
  "E:/UnrealProjects/Project_RE/Project_RE.uproject" Main \
  -game -nullrhi -unattended -nosplash -NoSound -log=RE_probe.log
```

기대 출력(전부 나와야 한다):
```
[Attack] fire montage len=0.80
[Attack] hit boss, applied=10.0
[Move] fire lock probe: 이동 요청 (거절 기대)
[Move] rejected: fire lock (0.80s 남음)
[Dash] immediate retry activated=0 (기대 0)
[Dash] dist=678.3 (기대 ~600)
[Dash] re-activate ok=1 (기대 1)
[RE] Probe complete 1/1
```

한 줄 게이트 — 8 이 나와야 한다:
```bash
grep -cE "fire montage len=0.80|hit boss, applied=10.0|fire lock probe|rejected: fire lock|immediate retry activated=0|Dash\] dist=678|re-activate ok=1|Probe complete 1/1" Saved/Logs/RE_probe.log
```

프로브는 `-unattended` 게이트다(`REPlayerController.cpp` 의 `FApp::IsUnattended()`).
빼면 자동종료가 사라진다.

### 데디 검증

RPC 시그니처·서버 로직·`Config/*.ini` 를 건드렸으면 **재빌드 + 재쿡이 필수**다.

```powershell
Build.bat Project_REServer Win64 Development -Project=... -DisableAdaptiveUnity -WaitMutex   # ~75초
RunUAT.bat BuildCookRun -project=... -noP4 -platform=Win64 -server -noclient `
  -serverconfig=Development -cook -stage -pak -skipbuild -utf8output                          # ~80초
scripts\dedi-verify.ps1 -Clients 2                                                            # 21항목
```

`-skipbuild` 를 빼먹지 마라. 클라 전용 변경(렌더·FX·UI·머티리얼)은 재스테이징 불필요 —
`-SkipStaleCheck` 로 넘긴다. **습관적으로 붙이지 마라**(#103: 낡은 exe 로 16항목 PASS 를 받은 적 있다).

### 네트워크 결정성 검증 (dedi-verify 로는 안 잡힌다)

`dedi-verify` 런은 5초짜리라 긴 페이즈가 안 걸린다. 패턴의 서버·클라 일치를 보려면 직접 띄운다:

```powershell
$srv="...\Saved\StagedBuilds\WindowsServer\Project_RE\Binaries\Win64\Project_REServer.exe"
Start-Process $srv -ArgumentList @('-log','-port=7777',
  '-ExecCmds="re.Coop.ExpectedPlayers 1,re.Debug.BossPattern 3"','-abslog="...\server.log"')
# 12초 뒤 클라 접속 (에디터 -game, 127.0.0.1:7777, -abslog)
# 80초 뒤 둘 다 Kill
```

`ExpectedPlayers 1` 이어야 보스가 발사를 시작한다(9 로 두면 인원이 안 차 영영 시작 안 함).
클라가 `-unattended` 가 아니면 프로브를 안 돌려 서버가 자동종료하지 않는다 — 관찰 시간을 통제할 수 있다.

대조:
```bash
diff <(grep -o "Sweep=[0-9]*" server.log) <(grep -o "Sweep=[0-9]*" client1.log)   # 비어야 한다
grep -c "Pattern=4" server.log; grep -c "Pattern=4" client1.log                    # 같아야 한다
```

### 시각 검증 — 창 캡처 (`PrintWindow`)

핵심만: `Start-Process` 로 `-game -windowed` 실행 → 60초쯤 대기(맵 로드 + 페이즈 도달) →
`GetWindowRect` 로 창 크기 → `PrintWindow(hwnd, hdc, 2)` 로 여러 장 연속 캡처 → `Kill`.

- **포커스를 안 뺏는다** — 유저가 그 사이 다른 창을 만져도 게임만 찍힌다
- 여러 장 찍어 고르는 쪽이 셔터를 맞추는 것보다 확실하다
- 확대해서 보려면 `Graphics.DrawImage` + `InterpolationMode.NearestNeighbor` 로 크롭·확대
- `-ExecCmds` 는 **통짜 문자열 + 쉼표 뒤 공백 없음**이라야 한다. 공백에서 쪼개지면 CVar 가 조용히 무시된다
- 탄막을 채우려면 `re.Profiling.KeepFiring 1`, 50k 는 `re.Bullets.Count 50000`
- 캐릭터를 가까이 보려면 `FOV 25`, HUD 를 찍으려면 `re.Debug.ScreenshotUI 1`

### 프로파일

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/profile.ps1 -Bullets 50000 -Label "태그"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/profile-stats.ps1 -Run "Saved\Profiling\<런디렉터리>"
```

기준선 `docs/profiling/M6-gpu-breakdown.md` 부록 A: **50,000발 Frame p99 15.00 ms**, 게이트 **16.6 ms**.

- **측정 전 배경을 정리해라.** 병목이 GT(CPU)라 배경 부하가 p99 를 직격한다
- 하네스는 `re.Fx.Explosions 0` 으로 폭발을 뺀다(#98)
- **인스턴스 수를 반드시 확인해라.** 0발로 조용히 측정되는 함정이 있다(#46)
- 표본 하나로 단정하지 마라

가벼운 대안 — 로그의 프로브 간격으로 프레임 시간을 낸다(프로브는 30프레임당 1회):
```bash
grep "ArcRenderProbe" Saved/Logs/X.log \
 | sed -E 's/^\[[0-9.]+-([0-9]+)\.([0-9]+)\.([0-9]+):([0-9]+)\].*live=([0-9]+).*/\1 \2 \3 \4 \5/' \
 | awk '{ms=($1*3600+$2*60+$3)*1000+$4; if($5>=880){if(prev>0){d=ms-prev; if(d>0&&d<3000){s+=d;n++}}} prev=ms}
        END{printf "%.2f ms (%.0f fps)\n",s/n/30,30000/(s/n)}'
```
**p99 가 아니라 평균이다** — 주장할 때 그렇게 밝혀라.

### Git / PR

Gitflow. `dev` 에서 `feature/M7-...` 분기 → PR base `dev`.
이슈/PR 메타 6개 필드 전부 채운다(`--label --milestone --assignee --project`, Reviewer 생략).
패턴 이슈 라벨 선례: `enhancement` + `mass-entity` + `C++` (+RPC 건드리면 `networking`).

**`Closes #N` 은 dev 머지로 발동하지 않는다**(기본 브랜치 전용). 머지 후 이슈를 수동으로 닫아라.

머지는 `gh pr merge <N> --merge`, **브랜치 삭제 금지**.

### 문서 작성 규칙

이슈/PR 본문 쓰기 전 **관련 소스를 전부 읽고 실제 상태와 대조**한다. 추측을 원인으로 적지 마라.

### 옛 세션에서 여전히 유효한 항목

- **애디티브 애님은 애디티브 슬롯 없이는 화면에 안 나온다.** `Montage_Play` 가 정상 길이를 돌려주고
  `Montage_IsPlaying` 이 true 이고 데디 검증도 PASS 로 찍힌다 — **화면으로만 잡힌다.**
  애님이 안 보이면 `additive_anim_type` 을 먼저 찍어라
- **파이썬 쓰기가 조용히 무시되는 프로퍼티가 있다.** `UAnimMontage.slot_anim_tracks` 는 예외도 없이
  no-op. **애셋을 고치는 스크립트에는 저장 후 재읽기 검증을 반드시 넣어라** (이번 PlayerStart 이동에도 넣었다)
- **UE 5.8 파이썬에서 막힌 것**: `SkeletalMesh.imported_bounds`(→`get_bounds()`),
  `Skeleton.sockets`(→스폰 후 `get_all_socket_names()`), `AnimMontage.slot_anim_tracks`(우회 없음),
  `WidgetBlueprint.widget_tree`(우회 없음), `NiagaraSystem.EmitterHandles`(protected)
- **엔진에 빔 Niagara "시스템"이 없다** — 모듈과 이미터 템플릿뿐. 빔은 실린더 메시 + 자체 머티리얼
- **밝은 바닥 위에서 Additive 는 흰색으로 포화된다.** Translucent + Strength 1 근처
- **`BlockAll` 이 우클릭 이동을 막는다.** 구조물엔 `InvisibleWall` 프로파일
- **애님 재생 판정은 본 좌표로.** `-nullrhi` 에서는 본이 갱신되지 않는다 — 실 RHI 필수
- **Fab 애셋 임포트가 `.uproject` 에 플러그인을 추가해 에디터가 안 열릴 수 있다.** 임포트할 때마다 `git diff Project_RE.uproject`
- **Cascade 팩 머티리얼을 Niagara 에 쓰면 조용히 회색 사각형**(`bUsedWithNiagaraSprites`). `GetMaterial()` 은 정상값을 돌려준다
- **ISM 머티리얼은 `bUsedWithInstancedStaticMeshes` 없으면 조용히 기본 머티리얼로 폴백**
- **`delete_asset` 은 인메모리다** — `rm -f` 가 확실하다
- **`-ExecutePythonScript` 인자는 마지막 토큰에 따옴표가 붙어 온다.** 파서에 `.strip` 을 넣어라
- **모든 생성기는 `save_asset(FULL, False)`** — 기본값이면 저장이 조용히 건너뛰어진다
- **메시가 캡슐보다 커도 판정은 안 깨진다.** `CharacterMesh` 프로파일이 `ECC_Pawn` 을 무시한다
- **코드와 검증기를 같은 커밋에서 함께 바꾸지 마라** — `dedi-verify.ps1` 이 로그 문자열로 판정한다

---

## 7. 아직 열려 있는 것

| 항목 | 상태 |
|---|---|
| **패턴 추가** | **다음 작업.** 3절 |
| `#121` UI 디자인 | PR #135 열림. 배관 끝, **WBP 트리 배치는 사람 몫**(파이썬으로 못 짠다). `Content/UI/WBP_PlayerHud` 에 `HealthBar`/`HealthText`/`DashBar`/`DashText`/`BulletText`/`BulletBox`, `Content/UI/WBP_Result` 에 `ResultText`. **전부 아니면 전무** — 하나라도 만들면 C++ 폴백이 통째로 꺼진다. 상세는 `feature/M7-hud-wbp` 의 `2026-08-20-M7-121-handoff.md` |
| `#99` M6 최종 영상 | 선행(#97/#98) 닫혔으나 M7 이후로 미룸 |
| `#67` M5 보스 Homing | 의도적 백로그. enum 슬롯 + early-return 스텁만 존재 |
| 폭풍 마커 오버드로 | 간격 200 < 마커 지름 240 이라 인접 띠가 겹쳐 회피 통로가 없다. **의도된 선택**(밀도 우선) |
| 폭풍 팔 발사점 | 팔 2개가 보스 한 점에서 출발해 발사 근처가 뭉친다. `Start` 를 팔 방향으로 밀면 풀린다(비용 0, 미적용) |
| 빔/섬광이 애님을 안 따라감 | 발사 **시점**의 머즐 위치에 고정. 실노출 0.06초라 화면상 문제없음 |
| 골렘 몸통 관통 | 메시가 캡슐보다 1.9배 |
| 은하수 띠 | 마스크는 작동하나 화면에서 띠로 안 읽힌다 |
| 새 환경 셋업 문서 | 4절 표를 M7 종료 시 정식 문서로 옮겨야 한다 |
| 카툰 렌더링 | M7 제외 |

M7 마일스톤: **19 closed / 2 open**.

---

## 8. 새 세션 첫 발언 권장

> `docs/superpowers/handoffs/2026-08-23-M7-boss-patterns-handoff.md` 읽고 보스 패턴 추가하자.
> 어떤 패턴을 넣을지는 같이 정하고 싶다 — 3절의 손잡이 표부터 보자.
