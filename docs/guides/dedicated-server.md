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

## HP 리플리케이션 / 체력바 클라 갱신 (#73)

### 코드 경로 (소스 대조 결과)

서버 → 원격 클라 체력바 경로는 코드상 완결되어 있다. 끊긴 고리 없음.

| 단계 | 위치 |
|---|---|
| 데미지 판정(서버 전용) | `Mass/REBulletHitProcessor.cpp:30`, `Mass/REArcHitProcessor.cpp:17` — `ExecutionFlags = Standalone\|Server` |
| Health 차감(서버 권위) | `Core/RECharacterBase.cpp:98-111`, `Core/REBossCharacter.cpp:350-358` — `HasAuthority()` 가드 후 차감 |
| 복제 등록 | `RECharacterBase.cpp:130`, `REBossCharacter.cpp:376` — `DOREPLIFETIME(..., Health)`. 액터 복제는 `APawn` 기본값(`Pawn.cpp:86` `bReplicates = true`) |
| 클라 수신 | `ReplicatedUsing = OnRep_Health` → `HealthBar->SetHealthPercent()` (`RECharacterBase.cpp:177-183`, `REBossCharacter.cpp:379-385`) |
| 서버/싱글 | `TakeDamage`가 `OnRep_Health()`를 직접 호출 — 리슨/싱글에서도 같은 갱신 로직을 타므로 회귀 위험 없음 |
| 위젯 생성 타이밍 | `UI/REHealthBarComponent.cpp:34-41` — 위젯 생성 전 도착한 percent는 `CachedPercent`에 저장 후 `InitWidget`에서 반영 |

`Health`를 읽는 곳은 체력바가 유일하다(그 외 소비자 없음).

### 주의 지점 판정 (이슈 #73 항목 1~3)

**1. `MaxHealth` 비복제 — 무변경.**
쓰기는 두 생성자뿐(`RECharacterBase.cpp:38`, `REBossCharacter.cpp:45`)이고 값 출처는 `UREStatsSettings`(`Config/DefaultGame.ini`) 단일. 런타임에 `MaxHealth`를 바꾸는 코드가 전무하므로 클라 비율이 틀어질 수 있는 경로가 없다. 복제 추가는 YAGNI. 헤더 주석에 근거를 남겼다.

**2. `bIsDead` 비복제 — 무변경.**
소비자가 전부 서버 전용이다: `RECharacterBase.cpp:114`(EndGame 재진입 차단), `REBossCharacter.cpp:150/228`(`FireArtillery` / `TriggerBulletPattern` 가드). 보스 발사는 `StartFiring`을 **GameMode(서버 전용 액터)만** 호출하므로(`REGameMode.cpp:80`) 클라에서 실행되지 않는다. 클라가 `bIsDead`를 읽는 지점이 0개 → 복제 불필요. 사망 시각 처리(모델 숨김/래그돌)를 넣게 되면 그때 재검토.

**3. 데디 서버 `HealthBar` 위젯컴포넌트 — 무변경.**
엔진이 이미 데디를 스킵한다. `UWidgetComponent`가 `IsRunningDedicatedServer()`로 가드하는 지점: `InitWidget` 1748, `OnRegister` 960, 틱 1245/1253, 1553 (`Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp`). 위젯이 생성되지 않으므로 `REHealthBarComponent::InitWidget` / `SetHealthPercent`의 `Cast<UREHealthBarWidget>(GetUserWidgetObject())`는 null → 자연 no-op(경고·크래시 없음). `REBulletRenderSubsystem`식 `NM_DedicatedServer` 조기 반환을 추가할 이유가 없다.

### PIE 검증 절차 (실행 필요 — 코디네이터 담당)

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
