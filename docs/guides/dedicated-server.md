# 데디케이티드 서버 빌드 절차 (M4)

## 왜 소스 빌드 엔진이 필요한가

런처(Epic Games Launcher)가 설치해주는 바이너리 엔진은 **Server 타겟을 빌드할 수 없다.** UBT가 명시적으로 거부한다:

```
Server targets are not currently supported from this engine distribution.
Result: Failed (OtherCompilationError)
```

Installed Build는 Editor / Game 타겟만 프리컴파일해 배포한다 (`Engine/Intermediate/Build/Win64` 에 `UnrealEditor`, `UnrealGame` 만 있고 `UnrealServer` 가 없다). Server 컨피그로 엔진을 링크할 재료가 아예 없다.

→ **소스 빌드 엔진이 유일한 경로다.** 우회 방법 없다.

## 환경

| 항목 | 값 |
|---|---|
| 엔진 | `E:\UnrealEngine-5.8\UnrealEngine-5.8` (소스 빌드, 5.8.1) |
| 엔진 GUID | `{51F2F747-442F-4E4C-1745-C98EB33B1AD6}` |
| 에디터 | `<엔진>\Engine\Binaries\Win64\UnrealEditor.exe` |
| `.uproject` | `"EngineAssociation": "{51F2F747-...}"` |

`UnrealEditor.exe` 가 0.5MB인 건 정상이다 — Development 모듈러 빌드라 실체는 `UnrealEditor-*.dll` 544개에 있다.

**GUID는 이 PC 로컬 값이다.** `HKCU\SOFTWARE\Epic Games\Unreal Engine\Builds` 에 등록된 것으로, 머신마다 다르다. 그래서 `.uproject` 의 이 변경은 **커밋하지 않는다.** 다른 환경에서 clone하면 `EngineAssociation` 이 `"5.8"` 인 채이고, 그 환경에선 Server 타겟이 또 거부된다 — 각자 소스 엔진을 빌드하고 자기 GUID를 넣어야 한다.

## 빌드

`Source/Project_REServer.Target.cs` 가 Server 타겟을 정의한다 (`TargetType.Server`). 커밋되어 있다.

```powershell
$BB = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat"
$UP = "E:\UnrealProjects\Project_RE\Project_RE.uproject"

& $BB Project_REEditor Win64 Development -Project="$UP" -WaitMutex
& $BB Project_REServer Win64 Development -Project="$UP" -WaitMutex
```

산출물: `Binaries/Win64/Project_REServer.exe`

## 쿡 + 스테이징

**서버 exe 단독 실행에는 쿡이 필수다.** 아래 "함정" 참조.

```powershell
& "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
  -project="E:\UnrealProjects\Project_RE\Project_RE.uproject" `
  -noP4 -platform=Win64 -server -noclient -serverconfig=Development `
  -cook -stage -pak -build -utf8output
```

산출물: `Saved/StagedBuilds/WindowsServer/`

## 실행

```powershell
Saved\StagedBuilds\WindowsServer\Project_RE\Binaries\Win64\Project_REServer.exe `
  /Game/Level/Main -log -port=7777
```

정상 기동 로그:

```
LogLoad: Game class is 'REGameMode'
LogNet: IpNetDriver listening on port 7777
LogWorld: Bringing World /Game/Level/Main.Main up for play (max tick rate 30)
```

## 개발 루프는 쿡이 필요 없다

일상 개발은 **에디터 PIE**로 한다. Play 드롭다운 → Net Mode `Play As Client` + `Run Dedicated Server`. 쿡 없이 데디 서버 + 클라이언트가 뜬다.

standalone 서버 exe는 배포 / 부하테스트 단계용이다. 코드 고칠 때마다 재쿡하지 마라.

## 함정

- **언쿡 콘텐츠로 서버 exe를 띄우면 크래시한다.** Server 타겟은 비-에디터 타겟이라 `FPlatformProperties::RequiresCookedData() == true` 다. 언쿡 패키지 헤더를 쿡 전제로 파싱해 `FPackageFileSummary` 역직렬화가 어긋난다:

  ```
  Assertion failed: ReaderPos + Num <= ReaderSize [BufferReader.h:52]
    FBufferReaderBase::Serialize()
    operator<<(FString)          String.cpp.inl:1895
    operator<<(FEngineVersion)   EngineVersion.cpp:263
    operator<<(FPackageFileSummary) PackageFileSummary.cpp:407
    FAsyncArchive::ReadCallback()   AsyncLoading.cpp:445
  ```

  `/Engine/EngineMaterials/WorldGridMaterial` 같은 **엔진 기본 에셋**에서 터지므로 프로젝트 에셋을 의심하게 되는데, 원인은 "쿡 데이터가 없다"는 것 하나다. 같은 로그의 `Failed to load premade asset registry. LoadResult == 1` 이 방증이다. 버그가 아니라 구조적 제약 — 쿡하면 해결된다.

- **`ServerDefaultMap` 미설정.** 맵 인자 없이 띄우면 `/Engine/Maps/Entry` 로 폴백한다. 명령줄에 맵을 주거나 `Config/DefaultEngine.ini` 에 설정해라.

- **디스크를 크게 먹는다.** 엔진 소스 88GB + 빌드 산출물이 더해진다. 실측:

  | 항목 | 크기 |
  |---|---|
  | `<엔진>\Engine\Binaries` | 43.6 GB |
  | `<엔진>\Engine\Intermediate` | 28.7 GB |
  | `Project_RE\Intermediate` | 23.4 GB |

  `Engine\Binaries` 를 지우면 엔진을 3시간 재빌드해야 한다. Intermediate도 마찬가지.

- **첫 빌드 소요 (6코어 / 32GB 실측)**

  | 단계 | 시간 |
  |---|---|
  | 엔진 Development Editor (8055 액션) | 2시간 57분 |
  | 프로젝트 Editor 타겟 | 87초 |
  | 프로젝트 Server 타겟 | 75분 |
  | 쿡 (548 패키지) + Pak | 70초 |
  | 에디터 첫 실행 셰이더 (14,261개) | 8분 |

  엔진 빌드 중 UBA가 메모리 임계(커밋 ~45GB)에서 컴파일 프로세스를 킬한다. 자동 재큐잉되니 죽지는 않지만 느려진다 — 빌드 중엔 메모리 큰 프로그램을 띄우지 마라.

- **빌드는 반드시 하네스 수명에서 분리해 띄워라.** 3시간짜리 빌드를 타임아웃 있는 셸로 돌리면 중간에 잘린다. `Start-Process ... -PassThru` 로 detach하고 로그 파일을 따로 감시하는 방식이 안전하다.

## HP 리플리케이션 검증 절차 (#73)

서버 → 원격 클라 체력바 경로의 **코드 분석과 판정 근거**는 설계 문서에 있다:
`docs/superpowers/specs/2026-08-10-dedi-hp-replication-design.md`
(요지: 경로에 끊긴 고리 없음, `MaxHealth`/`bIsDead` 비복제와 데디 위젯컴포넌트 3건 모두 무변경 판정.)

여기에는 **실행 절차만** 둔다.

### PIE 검증 절차

에디터 Play 드롭다운 → Net Mode `Play As Client`, `Run Dedicated Server` 체크, Players `1`.
이 모드에선 **화면에 뜨는 창이 클라 창 하나뿐**이다(데디 서버 월드는 렌더되지 않음) → 보이는 체력바가 곧 클라 복제 결과다.

1. **플레이어 체력바**: 보스 탄막에 맞을 때 클라 창의 머리 위 초록 바가 줄어드는지. 스크린샷 2장(피격 전/후)으로 근거를 남긴다.
2. **보스 체력바**: 좌클릭 홀드로 보스를 때렸을 때 빨간 바가 줄어드는지.
3. **수치 근거(육안 대신)**: 클라 창에서 `` ` `` 콘솔 →
   ```
   DisplayAll RECharacterBase Health
   DisplayAll REBossCharacter Health
   ```
   클라 로컬 액터의 복제된 `Health` 값이 화면에 실시간 출력된다. 값이 감소하면 복제 경로 확인 완료.
4. **결과 위젯**: HP 0까지 진행 후 로그 확인 —
   ```powershell
   Select-String -Path "Saved\Logs\Project_RE.log" -Pattern "\[RE\] (EndGame|Client_ShowResult|Player died|Boss died)"
   ```
   `[RE] EndGame: DEFEAT`(서버) 다음에 `[RE] Client_ShowResult: DEFEAT`(클라)가 찍혀야 한다. `Client_ShowResult`는 `UFUNCTION(Client, Reliable)`(`REPlayerController.h:29`)이라 이 로그 자체가 클라 도달 증거다. 승리 경로도 같은 방식.
5. **회귀 확인**: Net Mode `Standalone`으로 한 번 더 돌려 싱글에서 체력바/결과 위젯이 종전대로 동작하는지.

**함정: PIE는 3번 항목의 검증에 쓸 수 없다.** `IsRunningDedicatedServer()`는 `-server` 커맨드라인으로 판정하므로(`Core/Public/Misc/CoreMisc.h:152`) 에디터 프로세스에선 항상 false다 → PIE의 데디 서버 월드는 위젯을 실제로 생성한다. 위젯 스킵을 실측하려면 스테이징된 `Project_REServer.exe`로 띄워야 한다(위 "실행" 절차).

## 코드 수정 후 재검증 루프 (실측)

첫 빌드 표(위)는 **환경 구축 1회** 비용이다. 일상적인 코드 수정 → 데디 재검증은 훨씬 싸다:

| 단계 | 시간 | 커맨드 |
|---|---|---|
| Editor 타겟 증분 빌드 | 10~60초 | `Build.bat Project_REEditor Win64 Development -Project=...` |
| Server 타겟 증분 빌드 | 87초 | `Build.bat Project_REServer Win64 Development -Project=...` |
| 쿡 + 스테이징 (`-skipbuild`) | 약 2분 | `RunUAT BuildCookRun ... -cook -stage -pak -skipbuild` |

`-skipbuild`를 빼먹지 마라 — 이미 빌드한 걸 다시 빌드한다.

**에디터가 켜져 있으면 빌드가 실패한다:**
```
Unable to build while Live Coding is active. Exit the editor and game, or press Ctrl+Alt+F11
```
헤더를 건드린 변경은 Live Coding 패치로 안 붙는 경우가 많으니, 에디터를 닫고 빌드하는 편이 확실하다.

**Config 변경은 반드시 재쿡해야 반영된다.** 스테이징 빌드의 `Config`는 pak 안에 들어간다(`Project_RE-WindowsServer.pak`) — `Config/*.ini`만 고치고 서버 exe를 다시 띄우면 옛 값이 그대로 쓰인다.

## 서버 + 클라 2프로세스 검증 (자동화)

PIE로는 데디 분기를 재현할 수 없으므로(`IsRunningDedicatedServer()`가 빌드 타깃 기준), 실제 검증은 프로세스 2개로 한다. 기동·대기·종료·판정은 `scripts/dedi-verify.ps1` 이 전부 수행한다 (#82).

```powershell
scripts\dedi-verify.ps1              # 서버 + 클라 1개
scripts\dedi-verify.ps1 -Clients 2   # 인자만 열려 있음 — 아래 천장 먼저 읽어라
scripts\dedi-verify.ps1 -SelfTest    # 판정 로직만 검사(프로세스 미기동)
```

전 항목 통과 시 종료 코드 `0`, 실패 시 `1` + 실패 항목·이유 출력.

**이 스크립트는 이미 스테이징된 산출물을 전제한다.** 빌드/쿡은 하지 않는다 — 코드를 고쳤으면 위 "재검증 루프" 표대로 먼저 빌드+재쿡해라. 안 그러면 옛 산출물을 검증한다.

판정 항목:

| 대상 | 있어야 하는 것 | 없어야 하는 것 |
|---|---|---|
| 서버 | `IpNetDriver listening`, `Bringing World .../Main.Main`, `[Dash] dist=`(500~700), `[RE] Boss Fire(Direct\|Artillery):... role=ROLE_Authority`, `[Dash] probe done` | `[Attack] fire montage`, `[Dash] anim len=` (데디 코스메틱 생략 가드 #74/#75), `Assertion failed`/`Critical error` |
| 클라 | `[Attack] fire montage len=`, `[Dash] anim len=... (role=ROLE_AutonomousProxy)`, `[RE] Boss Fire(Direct\|Artillery):... role=ROLE_SimulatedProxy` | `Assertion failed`/`Critical error` |

종료 조건은 고정 대기가 아니다 — 서버측 프로브(`RunHeadlessDashProbe`)가 완주하며 `RequestExit` 하므로, 스크립트는 **서버 프로세스의 자체 종료**를 프로브 완료 신호로 쓴다. 클라 접속 후 약 4.4초.

**승리 경로(`-Victory`)는 기본 실행에서 통과하지 않는다.** 서버 프로브의 첫 명중은 10 데미지인데 쿡된 `BossMaxHealth` 는 1000이다. 아래 "무적 치트" 항목대로 값을 낮추고 재쿡한 상태에서만 `-Victory` 를 붙여라.

**`-Clients 2` 이상은 아직 반쪽이다.** 첫 클라의 서버측 프로브가 완주하며 서버를 내리므로, 뒤 클라의 서버측 프로브는 시작도 못 하고 잘린다. 그런데 뒤 클라도 코스메틱 로그(발사/대쉬 몽타주)는 접속 직후 찍히므로 **판정은 전부 초록으로 뜬다** — 이걸 "2인 데디 검증 완료"로 읽으면 안 된다. 클라별 프로브 완주가 실제로 필요해지면 프로브에 클라 인덱스 게이트를 넣어야 한다(M5 몫).

### 2인 수동 페어 검증 (#85, 자동화는 #87 몫)

`dedi-verify.ps1 -Clients 2`가 반쪽인 동안은 서버·클라 2개를 손으로 붙여서 본다.

1. **서버에 `-ExecCmds="re.Coop.ExpectedPlayers 2"` 를 준다.** 협동 인원은 ini가 아니라 CVar다 — 스테이징 `Config`는 pak 안에 들어가 인원을 바꿀 때마다 재쿡해야 하지만, CVar면 커맨드라인만 바꾸면 되므로 재쿡 없이 인원 수를 바꿔가며 검증할 수 있다.
2. **서버에 `-unattended`를 주지 않는다.** 주면 첫 클라 접속 시 서버측 헤드리스 프로브(위 "서버 + 클라 2프로세스 검증" 절 참조)가 켜져 약 4.4초 뒤 `RequestExit`으로 서버가 죽는다 — 둘째 클라가 붙기 전에 서버가 내려간다.
   ```powershell
   $srv = Start-Process ...\Project_REServer.exe -ArgumentList "-log","-port=7777","-ExecCmds=`"re.Coop.ExpectedPlayers 2`"","-abslog=..."
   # 서버 리스닝 확인 후 클라 1, 이어서 클라 2를 붙인다(각각 UnrealEditor-Cmd.exe ... 127.0.0.1:7777 -game -nullrhi)
   ```
3. **확인할 판정 4개**(서버·클라 로그를 `Select-String`으로 grep):
   - 클라 1만 접속한 상태에서는 `[RE] Player ready 1/2`만 있고 **`Boss firing started`는 없다** — 게이트가 인원을 세고 있다는 증거.
   - 클라 2 접속 직후 `[RE] Spawn player idx=0 offsetY=...`와 `idx=1 offsetY=...`의 **오프셋 값이 서로 다르다**(대칭, `SpawnSpacing` 기준).
   - 두 클라 로그 모두에 `Boss Fire(Direct|Artillery):... role=ROLE_SimulatedProxy`가 찍힌다 — 양쪽 다 탄막을 수신한다.
   - 전원 사망 시 두 클라 로그 모두에 `[RE] Client_ShowResult: DEFEAT`가 찍힌다 — 결과 화면이 전 클라에 간다.
4. 프로세스는 반드시 직접 정리한다(`Stop-Process -Force`) — `dedi-verify.ps1`처럼 자동 종료를 기다려주는 로직이 없다.

**주의 — NavMesh 재검증(스폰 이격을 바꾼 뒤 필요)은 위 2번과 정반대 설정이다.** 헤드리스 이동 프로브(`RunHeadlessMoveProbe`)는 `-unattended`가 있어야만 켜지므로, 이번엔 서버에 `-unattended`를 주고 대쉬 프로브가 `RequestExit`을 부르기 전 약 4.4초 창 안에 클라 둘을 모두 접속시켜야 한다 — 판정은 각 클라 로그에 `[Move] probe start`가 있는지(프로브가 실제로 돌았다는 증거)와 실제 목표 좌표에 대한 `[Move] rejected: off-navmesh`가 없는지이며, 프로브가 일부러 맵 밖 좌표도 하나 요청하므로 그 좌표를 지목한 거부 로그 한두 줄은 오히려 거부 경로가 살아있다는 정상 증거다(무발동으로 인한 침묵 통과와 혼동하지 말 것).

**주의 — #86이 살아있는 동안은 "전원 사망" 경로를 자연 전투로 재현할 수 없다.** `Mass/REBulletHitProcessor.cpp`와 `Mass/REArcHitProcessor.cpp`의 두 히트 프로세서가 피격 대상을 `UGameplayStatics::GetPlayerPawn(World, 0)`(플레이어 인덱스 0) 하나로 하드코딩하고 있어, 2인 이상 접속 시 인덱스 0이 아닌 플레이어는 탄막 데미지를 원천적으로 받지 못한다(실측: 1258회 피격 판정이 전부 인덱스 0에게만 적용, 나머지 플레이어는 9분 이상 관측해도 0회). 이 상태로는 판정 4번째 항목(전원 사망 → 양쪽 결과 화면)이 실전투로는 절대 도달하지 않는다 — 재시도해도 소용없다. #86이 고쳐지기 전까지는 (a) `NotifyPlayerDied`/`EndGame` 경로를 코드 리뷰로만 신뢰하거나, (b) 히트 프로세서를 임시로(커밋 금지) 전원 대상으로 바꿔 프로브하는 수밖에 없다. 다음에 이 절차를 돌리는 사람은 둘째 사망을 기다리며 시간을 태우기 전에 이 문단을 먼저 읽어라.

**이 스크립트가 대체하지 못하는 것:** 캐릭터 회전처럼 로그에 안 남는 항목은 여전히 육안이다(#70에서 실증). 스크린샷/영상 캡처는 하지 않는다.

### 궤도 일치 정량 측정 (#84)

서버·클라가 같은 탄환 궤적을 그리는지는 로그 존재 여부(위 표)로는 안 잡힌다 — 좌표 자체를 비교해야 한다. 방법: `REBulletSimProcessor.cpp`의 `Execute`에 임시 프로브(커밋 안 함)를 넣어 서버·클라 양쪽에서 생존 탄 좌표를 `TrajDump`로 찍고, 각 서버 좌표에서 가장 가까운 클라 좌표까지의 거리 중 최댓값(worst nearest-neighbour distance)을 본다. **게이트: < 10 uu(10cm).**

덤프 트리거는 로컬 `World->GetTimeSeconds()`가 아니라 **`AGameStateBase::GetServerWorldTimeSeconds()`**(서버 동기화 클럭)로 건다 — 로컬 월드 시각은 클라 접속 시 0으로 리셋돼 서버·클라가 "같은 실제 순간"을 가리키지 못한다. `dedi-verify.ps1`은 접속 유지 구간이 ~4.1초로 짧아(대쉬 프로브가 서버를 일찍 끔) 이 측정엔 쓰지 않는다 — `-unattended` 없이 서버·클라를 따로 띄운 전용 페어로 측정한다.

실측(#84, 스파이럴 첫 볼리 16발): **worst = 6.61 uu — PASS.**

**측정 하한(floor)이 있다.** 서버·클라는 각자의 틱에서 독립적으로 임계값 통과를 감지하므로 두 덤프는 최대 서버 넷틱 1개만큼 어긋날 수 있다 — 그 틱 사이 탄환은 계속 날아간다. 하한 크기는:

```
floor ≈ BulletSpeed / NetServerMaxTickRate
```

실측 조건(`BulletSpeed=200`, `NetServerMaxTickRate=30` 기본값)에서 floor ≈ 6.67 uu — 실측 6.61 uu와 일치한다. 즉 **6.61 uu는 대부분 덤프 타이밍 편차이지, 궤적 자체의 오차가 아니다.** (`FVector_NetQuantize`의 정수cm 반올림은 1 uu 미만이라 이 크기를 설명하지 못한다.) 다시 말해 **이 방법으로는 floor보다 작은 실제 궤적 오차를 구분해낼 수 없다** — worst가 floor 근처거나 밑이면 "일치했다"가 아니라 "이 측정 해상도로는 안 보인다"로 읽어야 한다.

이 하한은 `BulletSpeed`에 비례한다 — 더 빠른 탄속 패턴은 이 방법만으로 10 uu 게이트에 근접·초과할 수 있다, 재동기화가 완전히 정상이어도. **나중에 `BulletSpeed`를 올렸을 때 이 게이트가 실패하면, 먼저 이 하한 공식부터 확인해라** — 팬텀 디싱크를 쫓기 전에.

**측정 상한(ceiling)도 있다 — 시간 보정이 상쇄하는 건 전송 지연이 아니다.** UE 5.8 `AGameStateBase::GetServerWorldTimeSeconds()`는 ping/RTT 보정 항이 없는 EMA 추정치라(`Engine/Private/GameStateBase.cpp:168-192`), 클라 시각 추정치의 뒤처짐과 RPC 도착 지연이 서로 상쇄되고 `Elapsed`는 서버 자신의 큐잉/틱 잔차만 흡수한다(상세: 설계 스펙 "시간 보정" 절 정정). 따라서 실제 링크에서는 클라의 탄이 서버 권위 위치보다 대략 편도 지연 하나만큼 뒤처진 채로 그려진다 — `BulletSpeed=200`·RTT 100ms(편도 50ms)에서 약 10uu, 지연·탄속에 선형 비례한다. 로컬호스트 측정(위 floor)은 ping≈0이라 이 어긋남을 재현하지 못한다 — floor가 통과해도 실링크 편도 지연분은 이 게이트 밖의 별개 오차다. 개선 경로는 클라측에 `PlayerState->ExactPing * 0.0005f`(편도 추정) 항을 더하는 것인데, 실지연 테스트 환경이 생겨 이 항의 효과를 검증할 수 있을 때만 넣는다 — 로컬호스트로는 검증 자체가 불가능하다.

**피격 탄이 클라에서 안 사라지는 것도 이 측정 밖이다.** `REBulletHitProcessor`(`Standalone|Server`)는 명중한 탄을 서버에서만 파괴하고, `REBulletSimProcessor`(`AllNetModes`)는 수명이 다할 때만 파괴한다 — 플레이어를 맞춘 탄이 서버에서는 사라져도 클라 화면에는 `BulletLifetime=15`초 동안 계속 날아다닌다(최대 약 10발 유계, HP 100 / 히트당 10뎀). 곡사탄은 해당 없음 — `REArcSimProcessor`는 착지 시 양쪽 다 파괴한다. worst nearest-neighbour 측정은 서버 좌표마다 최근접 클라 좌표까지의 거리이므로, 클라에만 남는 이 여분 엔티티는 어느 서버 좌표의 최근접도 아니라 최댓값 계산에 전혀 안 잡힌다 — worst가 정상이어도 이 유령 탄의 반증이 아니다. 디스폰 브로드캐스트가 필요한 수정이라 이 이슈 범위 밖이다.

## 함정 (검증편)

- **접속 종료 후 클라 로그가 오염된다.** 서버가 먼저 종료되면 클라는 `Browse: /Game/Level/Main?closed` 로 **자기 스탠드얼론 월드**를 띄운다. 그 뒤로 나오는 `role=ROLE_Authority` 줄이나 두 번째 `Client_ShowResult`는 별개 게임의 로그다 — 데디 동작으로 오독하지 마라. 판정은 `Host closed the connection` **이전** 구간만 본다. (`dedi-verify.ps1` 은 이 절단을 자동으로 한다. 실측 1666줄 로그에서 마커는 1601줄이었고 그 뒤에 `[Dash] anim len=0.97 (role=ROLE_Authority)` 가 찍혔다 — 절단 없이는 오너 경로가 깨져도 통과한다.)

- **`-abslog=$var` 는 반드시 변수가 전개되는 형태로 넘겨라.** 수동으로 프로세스를 띄울 때의 함정이다. 리터럴 `$log` 가 그대로 들어가면 로그 파일이 아예 생성되지 않고, `Saved/Logs/Project_RE.log` 에도 안 남아 조용히 관측에 실패한다.

- **`.ps1` 은 UTF-8 BOM으로 저장해라.** Windows PowerShell 5.1은 BOM 없는 파일을 ANSI 코드페이지로 읽어 한글 주석·문자열이 깨지고, 깨진 바이트가 따옴표 짝을 무너뜨려 파싱 자체가 실패한다.

- **무적 치트는 데디에서 동작하지 않는다.** `re.Cheat.PlayerInvincible` 은 클라 로컬 CVar이고 `TakeDamage` 는 서버에서 돈다(`RECharacterBase.cpp` 주석의 알려진 천장). 승리 경로를 확인하려면 치트 대신 **`Config/DefaultGame.ini` 의 `BossMaxHealth` 를 임시로 낮추고 재쿡**해라. 서버 프로브의 첫 명중(10 데미지)으로 보스가 죽어 `Boss died → EndGame: VICTORY → Client_ShowResult: VICTORY` 경로를 그대로 탄다. 확인 후 원복 + 재쿡을 잊지 마라.
