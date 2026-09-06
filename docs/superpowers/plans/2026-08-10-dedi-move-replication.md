# 데디 플레이어 이동 리플리케이션 보정 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 데디에서 원격 클라 화면 기준으로 클릭 이동이 워프/러버밴딩 없이 보이게 하고, 발사 시 커서 방향 회전이 내 화면에서도 보이게 한다.

**Architecture:** 구조는 그대로 둔다 — 서버 패스팔로잉 권위 유지, 클라 예측 도입 없음. 오너 클라 보정이 10Hz 하드 텔레포트라는 것이 원인이므로 **보정 스로틀만 해제해 점프를 잘게 쪼갠다.** 회전은 오너 클라에 복제되지 않는 별 경로라 클라가 로컬로 같은 Yaw를 적용한다. 근거 전문: `docs/superpowers/specs/2026-08-10-dedi-move-replication-design.md`.

**Tech Stack:** UE5.8 C++, `UCharacterMovementComponent` 네트워킹 파라미터, `p.NetShowCorrections` 내장 계측.

## Global Constraints

- **테스트 하네스 없음** — 이 프로젝트는 in-engine 유닛 테스트를 안 쓴다. 검증은 ① `Build.bat Project_REEditor` 게이트, ② 데디 2프로세스 실행 + `p.NetShowCorrections` 로그 수치, ③ 클라 창 육안 확인이다.
- **빌드는 직렬로만.** E드라이브 여유 46GB뿐이라 병렬 빌드 금지. 빌드·실행은 코디네이터가 혼자 순서대로 돌린다.
- **`-Project` 경로는 이 워크트리다.** `C:\Users\leeji\orca\workspaces\Project_RE\M4-move-repl\Project_RE.uproject`. 본체 `E:\UnrealProjects\Project_RE\Project_RE.uproject`를 주면 캐시 때문에 10초에 "성공"하고 **이 변경은 검증되지 않는다.**
- **`-unattended` 금지.** `AREPlayerController::BeginPlay`가 `FApp::IsUnattended()`에서 헤드리스 프로브(자기이동/발사/대쉬)를 발동하고 마지막에 `RequestExit`까지 부른다 — 수동 클릭 측정이 오염되고 프로세스가 죽는다.
- **Server 타겟 불필요.** 이 검증은 에디터 바이너리로 한다(`UnrealEditor.exe ... -server`). 쿡/스테이징도 불필요 — `docs/guides/dedicated-server.md`의 쿡 절차는 배포용이다.
- **Gitflow** — 브랜치 `leejimin3/M4-move-repl` (이미 분기됨). dev로 PR.
- **커밋 분리** — 문서 1회(본 문서 + 스펙), 구현 1회.

## 파일 구조

| 파일 | 책임 | 변경 |
|------|------|------|
| `Source/Project_RE/Core/RECharacterBase.cpp` | 생성자에서 보정 스로틀 해제 | 수정 (Task 1) |
| `Source/Project_RE/Core/REPlayerController.cpp` | `OnFire`에서 클라 로컬 회전 적용 | 수정 (Task 1) |
| `docs/superpowers/specs/2026-08-10-dedi-move-replication-design.md` | 실측치 기록 | 수정 (Task 2) |

---

### Task 1: 보정 스로틀 해제 + 클라 로컬 발사 회전

설계 결정 1·2를 넣는다. 총 3줄(+주석). 헤더 변경 없음.

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: `UCharacterMovementComponent::NetworkMinTimeBetweenClientAdjustments`, `...LargeCorrection` (엔진 public UPROPERTY, `CharacterMovementComponent.h:860/868`), `AActor::SetActorRotation`.
- Produces: 없음 (내부 동작 변경).

- [ ] **Step 1: `RECharacterBase.cpp` — 보정 스로틀 0**

생성자의 회전 설정 블록 바로 아래(`GetCharacterMovement()->RotationRate = ...;` 다음, SpringArm 블록 위)에 추가:

```cpp
	// 데디 보정 스로틀 해제 (#72) — 클릭 이동은 입력 예측형이 아니라 클라 예측(Accel=0=브레이크)이
	// 서버 패스팔로잉과 매 프레임 어긋난다. 기본 0.10s 스로틀이면 100ms마다 ~30uu가 밀린 뒤
	// 하드 텔레포트로 보정된다(오너 클라는 SmoothCorrection 대상이 아님) — 캡슐에 붙은 카메라까지 튄다.
	// 매 무브 보정하면 점프량이 1프레임 이동량(~10uu)으로 줄어든다.
	// 근거: docs/superpowers/specs/2026-08-10-dedi-move-replication-design.md
	// ponytail: 플레이어 1명 전제(보정 RPC 1개/무브). 다인전이면 되돌리고 이동목표 복제+클라 예측으로 가라.
	GetCharacterMovement()->NetworkMinTimeBetweenClientAdjustments = 0.f;
	GetCharacterMovement()->NetworkMinTimeBetweenClientAdjustmentsLargeCorrection = 0.f;
```

**둘 다 0이어야 한다.** 엔진이 `FMath::Max(LargeCorrection, Adjustments)`를 쓰므로(`CMC:11090`) 한쪽만 0이면 0.05s 스로틀이 남는다.

- [ ] **Step 2: `REPlayerController.cpp` — `OnFire` 클라 로컬 회전**

`OnFire`의 `LastFireRequestTime = Now;` 와 `Server_RequestFire(Dir);` 사이에 삽입:

```cpp
	// 데디 회전 보정 (#72) — 폰 회전은 오너 클라에 복제되지 않는다
	// (ReplicatedMovement=COND_SimulatedOrPhysics, CMC::ShouldCorrectRotation()=false).
	// 서버가 Server_RequestFire에서 하는 커서 방향 회전을 내 화면에서도 보이게 로컬로 같이 돈다.
	// 리슨/싱글에서는 서버가 같은 값을 다시 넣으므로 무해.
	P->SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
```

`P`(`GetPawn()`)와 `Dir`(`GetSafeNormal2D()` 결과)은 같은 함수 안에 이미 있다. include 추가 없음.

- [ ] **Step 3: 빌드 게이트**

```powershell
$BB = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat"
$UP = "C:\Users\leeji\orca\workspaces\Project_RE\M4-move-repl\Project_RE.uproject"
& $BB Project_REEditor Win64 Development -Project="$UP" -WaitMutex
```

Expected: `Result: Succeeded`

유니티 빌드 변수 섀도잉(C4459) 같은 함정은 이 변경엔 없다(새 지역변수 0개). 실패하면 로그 그대로 워커에 넘겨라.

- [ ] **Step 4: Commit**

```powershell
git add Source/Project_RE/Core/RECharacterBase.cpp Source/Project_RE/Core/REPlayerController.cpp
git commit -m "fix(net): 데디 원격 클라 이동/회전 보정 (#72)"
```

---

### Task 2: 데디 실측 (보정 크기 + 회전 + off-navmesh + 싱글 회귀)

빌드된 에디터 바이너리로 데디 서버 1 + 클라 1을 띄우고 `p.NetShowCorrections`의 수치로 판정한다. 육안 판단은 회전 항목에만 쓴다.

**Files:**
- Modify: `docs/superpowers/specs/2026-08-10-dedi-move-replication-design.md` (실측치 기록)

**Interfaces:**
- Consumes: `p.NetShowCorrections`, `p.NetCorrectionLifetime`, `Net PktLag` (엔진 내장).
- Produces: 없음.

- [ ] **Step 1: 2프로세스 데디 기동**

```powershell
$UE  = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$W   = "C:\Users\leeji\orca\workspaces\Project_RE\M4-move-repl"
$UP  = "$W\Project_RE.uproject"

# 서버 (데디, 콘솔 로그만)
Start-Process $UE -ArgumentList @("`"$UP`"", "/Game/Level/Main", "-server", "-log", "-nosound",
  "-nosplash", "-port=7777", "-ExecCmds=`"p.NetShowCorrections 1`"",
  "-abslog=`"$W\Saved\Logs\dedi-server.log`"")

# 클라 (실RHI 창모드 — 마우스 클릭 필요)
Start-Process $UE -ArgumentList @("`"$UP`"", "127.0.0.1:7777", "-game", "-windowed", "-ResX=1280",
  "-ResY=720", "-log", "-nosound", "-nosplash", "-ExecCmds=`"p.NetShowCorrections 1,p.NetCorrectionLifetime 2`"",
  "-abslog=`"$W\Saved\Logs\dedi-client.log`"")
```

서버 기동 확인(로그): `IpNetDriver listening on port 7777`, 클라 접속 확인: `Join succeeded` / `Welcomed by server`.

에디터 바이너리라 **쿡 없이** 언쿡 콘텐츠로 뜬다. `-unattended`를 넣지 마라(Global Constraints).

PIE로 하고 싶으면: 에디터 실행 → Play 드롭다운 → Net Mode `Play As Client`, Number of Players `1`, `Run Dedicated Server` 체크 → Play. 콘솔(`~`)에 `p.NetShowCorrections 1` + `p.NetCorrectionLifetime 2`. 로그는 `$W\Saved\Logs\Project_RE.log`.

- [ ] **Step 2: 클라 창에서 이동 반복 + 보정 수치 수집**

클라 창에서 **우클릭 이동 10회**(매번 다른 방향, 도착 전에 다음 클릭 섞어라). 그 다음 서버 로그 수치를 뽑는다:

```powershell
$log = "C:\Users\leeji\orca\workspaces\Project_RE\M4-move-repl\Saved\Logs\dedi-server.log"
$vals = Select-String -Path $log -Pattern "Error for .* is ([0-9.]+) " |
        ForEach-Object { [double]$_.Matches[0].Groups[1].Value }
$vals.Count
$vals | Measure-Object -Average -Maximum
```

Expected: 평균/최대가 **한 자리 uu**(대략 1~6uu, 프레임레이트 의존). 스펙 3절 계산으로 16.6ms 창 잔차가 ≈1.2uu이고 오차 허용치가 1.73uu라 몇 프레임에 한 번 2~4uu씩 보정되는 게 정상.

**실패 판정: 평균이 20uu 이상 / 최대가 30uu 근처**면 Task 1 Step 1이 안 먹었다(스로틀 잔존). 확인 순서 — ① `NetworkMinTimeBetweenClientAdjustmentsLargeCorrection`도 0인지, ② `RECharacterBase`를 상속한 BP가 CDO 값을 덮지 않았는지, ③ 빌드가 이 워크트리 uproject로 됐는지.

- [ ] **Step 3: 육안 — 워프/카메라 저더/정지 시 뒤로 당김**

클라 창에서 볼 것:
- 이동 중 폰이 **툭툭 끊기지 않는다**(초록 캡슐=서버, 빨강 캡슐=클라가 겹쳐 보인다. 두 캡슐이 눈에 띄게 벌어지면 문제).
- 카메라가 저더 없이 따라간다.
- **이동 중 좌클릭** → 폰이 제자리에 서고 **뒤로 당겨지지 않는다**(발사 시 서버가 `StopMovement`하는 구간).

지연 환경도 한 번: 클라 콘솔에 `Net PktLag=100` + `Net PktLagVariance=20` 후 반복. 렉에서는 폰 위치가 상수만큼 뒤로 밀리는 게 정상이고(패스팔로잉 결과가 ping만큼 늦게 옴), **점프량**이 커지지 않아야 한다. `Net PktLag`가 안 먹으면 이 항목은 건너뛰어라(선택 항목).

- [ ] **Step 4: 회전 확인 (결정 2 판정)**

클라 창에서 **이동 중 좌클릭**, 커서를 폰 진행 방향과 다른 쪽에 두고 눌러라.

Expected: **내 폰이 커서 쪽으로 즉시 돈다.** 이 3줄이 없으면 데디에서는 진행 방향을 그대로 보고 있다(스펙 5절). 이동 중이 아니라 정지 상태에서 눌러도 돈다.

- [ ] **Step 5: off-navmesh 거부 (이슈 완료조건)**

클라 창에서 맵 밖(navmesh 없는 허공/바닥 밖) 우클릭 1회. 서버 로그:

```powershell
Select-String -Path $log -Pattern "\[Move\] rejected: off-navmesh" | Select-Object -Last 3
Select-String -Path $log -Pattern "\[Move\] Server_RequestMove recv target" | Select-Object -Last 3
```

Expected: `[Move] rejected: off-navmesh <좌표>` 출현 + 폰 무이동. 정상 클릭은 `Server_RequestMove recv target=` 로 남는다.

- [ ] **Step 6: 싱글/리슨 회귀**

두 프로세스 종료 후 에디터에서 `Play Standalone` 1회: 우클릭 이동 / 좌클릭 발사(정지+커서 회전) / 스페이스 대쉬 전부 기존대로. 회전이 **두 번 적용돼 튀는 현상 없음**(클라 로컬 + 서버가 같은 Yaw).

- [ ] **Step 7: 측정치 기록 + Commit**

스펙 문서 `## 검증 (완료 기준)` 아래에 실측 블록을 추가한다(항목 7):

```markdown
### 실측 (2026-08-10, 코디네이터)

| 항목 | 값 |
|---|---|
| 보정 오차 평균 / 최대 | <N> uu / <N> uu (샘플 <count>개) |
| 워프·카메라 저더 | <있음/없음> |
| 발사 시 뒤로 당김 | <있음/없음> |
| 클라 회전(커서 방향) | <보임/안 보임> |
| off-navmesh 거부 | <동작/미동작> |
| Standalone 회귀 | <정상/문제> |
```

```powershell
git add docs/superpowers/specs/2026-08-10-dedi-move-replication-design.md
git commit -m "docs(net): #72 데디 이동 복제 실측치 기록"
```

측정 결과가 **실패 판정**(Step 2)이거나 러버밴딩이 육안으로 남으면 스펙 결정 3(이동목표 복제 + 클라 직선 예측, 약 40줄)으로 승격한다 — 그건 별 계획 문서로 쓴다.

---

## Self-Review

**Spec coverage:**
- 결정 1 보정 스로틀 해제 → Task 1 Step 1 ✓
- 결정 2 클라 로컬 발사 회전 → Task 1 Step 2 ✓
- 결정 3 클라 예측 보류 → 스코프 밖, Task 2 Step 7에 승격 조건 명시 ✓
- 결정 4·5 (시뮬프록시 불가 / 스무딩 무효) → 코드 변경 없음, 스펙 근거만 ✓
- 검증 1 빌드 게이트 → Task 1 Step 3 ✓
- 검증 2 보정 크기 → Task 2 Step 2 ✓
- 검증 3 클라 화면 → Task 2 Step 3 ✓
- 검증 4 회전 → Task 2 Step 4 ✓
- 검증 5 off-navmesh → Task 2 Step 5 ✓
- 검증 6 싱글 회귀 → Task 2 Step 6 ✓
- 검증 7 측정치 기록 → Task 2 Step 7 ✓

**Placeholder scan:** Task 2 Step 7의 `<N>`/`<있음/없음>`은 실행 산출물 기입란(측정 전에는 값이 없다). 나머지 코드/커맨드 블록 전부 완전 — 경로는 워크트리 절대경로로 확정.

**Type consistency:** `NetworkMinTimeBetweenClientAdjustments`/`...LargeCorrection` 둘 다 `float`, public UPROPERTY(`CharacterMovementComponent.h:860/868`) ✓. `SetActorRotation(FRotator)` — `Dir.Rotation().Yaw`는 `double`이나 `FRotator` 생성자에서 축소 변환, 서버 경로(`REPlayerController.cpp:251`)와 완전히 동일한 표현 ✓. 헤더 선언 추가 없음 → 선언/정의 불일치 여지 없음 ✓.
