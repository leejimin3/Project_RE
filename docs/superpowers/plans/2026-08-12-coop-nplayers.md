# N인 협동 대응 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 게임루프의 "플레이어는 1명" 가정 5곳을 걷어내 N인 협동이 성립하게 한다 (#85).

**Architecture:** N인 상태는 전부 서버 `AREGameMode` 안에만 둔다(복제 상태 없음, `AREGameState` 안 만듦). 클라가 알아야 할 "내가 죽었다"·"게임이 끝났다"는 기존 Client RPC로 밀어준다. 보스 조준은 #84가 만든 서버 결정 경로 안에서 타깃만 바꾸므로 **RPC 계약이 바뀌지 않는다.**

**Tech Stack:** UE 5.8, C++ (`AGameModeBase::Logout` / `SpawnDefaultPawnAtTransform`, Client RPC, `TAutoConsoleVariable`).

설계 스펙: `docs/superpowers/specs/2026-08-12-coop-nplayers-design.md`

## Global Constraints

- 브랜치: `feature/M5-coop-nplayers` (이미 생성됨, 스펙 커밋 1건 있음)
- ⚠️ **본체 작업트리(`E:\UnrealProjects\Project_RE`)에서 실행 전제.** 아래 명령의 `-Project`가 절대경로다. 격리 worktree에서 그대로 돌리면 worktree가 아니라 본체를 빌드하고, 캐시 때문에 수 초 만에 `Succeeded`가 떠서 검증이 통과한 것처럼 보이지만 아무것도 검증하지 않는다.
- 빌드 게이트 — **Editor와 Server 둘 다** 통과해야 한다:
  ```powershell
  $BB = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat"
  $UP = "E:\UnrealProjects\Project_RE\Project_RE.uproject"
  & $BB Project_REEditor Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  & $BB Project_REServer Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  ```
  기대: 양쪽 `Result: Succeeded`. 타임아웃 400000ms.
- **에디터가 켜져 있으면 빌드가 실패한다** (`Unable to build while Live Coding is active`). 닫아둔 상태를 유지한다.
- **데디 실행 전에는 재쿡이 필요하다:**
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
    -project="$UP" -noP4 -platform=Win64 -server -noclient -serverconfig=Development `
    -cook -stage -pak -skipbuild -utf8output
  ```
  `-skipbuild`를 빼먹지 마라. 약 2분, 타임아웃 500000ms.
- 자동화 테스트 인프라 없음 → 게이트 = 빌드 + 헤드리스 로그 프로브 + 데디 실측.
- 로그 접두어 `[RE]` 고정.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- `Project_RE.uproject`는 **절대 스테이징하지 않는다** — `EngineAssociation` GUID가 머신 종속이다. `git status`에 수정으로 뜨는 것이 정상.
- 주석은 한국어. 주변 스타일에 맞춘다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- YAGNI: 스펙에 없는 기능·추상화·설정값 추가 금지.

## File Structure

| 파일 | 책임 | 변경 |
|---|---|---|
| `Core/REGameMode.h` / `.cpp` | N인 상태 전부(준비·사망·스폰 인덱스), 시작 게이트, 승패 판정 | 수정 — 이 이슈의 본체 |
| `Core/REPlayerController.h` / `.cpp` | 준비 통지에 자기 PC 전달, 사망 통지 수신 | 수정 |
| `Core/RECharacterBase.h` / `.cpp` | `IsAlive()` 공개, 사망 시 GameMode 통지로 전환 | 수정 |
| `Core/REBossCharacter.h` / `.cpp` | 최근접 생존자 조준 | 수정 |

새 파일 없음. 새 클래스 없음.

---

### Task 1: 시작 게이트를 인원 카운트로

`bool bPlayerReady` 하나로는 "몇 명 모였나"를 셀 수 없다. #84가 남긴 훅을 카운트로 바꾼다.

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h`
- Modify: `Source/Project_RE/Core/REGameMode.cpp`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Produces: `AREGameMode::NotifyPlayerReady(APlayerController* PC)` — 시그니처가 바뀐다(기존 무인자)
- Produces: `ReadyPlayers` (TSet) — Task 2·3이 승패 분모로 쓴다
- Produces: CVar `re.Coop.ExpectedPlayers` — Task 5가 스폰 오프셋 계산에 쓴다

- [ ] **Step 1: 헤더 — 선언 교체**

`REGameMode.h`의 `NotifyPlayerReady` 선언을 인자 받는 형태로 바꾼다:

```cpp
	/** 클라 준비 통지 수신 (#84/#85). 보스 발사 시작 조건을 재평가한다. */
	void NotifyPlayerReady(APlayerController* PC);
```

`private:` 의 `bool bPlayerReady = false;` **한 줄을 삭제**하고 그 자리에:

```cpp
	/**
	 *  준비를 알린 PC 집합 (#85). Num()이 곧 실제 접속자 수라 승패 판정 분모로도 쓴다.
	 *  int32 카운터가 아니라 집합인 이유: 클라가 Server_NotifyReady를 두 번 보내도
	 *  수가 부풀지 않는다. 카운터였다면 연타 한 번에 게이트가 뚫린다.
	 */
	UPROPERTY()
	TSet<TObjectPtr<APlayerController>> ReadyPlayers;
```

- [ ] **Step 2: cpp — CVar 추가**

`REGameMode.cpp` 상단의 기존 익명 네임스페이스(`CVarProfilingKeepFiring` 이 있는 블록) 안, 그 CVar **아래**에 추가:

```cpp
	// #85 협동 인원. ready가 이 수를 채우면 보스 발사 시작(RPG 던전 입장 모델).
	// ini가 아니라 CVar인 이유: 스테이징 Config는 pak 안에 들어가서 ini면 인원을 바꿀 때마다
	// 재쿡해야 한다. CVar면 서버 커맨드라인(-ExecCmds)으로 넘길 수 있어 데디 검증이 재쿡 없이 돈다.
	static TAutoConsoleVariable<int32> CVarExpectedPlayers(
		TEXT("re.Coop.ExpectedPlayers"),
		1,
		TEXT("협동 시작에 필요한 준비 완료 플레이어 수. 기본 1(싱글 동작 유지)."),
		ECVF_Default);
```

`ECVF_Cheat`이 아니라 `ECVF_Default`다 — 치트가 아니라 세션 설정이다.

- [ ] **Step 3: cpp — 두 함수 교체**

`NotifyPlayerReady` 와 `TryStartBossFiring` 을 아래로 대체:

```cpp
void AREGameMode::NotifyPlayerReady(APlayerController* PC)
{
	if (PC)
	{
		ReadyPlayers.Add(PC);
	}
	const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
	UE_LOG(LogTemp, Log, TEXT("[RE] Player ready %d/%d"), ReadyPlayers.Num(), Expected);
	TryStartBossFiring();
}

void AREGameMode::TryStartBossFiring()
{
	const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
	if (bFiringStarted || !DemoBoss || ReadyPlayers.Num() < Expected)
	{
		return;
	}
	bFiringStarted = true;
	// 시드는 서버 전용 PhaseRng 초기화용 — 네트워크에 나가지 않는다 (#84).
	DemoBoss->StartFiring(/*Seed=*/FMath::Rand());
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss firing started (%d/%d ready)"), ReadyPlayers.Num(), Expected);
}
```

`BeginPlay` 끝의 `TryStartBossFiring();` 호출은 **그대로 둔다** — PC BeginPlay와 GameMode BeginPlay의 순서가 보장되지 않아 양쪽에서 부르는 구조가 필요하다.

- [ ] **Step 4: PlayerController — 자기 PC 전달**

`REPlayerController.cpp` 의 `Server_NotifyReady_Implementation` 에서 `GM->NotifyPlayerReady();` 를 `GM->NotifyPlayerReady(this);` 로 바꾼다.

- [ ] **Step 5: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 6: 싱글 회귀 프로브**

기본값 1이므로 동작이 종전과 같아야 한다.

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_t1.log"
```

```bash
grep -n "Player ready\|Boss firing started\|Boss FireDirect" Saved/Logs/RE_85_t1.log | head -5
```

기대: `[RE] Player ready 1/1` → `[RE] Boss firing started (1/1 ready)` → `Boss FireDirect` 가 이어진다.
셋 중 하나라도 없으면 게이트가 싱글을 막은 것이다.

- [ ] **Step 7: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(coop): 시작 게이트를 준비 인원 카운트로 전환 (#85)"
```

---

### Task 2: 사망 통지 + 전원 사망 판정

지금은 한 명이라도 죽으면 `EndGame(false)` 로 게임 전체가 패배한다. 사망을 GameMode에 통지하고, 전원이 죽었을 때만 패배로 확정한다.

**Files:**
- Modify: `Source/Project_RE/Core/REPlayerController.h`, `.cpp`
- Modify: `Source/Project_RE/Core/REGameMode.h`, `.cpp`
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Consumes: `ReadyPlayers` (Task 1) — 승패 판정 분모
- Produces: `AREPlayerController::Client_NotifyDeath()` — `UFUNCTION(Client, Reliable)`
- Produces: `AREGameMode::NotifyPlayerDied(APlayerController* PC)`
- Produces: `DeadPlayers` (TSet) — Task 3의 `Logout` 이 정리한다

- [ ] **Step 1: PlayerController 헤더 — 사망 통지 RPC**

`REPlayerController.h` 의 `Client_ShowResult` 선언 **아래**(같은 `public:` 블록)에 추가:

```cpp
	/**
	 *  사망 통지 (#85). 입력만 차단하고 폰·카메라는 그대로 둔다 —
	 *  그 자리에서 동료 전투를 보는 것이 곧 관전 시점이다(별도 관전 카메라 없음).
	 */
	UFUNCTION(Client, Reliable)
	void Client_NotifyDeath();
```

- [ ] **Step 2: PlayerController cpp — 구현**

`Client_ShowResult_Implementation` **아래**에 추가:

```cpp
void AREPlayerController::Client_NotifyDeath_Implementation()
{
	// 입력만 끊는다. 결과 화면은 게임이 끝날 때 Client_ShowResult가 따로 띄운다.
	DisableInput(this);

	UE_LOG(LogTemp, Log, TEXT("[RE] Client_NotifyDeath: input disabled (spectating)"));
}
```

- [ ] **Step 3: GameMode 헤더 — 진입점과 상태**

`public:` 의 `NotifyPlayerReady` 선언 **아래**에 추가:

```cpp
	/** 플레이어 사망 통지 (#85). 전원 사망이면 EndGame(DEFEAT)까지 간다. */
	void NotifyPlayerDied(APlayerController* PC);
```

`private:` 의 `ReadyPlayers` **아래**에 추가:

```cpp
	/** 사망한 PC 집합 (#85). ReadyPlayers를 채우면 전원 사망 = 패배. */
	UPROPERTY()
	TSet<TObjectPtr<APlayerController>> DeadPlayers;
```

- [ ] **Step 4: GameMode cpp — 구현**

`NotifyPlayerReady` **아래**에 추가:

```cpp
void AREGameMode::NotifyPlayerDied(APlayerController* PC)
{
	if (!PC || bGameOver)
	{
		return;
	}
	DeadPlayers.Add(PC);

	if (AREPlayerController* REPC = Cast<AREPlayerController>(PC))
	{
		REPC->Client_NotifyDeath();
	}
	// 서버측: 마지막 이동 명령이 남아 시체가 계속 미끄러지는 것을 막는다.
	PC->StopMovement();

	UE_LOG(LogTemp, Log, TEXT("[RE] Player died %d/%d"), DeadPlayers.Num(), ReadyPlayers.Num());

	// 분모는 ExpectedPlayers가 아니라 실제 접속자 수다 — 중간에 나간 사람이 있으면
	// 고정 분모로는 남은 사람이 다 죽어도 게임이 끝나지 않는다.
	if (DeadPlayers.Num() >= ReadyPlayers.Num())
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] All %d players dead"), ReadyPlayers.Num());
		EndGame(/*bVictory=*/false);
	}
}
```

- [ ] **Step 5: 캐릭터 사망 경로 전환**

`RECharacterBase.cpp` 의 사망 분기에서 `GM->EndGame(/*bVictory=*/false);` **한 줄**을 아래로 바꾼다:

```cpp
			// 전원 사망이어야 패배다 — 판정은 GameMode가 한다 (#85).
			GM->NotifyPlayerDied(Cast<APlayerController>(GetController()));
```

주변의 `if (Health <= 0.f && !bIsDead)` 가드와 `bIsDead = true;` 는 그대로 둔다.

- [ ] **Step 6: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 7: 싱글 회귀 프로브 — 1인은 종전대로 즉시 패배**

`ExpectedPlayers=1` 이면 `ReadyPlayers.Num()==1` 이라 한 명 사망이 곧 전원 사망이다.

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_t2.log"
```

`-unattended` 를 **빼고** 실행한다(프로브가 4.4초에 프로세스를 종료시켜 사망까지 못 간다). 60초쯤 뒤 종료.

```bash
grep -n "Player died\|All .* players dead\|EndGame\|Client_NotifyDeath" Saved/Logs/RE_85_t2.log
```

기대: `[RE] Player died 1/1` → `[RE] All 1 players dead` → `[RE] EndGame: DEFEAT`. `Client_NotifyDeath` 도 찍힌다(싱글은 로컬 실행).

- [ ] **Step 8: 커밋**

```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/RECharacterBase.cpp
git commit -m "feat(coop): 전원 사망 판정 + 사망자 입력 차단 (#85)"
```

---

### Task 3: 결과 화면 전 클라 전파 + 접속 종료 정리

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h`
- Modify: `Source/Project_RE/Core/REGameMode.cpp`

**Interfaces:**
- Consumes: `ReadyPlayers` / `DeadPlayers` (Task 1·2)

- [ ] **Step 1: EndGame의 첫 PC 고정을 순회로 교체**

`REGameMode.cpp` `EndGame` 안의 아래 블록을

```cpp
	APlayerController* PC = GetWorld()->GetFirstPlayerController();

	// 2) 결과 화면 + 입력 차단 — 오너 클라 실행(싱글은 로컬 즉시).
	if (AREPlayerController* REPC = Cast<AREPlayerController>(PC))
	{
		REPC->Client_ShowResult(bVictory);
	}
```

아래로 대체한다:

```cpp
	// 2) 결과 화면 + 입력 차단 — 전 클라에 보낸다 (#85).
	//    이미 죽어서 입력이 차단된 플레이어도 결과 화면은 받아야 하므로 필터하지 않는다.
	//    FConstPlayerControllerIterator는 약참조를 주므로 역참조 전에 유효성을 본다.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			continue;
		}
		if (AREPlayerController* REPC = Cast<AREPlayerController>(It->Get()))
		{
			REPC->Client_ShowResult(bVictory);
		}
	}
```

- [ ] **Step 2: 헤더 — Logout 오버라이드 선언**

`REGameMode.h` 의 `protected:` 블록(`BeginPlay` 선언 옆)에 추가:

```cpp
	virtual void Logout(AController* Exiting) override;
```

- [ ] **Step 3: cpp — Logout 구현**

`NotifyPlayerDied` **아래**에 추가:

```cpp
void AREGameMode::Logout(AController* Exiting)
{
	if (APlayerController* PC = Cast<APlayerController>(Exiting))
	{
		ReadyPlayers.Remove(PC);
		DeadPlayers.Remove(PC);

		UE_LOG(LogTemp, Log, TEXT("[RE] Player left — ready=%d dead=%d"),
			ReadyPlayers.Num(), DeadPlayers.Num());

		// 분모가 줄었으니 지금이 종료 시점일 수 있다. 남은 사람이 이미 다 죽어 있던 경우다.
		// ReadyPlayers.Num() > 0 가드가 없으면 마지막 한 명이 나갈 때 0 >= 0 으로 DEFEAT가 떠서
		// 받을 클라도 없는 상태로 게임이 끝난 것으로 기록된다.
		if (!bGameOver && ReadyPlayers.Num() > 0 && DeadPlayers.Num() >= ReadyPlayers.Num())
		{
			EndGame(/*bVictory=*/false);
		}
	}
	Super::Logout(Exiting);
}
```

- [ ] **Step 4: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 5: 싱글 회귀 — 결과 화면이 여전히 뜬다**

Task 2 Step 7과 같은 명령으로 로그를 새로 받고:

```bash
grep -n "EndGame\|Client_ShowResult" Saved/Logs/RE_85_t3.log
```

기대: `[RE] EndGame: DEFEAT` 뒤에 `[RE] Client_ShowResult: DEFEAT` 가 **1회** 찍힌다(1인이므로 순회해도 1회).

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(coop): 결과 화면 전 클라 전파 + 접속 종료 시 집합 정리 (#85)"
```

---

### Task 4: 최근접 생존자 조준

보스가 첫 플레이어만 노린다. 최근접 **생존자**로 바꾼다 — 사망자를 빼지 않으면 시체를 조준한다.

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`
- Modify: `Source/Project_RE/Core/REBossCharacter.h`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp`

**Interfaces:**
- Produces: `ARECharacterBase::IsAlive() const` — public
- Produces: `AREBossCharacter::FindNearestLivingPlayerPawn() const` — private

- [ ] **Step 1: 생존 여부 공개 접근자**

`Health` 와 `bIsDead` 는 `RECharacterBase.h` 의 `protected:` 라 보스가 못 읽는다. `public:` 블록의 `TryDash` 선언 **아래**에 추가:

```cpp
	/** 생존 여부 (#85 보스 타깃 선택). bIsDead는 서버 전용이라 서버에서만 의미 있다. */
	bool IsAlive() const { return !bIsDead; }
```

- [ ] **Step 2: 보스 헤더 — 헬퍼 선언**

`REBossCharacter.h` 의 `private:` 블록, `FireArtillery()` 선언 **위**에 추가:

```cpp
	/**
	 *  최근접 생존 플레이어 폰 (#85). 없으면 nullptr.
	 *  서버 결정 경로에서만 부른다 — 결과는 RPC 페이로드로 나가므로 RPC 계약은 안 바뀐다.
	 */
	const APawn* FindNearestLivingPlayerPawn() const;
```

- [ ] **Step 3: 보스 cpp — include 확인 + 헬퍼 구현**

`REBossCharacter.cpp` 의 include 블록에 `#include "RECharacterBase.h"` 가 없으면 추가한다.

`FireArtillery` **위**에 추가:

```cpp
const APawn* AREBossCharacter::FindNearestLivingPlayerPawn() const
{
	const UWorld* W = GetWorld();
	if (!W)
	{
		return nullptr;
	}
	const FVector BossLoc = GetActorLocation();
	const APawn* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			continue;
		}
		const ARECharacterBase* P = Cast<ARECharacterBase>(It->Get()->GetPawn());
		if (!P || !P->IsAlive())
		{
			continue;   // 사망자를 빼지 않으면 시체를 조준한다
		}
		const float D = FVector::DistSquared2D(P->GetActorLocation(), BossLoc);
		if (D < BestDistSq)
		{
			BestDistSq = D;
			Best = P;
		}
	}
	return Best;
}
```

- [ ] **Step 4: Fan 조준 교체**

`FireCurrentPattern` 의 `else   // Fan` 블록에서 아래 부분을

```cpp
		// TODO: 멀티는 타깃 선택 정책 필요 — 지금은 첫 플레이어 고정 (#85).
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Target = PC->GetPawn())
			{
				const FVector D = Target->GetActorLocation() - GetActorLocation();
				AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			}
		}
```

아래로 대체한다(위 두 줄의 주석 `// 플레이어 방향 조준...` 과 `// 클라는 이 각을...` 은 그대로 둔다):

```cpp
		// 최근접 생존자를 조준한다 (#85). 전원 사망이면 0° 폴백 — 곧 EndGame이 발사를 끊는다.
		if (const APawn* Target = FindNearestLivingPlayerPawn())
		{
			const FVector D = Target->GetActorLocation() - GetActorLocation();
			AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
		}
```

- [ ] **Step 5: Artillery 조준 교체**

`FireArtillery` 에서 아래 부분을

```cpp
	// TODO: 멀티는 타깃 선택 정책 필요 — 지금은 첫 플레이어 고정 (#85).
	FVector PlayerLoc = BossLoc + FVector(300.f, 0.f, 0.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* P = PC->GetPawn())
		{
			PlayerLoc = P->GetActorLocation();
		}
	}
```

아래로 대체한다:

```cpp
	// 최근접 생존자를 조준한다 (#85). 전원 사망이면 보스 앞쪽 폴백.
	FVector PlayerLoc = BossLoc + FVector(300.f, 0.f, 0.f);
	if (const APawn* P = FindNearestLivingPlayerPawn())
	{
		PlayerLoc = P->GetActorLocation();
	}
```

- [ ] **Step 6: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 7: 싱글 회귀 프로브**

1인이면 최근접 생존자 = 그 사람이므로 조준이 종전과 같아야 한다.

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_t4.log"
```

```bash
grep -n "Boss FireDirect" Saved/Logs/RE_85_t4.log | head -5
```

기대: `Boss FireDirect` 가 나오고 `Angle=` 이 0이 아닌 값을 포함하는 줄이 있다(Fan 페이즈에서 플레이어를 조준했다는 뜻). 전 줄이 `Angle=0.0` 이면 조준이 폴백만 타고 있는 것이다.

- [ ] **Step 8: 커밋**

```bash
git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(coop): 보스 조준을 최근접 생존자로 전환 (#85)"
```

---

### Task 5: 스폰 이격

맵의 `PlayerStart` 가 `PlayerStart_0` 하나뿐이라 N인이면 같은 자리에 스폰된다. #54가 정확히 그 겹침 즉사 버그였다.

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h`
- Modify: `Source/Project_RE/Core/REGameMode.cpp`

**Interfaces:**
- Consumes: CVar `re.Coop.ExpectedPlayers` (Task 1)

- [ ] **Step 1: 헤더 — 오버라이드와 상태**

`REGameMode.h` 의 `protected:` 블록에 추가:

```cpp
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer,
	                                                          const FTransform& SpawnTransform) override;
```

`private:` 의 `DeadPlayers` **아래**에 추가:

```cpp
	/**
	 *  스폰된 폰 수 = 다음 스폰의 오프셋 인덱스 (#85).
	 *  감소시키지 않는다 — 나갔다 들어오면 오프셋이 바깥으로 밀리지만, 감소시키면 두 플레이어가
	 *  같은 인덱스를 받아 겹칠 수 있다. 겹침이 드리프트보다 나쁘다(#54).
	 */
	int32 SpawnedPawnCount = 0;
	/** 플레이어 간 이격 거리(uu). 캡슐 반경 대비 넉넉히. */
	static constexpr float SpawnSpacing = 250.f;
```

- [ ] **Step 2: cpp — 구현**

`Logout` **아래**에 추가:

```cpp
APawn* AREGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer,
                                                               const FTransform& SpawnTransform)
{
	// 맵에 PlayerStart가 하나뿐이라 N인이면 같은 자리에 겹친다 — 인덱스별로 흩는다 (#85).
	// +Y인 이유(#56): 보스가 +X 600에 있어 +X로 흩으면 플레이어를 탄막 레인에 밀어넣는다.
	// 1인이면 Half=0, SpawnedPawnCount=0 → 오프셋이 정확히 0이라 싱글 스폰 좌표가 불변이다.
	const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
	const float Half = (Expected - 1) * 0.5f;
	const FVector Offset(0.f, (SpawnedPawnCount - Half) * SpawnSpacing, 0.f);
	++SpawnedPawnCount;

	FTransform Adjusted = SpawnTransform;
	Adjusted.AddToTranslation(Offset);

	UE_LOG(LogTemp, Log, TEXT("[RE] Spawn player idx=%d offsetY=%.0f loc=%s"),
		SpawnedPawnCount - 1, Offset.Y, *Adjusted.GetLocation().ToString());

	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Adjusted);
}
```

- [ ] **Step 3: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 4: 싱글 회귀 — 좌표 불변 확인**

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_t5.log"
```

```bash
grep -n "Spawn player\|Move. probe start" Saved/Logs/RE_85_t5.log | head -5
```

기대: `[RE] Spawn player idx=0 offsetY=0 loc=...` — **`offsetY=0` 이어야 한다.** 0이 아니면 1인 회귀가 깨진 것이다.
이어서 `[Move] probe start: pawn=...` 의 좌표가 종전 로그와 같은지 대조한다(헤드리스 프로브가 스폰 직후 폰 위치를 찍는다).

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(coop): N인 스폰 인덱스별 +Y 이격 (#85)"
```

---

### Task 6: 2인 데디 실측

여기서 처음으로 실제 목표를 확인한다. `scripts/dedi-verify.ps1` 은 `-Clients 2` 가 반쪽이므로(#87) 수동 페어로 검증한다.

**Files:** 커밋할 코드 없음. 관측 전용.

- [ ] **Step 1: 재쿡**

Global Constraints의 `BuildCookRun` 실행. **빼먹으면 옛 산출물을 검증한다.**

- [ ] **Step 2: 1인 회귀 — dedi-verify 통과**

```powershell
scripts\dedi-verify.ps1
```

기대: 12항목 전부 PASS, `EXIT=0`. 여기가 깨지면 2인을 볼 것도 없다.

- [ ] **Step 3: 2인 페어 기동**

**서버에 `-unattended` 를 주지 않는다** — 주면 첫 클라의 서버측 프로브가 약 4.4초에 `RequestExit` 으로 서버를 내린다.

```powershell
$stage = "E:\UnrealProjects\Project_RE\Saved\StagedBuilds\WindowsServer\Project_RE"
$srv = Start-Process "$stage\Binaries\Win64\Project_REServer.exe" -PassThru -WindowStyle Hidden `
  -ArgumentList "-log","-port=7777","-ExecCmds=`"re.Coop.ExpectedPlayers 2`"","-abslog=`"E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_srv.log`""
Start-Sleep -Seconds 20
```

서버 로그에 `IpNetDriver listening` 이 뜬 뒤 클라 1을 붙인다:

```powershell
$c1 = Start-Process "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -PassThru -WindowStyle Hidden `
  -ArgumentList "`"E:\UnrealProjects\Project_RE\Project_RE.uproject`"","127.0.0.1:7777","-game","-nullrhi","-log","-abslog=`"E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_c1.log`""
Start-Sleep -Seconds 15
```

- [ ] **Step 4: 게이트 확인 — 클라 1만으로는 발사가 시작되지 않는다**

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_srv.log" -Pattern "Player ready|Boss firing started"
```

기대: `[RE] Player ready 1/2` 만 있고 **`Boss firing started` 는 없다.**
있으면 게이트가 인원을 안 세는 것이다 — CVar가 서버에 전달됐는지(`-ExecCmds`)부터 확인한다.

- [ ] **Step 5: 클라 2 접속 → 발사 시작 + 스폰 이격 확인**

```powershell
$c2 = Start-Process "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" -PassThru -WindowStyle Hidden `
  -ArgumentList "`"E:\UnrealProjects\Project_RE\Project_RE.uproject`"","127.0.0.1:7777","-game","-nullrhi","-log","-abslog=`"E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_c2.log`""
Start-Sleep -Seconds 30
```

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_srv.log" -Pattern "Player ready|Boss firing started|Spawn player"
```

기대:
- `[RE] Player ready 2/2` 직후 `[RE] Boss firing started (2/2 ready)`
- `Spawn player idx=0 offsetY=-125` 와 `idx=1 offsetY=125` — **두 좌표가 다르다**

- [ ] **Step 6: 두 클라 모두 탄막 수신**

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_c1.log" -Pattern "Boss Fire.*ROLE_SimulatedProxy" | Select-Object -First 2
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_c2.log" -Pattern "Boss Fire.*ROLE_SimulatedProxy" | Select-Object -First 2
```

기대: 양쪽 다 나온다.

- [ ] **Step 7: 전원 사망 판정 + 결과 화면 전파**

두 클라가 정지 상태라 탄막에 맞아 순차적으로 죽는다. 충분히 기다린 뒤(플레이어 100HP 기준 수십 초):

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_srv.log" -Pattern "Player died|All .* players dead|EndGame"
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_c1.log" -Pattern "Client_NotifyDeath|Client_ShowResult"
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_c2.log" -Pattern "Client_NotifyDeath|Client_ShowResult"
```

기대:
- 서버: `Player died 1/2` (게임 계속) → `Player died 2/2` → `All 2 players dead` → `EndGame: DEFEAT`
- **두 클라 모두** `Client_NotifyDeath` 와 `Client_ShowResult: DEFEAT` 를 받는다

`Player died 1/2` 직후 바로 `EndGame` 이 뜨면 전원 사망 판정이 안 걸린 것이다.

- [ ] **Step 8: NavMesh 확인 — 오프셋 지점에서 이동이 되는가**

오프셋 스폰 지점이 NavMesh 밖이면 `Server_RequestMove` 가 거부돼 이동이 통째로 죽는다.

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_srv.log" -Pattern "Move. rejected: off-navmesh"
```

기대: **0건.** 걸리면 `SpawnSpacing` 을 줄이거나 오프셋 방향을 조정해야 한다.

- [ ] **Step 8b: 타깃 전환 관측 — 조준이 최근접을 따라가는가**

두 플레이어는 `SpawnSpacing` 만큼 Y로 떨어져 있고 보스는 +X 600에 있으므로, 보스까지의 거리가 서로 다르다. Fan 페이즈의 조준각이 그 차이를 반영해야 한다.

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_srv.log" -Pattern "Boss FireDirect: Pattern=1" | Select-Object -First 6
```

`Pattern=1` 이 Fan이다. 기대: `Angle=` 이 **0이 아니고**, 한 명이 죽은 뒤 찍힌 줄의 각도가 그 전과 **달라진다**(남은 생존자 쪽으로 조준이 옮겨간 것). 사망 전후로 각도가 동일하면 사망자를 여전히 조준하고 있는 것이다.

Fan 페이즈가 로그에 안 잡히면(로테이션이 랜덤이라 가능) 관측 창을 늘려 재실행한다.

- [ ] **Step 9: 프로세스 정리**

```powershell
foreach ($p in @($srv,$c1,$c2)) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
```

- [ ] **Step 10: 접속 종료 처리 확인**

클라 2를 먼저 죽이고(강제 종료) 남은 클라 1이 죽었을 때 게임이 끝나는지 본다. Step 3~5를 다시 수행해 2인을 붙인 뒤, 클라 2 프로세스만 `Stop-Process` 로 종료하고 기다린다.

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_85_srv.log" -Pattern "Player left|All .* players dead|EndGame"
```

기대: `[RE] Player left — ready=1 dead=...` 가 찍히고, 남은 한 명이 죽으면 `EndGame: DEFEAT` 가 뜬다.
**게임이 영영 안 끝나면 `Logout` 정리가 동작하지 않는 것이다** — 이 플랜이 막으려던 바로 그 교착이다.

- [ ] **Step 11: 가이드 갱신**

`docs/guides/dedicated-server.md` 의 `-Clients 2` 천장 문단 옆에 2인 수동 검증 절차를 추가한다. 반드시 담을 것 네 가지:

1. 서버에 `-ExecCmds="re.Coop.ExpectedPlayers 2"` 를 준다 — CVar라서 재쿡 없이 인원을 바꿀 수 있다는 점까지.
2. **서버에서 `-unattended` 를 뺀다** — 주면 첫 클라의 서버측 프로브가 약 4.4초에 `RequestExit` 으로 서버를 내린다.
3. 확인할 판정 4개: 클라 1만으로는 발사가 시작되지 않을 것 / 스폰 `offsetY` 가 서로 다를 것 / 두 클라 모두 `ROLE_SimulatedProxy` 탄막을 받을 것 / 전원 사망 시 두 클라 모두 `Client_ShowResult` 를 받을 것.
4. 이 절차의 자동화는 #87의 몫이라는 것.

```bash
git add docs/guides/dedicated-server.md
git commit -m "docs(coop): 2인 데디 수동 검증 절차 추가 (#85)"
```

---

## 완료 후

### PR

```bash
git push -u origin feature/M5-coop-nplayers
```

PR 규칙: **base=dev**, 이슈 #85 메타 미러링 — label `enhancement`,`networking`,`C++` / milestone `M5: 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격` / assignee `leejimin3` / project `Project_RE 개발 로드맵`. **Reviewer 생략.** 본문 6개 필드:

1. 요약
2. 변경사항
3. 이슈링크 (`Closes #85` — dev 머지로는 자동 종료가 안 되므로 머지 후 `gh issue close 85` 수동)
4. 검증 — Task 6의 서버/클라 로그 발췌
5. 스코프 제외
6. 참고

### 남는 의도된 TODO (후속 이슈 몫 — 건드리지 말 것)

- `Mass/REBulletHitProcessor.cpp:51`, `Mass/REArcHitProcessor.cpp:35` 의 `GetPlayerPawn(World, 0)` → **#86**
- `scripts/dedi-verify.ps1` 의 `-Clients 2` 반쪽 판정 → **#87**

## 하지 말 것 (스코프 밖)

- **리스폰 / 부활** — M2에서 이미 제외됐다
- **대기 로비 UI / "1/2 접속" 화면** — 로그로 충분하다
- **별도 관전 카메라** — 사망 폰에 붙은 카메라가 그대로 관전 시점이다
- **`AREGameState`** — 복제 상태가 필요 없다는 것이 설계 결론이다
- **`Variant_Combat/AI/EnvQueryContext_Player.cpp` 의 `GetPlayerPawn(0)`** — 참조 0건 엔진 템플릿 잔재. 선재 죽은 코드라 건드리지 않는다
- **`Build.cs` / `.uproject` 수정** — 필요 없다
- **밸런스 조정** — `Config/DefaultGame.ini` 값은 건드리지 않는다
