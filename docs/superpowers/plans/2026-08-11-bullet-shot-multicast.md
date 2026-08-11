# 보스 탄막 서버→클라 동기화 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 데디 클라 화면에 보스 탄막이 서버와 수 cm 이내로 일치하는 궤도로 보이게 한다 (#84).

**Architecture:** 서버가 발사 1회의 **생성기 입력**을 Reliable Multicast로 보내고, 서버도 자기 Multicast 구현체를 통해 스폰한다(로컬 직접 호출을 대체). 양쪽이 같은 코드를 타므로 궤도 불일치의 여지가 구조적으로 없다. 클라는 `ServerTime` 기준 경과분만큼 앞당겨 스폰해 RPC 지연을 상쇄한다. 시드는 전송하지 않는다 — `PhaseRng`는 서버 전용 상태로 남는다.

**Tech Stack:** UE 5.8, C++ (NetMulticast RPC, `FVector_NetQuantize`, `AGameStateBase::GetServerWorldTimeSeconds`, Mass Entity).

설계 스펙: `docs/superpowers/specs/2026-08-11-bullet-shot-multicast-design.md`

## Global Constraints

- 브랜치: `feature/M5-bullet-sync` (이미 생성됨, 스펙 커밋 2건 있음)
- ⚠️ **이 계획은 본체 작업트리(`E:\UnrealProjects\Project_RE`)에서 실행하는 것을 전제한다.** 아래 명령들의 `-Project` / `-project` 가 절대경로로 박혀 있다. 격리 worktree에서 그대로 돌리면 **worktree가 아니라 본체를 빌드하고** 캐시 때문에 수 초 만에 `Succeeded`가 떠서, 검증이 통과한 것처럼 보이지만 실제로는 아무것도 검증하지 않는다. 다른 트리에서 실행하려면 경로를 전부 그 트리 기준으로 바꿔라.
- 빌드 게이트 — **Editor와 Server 타겟 둘 다** 통과해야 한다:
  ```powershell
  $BB = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat"
  $UP = "E:\UnrealProjects\Project_RE\Project_RE.uproject"
  & $BB Project_REEditor Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  & $BB Project_REServer Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  ```
  기대: 양쪽 `Result: Succeeded`.
- **에디터가 켜져 있으면 빌드가 실패한다** — `Unable to build while Live Coding is active`. 헤더를 건드리는 변경은 Live Coding 패치로 안 붙으므로 에디터를 닫고 빌드한다.
- **데디 검증 전에는 재쿡이 필요하다.** 스테이징 `Config`는 pak 안에 들어간다:
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
    -project="$UP" -noP4 -platform=Win64 -server -noclient -serverconfig=Development `
    -cook -stage -pak -skipbuild -utf8output
  ```
  `-skipbuild`를 빼먹지 마라(이미 빌드한 걸 다시 빌드한다).
- 자동화 테스트 인프라 없음 → 게이트 = 빌드 + 헤드리스 로그 프로브 + 실RHI 스크린샷 + 데디 2프로세스.
- 로그 접두어 `[RE]` 고정.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- `Project_RE.uproject`는 **절대 스테이징하지 않는다** — `EngineAssociation` GUID가 머신 종속이다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- YAGNI: 스펙에 없는 기능·추상화·설정값 추가 금지.

## File Structure

| 파일 | 책임 | 변경 |
|---|---|---|
| `Mass/REBulletPatternGenerator.h` | `FArcBulletSpawnParams`에 `Elapsed` 필드 | 수정 (1줄) |
| `Mass/REBulletSpawnSubsystem.h` / `.cpp` | 곡사탄 스폰 시 `Elapsed` 초기값 전달 | 수정 |
| `Core/REBossCharacter.h` / `.cpp` | 발사 결정(서버) / 발사 실행(Multicast, 양쪽) 분리 | 수정 — 이 이슈의 본체 |
| `Core/REPlayerController.h` / `.cpp` | `Server_NotifyReady` 준비 통지 | 수정 |
| `Core/REGameMode.h` / `.cpp` | 준비 신호 수신 후 발사 시작 | 수정 |
| `scripts/dedi-verify.ps1` | 클라측 스폰 판정 추가 | 수정 |
| `docs/guides/dedicated-server.md` | 판정 항목 표 갱신 | 수정 |

새 파일 없음. `REBossCharacter.cpp`가 커지므로 서버 전용 결정 로직(Spiral 클로즈드루프)은 private 헬퍼로 분리한다.

---

### Task 1: 곡사탄 스폰에 Elapsed 전달 경로

시간 보정값을 곡사탄에 넣을 진입점이 없다. `FArcBulletFragment.Elapsed`는 이미 존재하지만(`REBulletFragments.h:43`) 스폰 함수가 인자로 받지 않는다.

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletPatternGenerator.h:56-64`
- Modify: `Source/Project_RE/Mass/REBulletSpawnSubsystem.h`, `Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp:98-104`

**Interfaces:**
- Produces: `REBulletPattern::FArcBulletSpawnParams::Elapsed` (float, 기본 0). Task 3이 소비한다.

- [ ] **Step 1: `FArcBulletSpawnParams`에 필드 추가**

`REBulletPatternGenerator.h`의 구조체 끝(`float Radius = 120.f;` 아래)에:

```cpp
		/** 스폰 시점의 비행 경과초 (#84 지연 보정). 0 = 갓 발사됨 — 기존 호출부는 무변경. */
		float   Elapsed    = 0.f;
```

- [ ] **Step 2: `SpawnArcBullet` 시그니처에 인자 추가**

`REBulletSpawnSubsystem.h`의 선언과 `.cpp`의 정의 양쪽에 마지막 인자로 `float InElapsed = 0.f` 를 추가하고, 프래그먼트 초기화에 `Arc.Elapsed = InElapsed;` 를 넣는다. 기본값이 있으므로 기존 호출부는 컴파일이 유지된다.

- [ ] **Step 3: `SpawnArcBulletBatch`가 전달하도록 수정**

`REBulletSpawnSubsystem.cpp:98-104`:

```cpp
void UREBulletSpawnSubsystem::SpawnArcBulletBatch(TConstArrayView<REBulletPattern::FArcBulletSpawnParams> Params)
{
	for (const REBulletPattern::FArcBulletSpawnParams& P : Params)
	{
		SpawnArcBullet(P.Start, P.Target, P.FlightTime, P.MaxHeight, P.Damage, P.Radius, P.Elapsed);
	}
}
```

- [ ] **Step 4: 빌드 게이트**

Editor 타겟만으로 충분하다(이 태스크는 서버 전용 코드가 아니다).
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletSpawnSubsystem.h Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp
git commit -m "feat(mass): 곡사탄 스폰에 비행 경과초 전달 경로 추가 (#84)"
```

---

### Task 2: 직선탄 Multicast (Spiral / Fan)

`TriggerBulletPattern`은 호출자가 `FireCurrentPattern`(`REBossCharacter.cpp:145`) 하나뿐이다. 따라서 시그니처를 교체해도 외부 파급이 없고, 죽어 있던 `Seed`/`StartTime` 인자도 같이 정리된다.

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.h:25-34, 59-77`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp:138-146, 226-337`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `AREBossCharacter::Multicast_FireDirect(EBulletPattern, FVector_NetQuantize, float AngleDeg, int32 Count, float ServerTime)`
  - `float AREBossCharacter::GetServerNow() const`
  - `float AREBossCharacter::GetElapsedSince(float ServerTime) const`
  - Task 3이 `GetServerNow` / `GetElapsedSince`를 재사용한다.

- [ ] **Step 1: 헤더 — 선언 교체**

`REBossCharacter.h`에서 `TriggerBulletPattern` 선언(`:25-29`)을 **삭제**하고 그 자리에:

```cpp
	/**
	 *  직선탄(Spiral/Fan) 1회 발사 (#84). 서버가 결정한 생성기 입력을 브로드캐스트하고
	 *  서버·클라가 이 같은 구현체에서 같은 탄을 만든다 — 서버도 로컬 실행되므로 직접 스폰 경로가 없다.
	 *  Reliable: 유실되면 그 발사분이 클라에 영영 안 보인다(회피 게임에서 치명적).
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_FireDirect(EBulletPattern Pattern, FVector_NetQuantize Origin,
	                          float AngleDeg, int32 Count, float ServerTime);
```

`StartFiring` 주석(`:31`)을 갱신 — 시드는 이제 네트워크에 안 나간다:

```cpp
	/** 페이즈 로테이션 발사 시작. Seed는 서버 전용 PhaseRng 초기화용 — 네트워크 미전송 (#84). */
	void StartFiring(int32 Seed);
```

`private:` 블록(`SpiralShotCount` 아래)에 헬퍼 3개 선언:

```cpp
	/** 서버 기준 현재 시각. GameState 미준비면 0. */
	float GetServerNow() const;
	/** ServerTime 이후 경과초(음수 클램프). 서버에서는 ≈0이라 보정이 자연히 무효화된다. */
	float GetElapsedSince(float ServerTime) const;
	/** Spiral 발사당 탄 수. 클로즈드루프는 서버 ISM 상태에 의존 — 서버 전용 결정. */
	int32 ResolveSpiralCount();
```

- [ ] **Step 2: cpp — include 추가**

`REBossCharacter.cpp` include 블록에:

```cpp
#include "GameFramework/GameStateBase.h"
```

- [ ] **Step 3: 시간 헬퍼 2개 구현**

`EndPhase`(`:218-224`) 아래에 추가:

```cpp
float AREBossCharacter::GetServerNow() const
{
	const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return GS ? GS->GetServerWorldTimeSeconds() : 0.f;
}

float AREBossCharacter::GetElapsedSince(float ServerTime) const
{
	// 접속 직후 GameState 복제 전이면 GetServerWorldTimeSeconds가 0을 반환할 수 있다 →
	// ServerTime을 그대로 빼면 큰 음수가 나오므로 Max로 막는다(보정 없음으로 폴백).
	return FMath::Max(0.f, GetServerNow() - ServerTime);
}
```

- [ ] **Step 4: Spiral 발사 수 결정을 헬퍼로 분리**

`TriggerBulletPattern`의 `case EBulletPattern::Spiral:` 안에 있는 발사 수 결정 블록을 **한 글자도 바꾸지 말고** 새 함수로 옮긴다. 이 블록은 클로즈드루프 적분 제어(#51)와 와인드업 방지 로직이라 재작성하면 M3 측정 하네스가 깨진다.

**옮길 범위** — 아래 두 줄 사이(양 끝 포함)의 전부:

- 시작: `const int32 CVarCount = CVarBulletCount.GetValueOnGameThread();` (현재 `:251`)
- 끝: `[RE] Boss Spiral: Target=%d Live=%d Rate=%.1f` 로그를 닫는 `}` (현재 `:302`)

옮긴 뒤 함수 껍데기만 씌운다:

```cpp
int32 AREBossCharacter::ResolveSpiralCount()
{
	// ── 여기에 위 범위를 그대로 붙여넣는다. 로직·주석·CVar 이름·로그 문구 전부 무변경. ──
	return Count;
}
```

붙여넣은 블록은 `int32 Count;` 를 선언하고 두 분기에서 대입하므로 `return Count;` 가 그대로 성립한다. `SpiralSpawnRate` / `SpiralSpawnAccum` / `SpiralShotCount` 멤버를 변형하는데, 이 함수는 **서버에서만** 불리므로 클라 상태와 무관하다.

- [ ] **Step 5: `FireCurrentPattern`을 서버 전용 결정부로 교체**

`REBossCharacter.cpp:138-146`을 아래로 대체:

```cpp
void AREBossCharacter::FireCurrentPattern()
{
	if (bIsDead)
	{
		return;
	}
	if (CurrentPhasePattern == EBulletPattern::Artillery)
	{
		FireArtillery();
		return;
	}
	if (CurrentPhasePattern == EBulletPattern::Homing)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss: Homing 미구현 (#67)"));
		return;
	}

	// 여기까지가 서버 전용 결정이다. 클라는 로테이션도 PhaseRng도 돌리지 않는다 (#84).
	float AngleDeg = 0.f;
	int32 Count    = 0;

	if (CurrentPhasePattern == EBulletPattern::Spiral)
	{
		Count    = ResolveSpiralCount();
		AngleDeg = SpiralBaseAngleDeg;
		SpiralBaseAngleDeg += SpiralRotationStepDeg;   // 다음 발사에 회전
	}
	else   // Fan
	{
		Count = REBulletPattern::FFanParams().Count;
		// 플레이어 방향 조준. 폰 없으면 0°(기존 기본) 폴백.
		// 클라는 이 각을 유도할 수 없다(복제 위치가 서버와 다름) → 페이로드로 보낸다.
		// TODO: 멀티는 타깃 선택 정책 필요 — 지금은 첫 플레이어 고정 (#85).
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Target = PC->GetPawn())
			{
				const FVector D = Target->GetActorLocation() - GetActorLocation();
				AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			}
		}
	}

	Multicast_FireDirect(CurrentPhasePattern, GetActorLocation(), AngleDeg, Count, GetServerNow());
}
```

- [ ] **Step 6: `TriggerBulletPattern`을 Multicast 구현체로 교체**

`REBossCharacter.cpp:226-337` 전체를 아래로 대체:

```cpp
void AREBossCharacter::Multicast_FireDirect_Implementation(EBulletPattern Pattern, FVector_NetQuantize Origin,
                                                           float AngleDeg, int32 Count, float ServerTime)
{
	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::FireDirect: UREBulletSpawnSubsystem NULL"));
		return;
	}

	// Origin을 쓴다 — GetActorLocation()이 아니다. 클라의 보스 위치는 복제 지연으로 서버와 다를 수 있다.
	TArray<FBulletSpawnParams> Params;
	if (Pattern == EBulletPattern::Spiral)
	{
		const REBulletPattern::FSpiralParams SP = REBulletPattern::MakeSpiralRing(Count, AngleDeg);
		Params = REBulletPattern::GenerateSpiral(Origin, SP);
	}
	else if (Pattern == EBulletPattern::Fan)
	{
		REBulletPattern::FFanParams FP;
		FP.Count          = Count;
		FP.CenterAngleDeg = AngleDeg;
		Params = REBulletPattern::GenerateFan(Origin, FP);
	}
	else
	{
		return;
	}

	// 지연 보정. 서버는 발사 시각이 곧 현재라 Elapsed≈0 → 같은 코드가 무보정으로 동작한다.
	const float Elapsed = GetElapsedSince(ServerTime);
	if (Elapsed > 0.f)
	{
		for (int32 i = Params.Num() - 1; i >= 0; --i)
		{
			if (Elapsed >= Params[i].Lifetime)
			{
				Params.RemoveAtSwap(i);   // 이미 수명이 다한 탄 — 스폰하지 않는다
				continue;
			}
			Params[i].Location += Params[i].Velocity * Elapsed;
			Params[i].Lifetime -= Elapsed;
		}
	}

	Spawner->SpawnBulletBatch(Params);

	// role이 판정의 핵심 신호다 — 서버=ROLE_Authority, 클라=ROLE_SimulatedProxy 양쪽에 찍혀야 한다.
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireDirect: Pattern=%d Angle=%.1f N=%d Elapsed=%.3f role=%s"),
		(int32)Pattern, AngleDeg, Params.Num(), Elapsed, *UEnum::GetValueAsString(GetLocalRole()));
}
```

- [ ] **Step 7: 빌드 게이트 (Editor + Server 둘 다)**

기대: 양쪽 `Result: Succeeded`, 에러 0.

- [ ] **Step 8: 싱글 회귀 프로브 — Multicast 로컬 실행 전제 확인**

이 설계는 "넷드라이버 없는 월드에서도 NetMulticast 구현체가 로컬 실행된다"를 전제한다. 깨지면 싱글에서 탄이 0발이 된다.

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_84_single.log"
```

```bash
grep -c "Boss FireDirect" Saved/Logs/RE_84_single.log
grep -n "Boss FireDirect" Saved/Logs/RE_84_single.log | head -3
```

기대: 카운트 > 0, `role=ROLE_Authority`, `Elapsed=0.000`, `N=`이 종전 발수(`BulletsPerShot=16`)와 일치.
**카운트가 0이면 전제가 깨진 것이다** — superpowers:systematic-debugging으로 원인 규명. 로컬 실행이 안 되면 서버 경로만 직접 호출로 되돌리고 클라만 Multicast로 받는 구조로 설계를 수정해야 한다(스펙 갱신 필요).

- [ ] **Step 9: 커밋**

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(net): 직선탄 발사를 Multicast로 전환, 시드 대신 생성기 입력 전송 (#84)"
```

---

### Task 3: 곡사탄 Multicast (Artillery)

`FireArtillery`(`REBossCharacter.cpp:148-216`)를 결정부(서버)와 실행부(Multicast)로 가른다. 착지점 생성기 6종 중 `Line`/`PlayerAimed`는 플레이어 위치를, `Random`은 RNG를 먹으므로 둘 다 페이로드로 넘긴다.

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.h`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp:148-216`

**Interfaces:**
- Consumes: `GetServerNow()` / `GetElapsedSince()` (Task 2), `FArcBulletSpawnParams::Elapsed` (Task 1)
- Produces: `AREBossCharacter::Multicast_FireArtillery(EArtilleryShape, FVector_NetQuantize Origin, FVector_NetQuantize AimLoc, int32 CallSeed, float ServerTime)`

- [ ] **Step 1: 헤더 — RPC 선언 추가**

`Multicast_FireDirect` 선언 아래에:

```cpp
	/**
	 *  곡사탄(Artillery) 1회 일제사 (#84). Line/PlayerAimed가 먹는 조준점과
	 *  Random이 먹는 시드를 서버가 정해 보낸다 — 클라는 PhaseRng를 돌리지 않는다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_FireArtillery(EArtilleryShape Shape, FVector_NetQuantize Origin,
	                             FVector_NetQuantize AimLoc, int32 CallSeed, float ServerTime);
```

- [ ] **Step 2: `FireArtillery`를 서버 전용 결정부로 교체**

`REBossCharacter.cpp:148-216`을 아래로 대체:

```cpp
void AREBossCharacter::FireArtillery()
{
	if (bIsDead)
	{
		return;
	}

	const FVector BossLoc = GetActorLocation();

	// 조준점. 폰 없으면 보스 앞쪽 폴백. 클라는 이 값을 유도할 수 없다 → 페이로드로 보낸다.
	// TODO: 멀티는 타깃 선택 정책 필요 — 지금은 첫 플레이어 고정 (#85).
	FVector PlayerLoc = BossLoc + FVector(300.f, 0.f, 0.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* P = PC->GetPawn())
		{
			PlayerLoc = P->GetActorLocation();
		}
	}

	// Random 모양 전용 시드. 서버 스트림에서 1회 뽑아 넘긴다 —
	// 클라는 PhaseRng가 없으므로 이 시드로 로컬 스트림을 만들어 같은 착지점을 얻는다.
	const int32 CallSeed = (int32)PhaseRng.GetUnsignedInt();

	Multicast_FireArtillery(CurrentArtilleryShape, BossLoc, PlayerLoc, CallSeed, GetServerNow());
}
```

- [ ] **Step 3: Multicast 구현체 추가**

`FireArtillery` 아래에:

```cpp
void AREBossCharacter::Multicast_FireArtillery_Implementation(EArtilleryShape Shape, FVector_NetQuantize Origin,
                                                              FVector_NetQuantize AimLoc, int32 CallSeed, float ServerTime)
{
	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::FireArtillery: Spawner NULL"));
		return;
	}

	const FVector BossLoc  = Origin;
	const FVector PlayerLoc = AimLoc;
	const float   GroundZ  = BossLoc.Z + MarkerGroundOffset;   // 착지 평면(보스 캡슐 바닥 근사)

	// 서버가 넘긴 시드로 만든 로컬 스트림 — 양쪽이 같은 난수열을 본다.
	FRandomStream CallRng(CallSeed);

	TArray<FVector> Targets;
	switch (Shape)
	{
	case EArtilleryShape::Ring:
		Targets = REBulletPattern::GenRing(BossLoc, /*Radius=*/500.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::Line:
		Targets = REBulletPattern::GenLine(BossLoc, PlayerLoc, /*WallLen=*/900.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::Grid:
		Targets = REBulletPattern::GenGrid(BossLoc, /*ExtentX=*/600.f, /*ExtentY=*/600.f, /*Cols=*/4, /*Rows=*/3, GroundZ);
		break;
	case EArtilleryShape::Spiral:
		Targets = REBulletPattern::GenArcSpiral(BossLoc, /*MaxRadius=*/600.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::PlayerAimed:
		Targets = REBulletPattern::GenPlayerCluster(PlayerLoc, /*ClusterRadius=*/150.f, /*RingN=*/4, GroundZ);
		break;
	case EArtilleryShape::Random:
		Targets = REBulletPattern::GenRandom(BossLoc, /*ArenaRadius=*/800.f, ArtilleryCount, CallRng, GroundZ);
		break;
	}

	// 지연 보정 — 이미 착지한 탄은 스폰하지 않는다.
	const float Elapsed = GetElapsedSince(ServerTime);
	if (Elapsed >= ArtilleryFlightTime)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireArtillery: skipped (Elapsed=%.3f >= FlightTime=%.2f)"),
			Elapsed, ArtilleryFlightTime);
		return;
	}

	TArray<REBulletPattern::FArcBulletSpawnParams> Shots;
	Shots.Reserve(Targets.Num());
	for (const FVector& T : Targets)
	{
		REBulletPattern::FArcBulletSpawnParams P;
		P.Start      = BossLoc;
		P.Target     = T;
		P.FlightTime = ArtilleryFlightTime;
		P.MaxHeight  = ArtilleryMaxHeight;
		P.Damage     = ArtilleryDamage;
		P.Radius     = ArtilleryRadius;
		P.Elapsed    = Elapsed;
		Shots.Add(P);
	}
	Spawner->SpawnArcBulletBatch(Shots);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss FireArtillery: Shape=%d N=%d Elapsed=%.3f role=%s"),
		(int32)Shape, Shots.Num(), Elapsed, *UEnum::GetValueAsString(GetLocalRole()));
}
```

- [ ] **Step 4: 빌드 게이트 (Editor + Server 둘 다)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 5: 싱글 회귀 프로브 — Artillery 경로**

Task 2 Step 8과 같은 명령으로 로그를 새로 받되, Artillery 페이즈가 나올 때까지 충분히 돌린다(로테이션 랜덤이라 수십 초 필요할 수 있다). `-unattended`는 프로브가 ~4.4초에 프로세스를 종료시키므로 **빼고** 실행한다:

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_84_artillery.log"
```

60초쯤 뒤 종료(kill). 판정:

```bash
grep -n "Boss FireArtillery\|Boss Phase: Artillery" Saved/Logs/RE_84_artillery.log | head -10
```

기대: `Boss FireArtillery: Shape=... N=12 Elapsed=0.000 role=ROLE_Authority`. `N=0`이면 착지점 생성기가 실패한 것.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(net): 곡사탄 발사를 Multicast로 전환, 조준점·시드 페이로드화 (#84)"
```

---

### Task 4: 발사 시작 게이트

서버가 클라보다 먼저 뜬다. 접속까지 실측 15~90초가 걸리고 그동안 약 1600발이 쌓이는데(`BulletLifetime=15`, 초당 107탄), 그 탄들은 클라에 안 보인 채 플레이어를 때린다.

**Files:**
- Modify: `Source/Project_RE/Core/REPlayerController.h`, `.cpp`
- Modify: `Source/Project_RE/Core/REGameMode.h`, `Source/Project_RE/Core/REGameMode.cpp:75-82`

**Interfaces:**
- Consumes: `AREBossCharacter::StartFiring(int32)` (기존)
- Produces: `AREGameMode::NotifyPlayerReady()`, `AREPlayerController::Server_NotifyReady()`

- [ ] **Step 1: GameMode 헤더 — 진입점과 상태 추가**

`REGameMode.h`의 `public:`에 `EndGame` 선언 아래:

```cpp
	/** 클라 준비 통지 수신 (#84). 보스 발사 시작 조건을 재평가한다. */
	void NotifyPlayerReady();
```

`private:`의 `bGameOver` 아래:

```cpp
	/** 클라 준비 신호 도착 여부. #85에서 전원 입장 카운트로 대체될 자리. */
	bool bPlayerReady = false;
	/** 발사 시작 1회성 가드. */
	bool bFiringStarted = false;
	/** 준비 신호와 보스 스폰이 모두 끝났으면 발사 시작. 둘의 순서는 보장되지 않는다. */
	void TryStartBossFiring();
```

- [ ] **Step 2: GameMode cpp — 발사 시작을 BeginPlay에서 분리**

`REGameMode.cpp:75-82`의 보스 스폰 블록에서 `Boss->StartFiring(/*Seed=*/FMath::Rand());` 한 줄을 **삭제**한다(`DemoBoss = Boss;`는 남긴다). 그 자리 주석도 갱신:

```cpp
		// #64: 발사 주체를 Boss로 이관. 발사 시작은 클라 준비 후 (#84) — 여기서 켜지 않는다.
		DemoBoss = Boss;
```

`BeginPlay()` 본문 **맨 끝**에 추가:

```cpp
	// 준비 신호가 이미 와 있었다면 여기서 켜진다 — PC BeginPlay와 GameMode BeginPlay는 순서가 보장되지 않는다.
	TryStartBossFiring();
```

파일 끝(`EndGame` 아래)에 구현 추가:

```cpp
void AREGameMode::NotifyPlayerReady()
{
	bPlayerReady = true;
	TryStartBossFiring();
}

void AREGameMode::TryStartBossFiring()
{
	if (bFiringStarted || !bPlayerReady || !DemoBoss)
	{
		return;
	}
	bFiringStarted = true;
	// 시드는 서버 전용 PhaseRng 초기화용 — 네트워크에 나가지 않는다 (#84).
	DemoBoss->StartFiring(/*Seed=*/FMath::Rand());
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss firing started (player ready)"));
}
```

- [ ] **Step 3: PlayerController 헤더 — Server RPC 선언**

`REPlayerController.h`의 `Server_RequestFire` 선언 아래:

```cpp
	/**
	 *  로컬 클라 준비 통지 (#84). 서버는 이 신호를 받고 보스 발사를 시작한다.
	 *  PostLogin이 아니라 클라발인 이유: PostLogin은 서버측 PC 생성 시점이라
	 *  클라 월드가 아직 Multicast를 받을 준비가 안 됐을 수 있다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_NotifyReady();
```

- [ ] **Step 4: PlayerController cpp — 통지 + 수신 구현**

`BeginPlay()`의 `if (IsLocalPlayerController())` 블록 **안 끝부분**(매핑 컨텍스트 추가 뒤)에:

```cpp
		// 준비 완료를 서버에 알린다 — 싱글/리슨에서는 권한 보유라 즉시 로컬 실행된다.
		Server_NotifyReady();
```

include 블록에 `#include "REGameMode.h"` 추가(없으면). `Server_Dash_Implementation` 아래에:

```cpp
void AREPlayerController::Server_NotifyReady_Implementation()
{
	if (AREGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AREGameMode>() : nullptr)
	{
		GM->NotifyPlayerReady();
	}
}
```

- [ ] **Step 5: 빌드 게이트 (Editor + Server 둘 다)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 6: 싱글 회귀 프로브 — 게이트가 싱글을 막지 않는지**

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_84_gate.log"
```

```bash
grep -n "Boss firing started\|Boss FireDirect" Saved/Logs/RE_84_gate.log | head -5
```

기대: `[RE] Boss firing started (player ready)` 1회 후 `Boss FireDirect`가 이어진다.
**둘 다 없으면** 게이트가 싱글을 막은 것이다 — `TryStartBossFiring`의 두 진입점(BeginPlay 끝 / NotifyPlayerReady) 중 어느 쪽도 조건을 만족하지 못한 것이므로 각각에 임시 로그를 넣어 원인 규명.

- [ ] **Step 7: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(net): 보스 발사 시작을 클라 준비 신호로 게이트 (#84)"
```

---

### Task 5: 데디 검증 — 궤도 일치 정량 측정

여기서 처음으로 실제 목표("수 cm 이내 일치")를 숫자로 판정한다. 임시 프로브 코드가 필요하며 **커밋하지 않는다.**

**Files:**
- 임시 수정(커밋 금지): `Source/Project_RE/Mass/REBulletSimProcessor.cpp`
- Modify: `scripts/dedi-verify.ps1`
- Modify: `docs/guides/dedicated-server.md`

- [ ] **Step 1: 재쿡**

Global Constraints의 `BuildCookRun` 명령 실행. 기대: 성공. **이걸 빼먹으면 옛 산출물을 검증한다.**

- [ ] **Step 2: 데디 2프로세스 — 클라 스폰 확인**

```powershell
scripts\dedi-verify.ps1
```

기대: 기존 판정 전부 PASS(회귀 없음). 그 뒤 로그를 직접 확인한다:

```powershell
$d = (Get-ChildItem Saved\DediVerify | Sort-Object Name | Select-Object -Last 1).FullName
Select-String -Path "$d\server.log"  -Pattern "Boss FireDirect" | Select-Object -First 2
Select-String -Path "$d\client1.log" -Pattern "Boss FireDirect" | Select-Object -First 2
```

기대: **양쪽에 찍힌다.** 서버는 `role=ROLE_Authority Elapsed=0.000`, 클라는 `role=ROLE_SimulatedProxy Elapsed=` 가 0보다 큰 값(RPC 지연).
클라에 0건이면 Multicast가 안 간 것 — `Server_NotifyReady` 도달 여부부터 확인.

Reliable 큐 부담도 여기서 본다(스펙의 "측정해 기록" 항목). 초당 6.7회 발사에서 reliable 버퍼가 넘치면 UE가 클라를 끊는다:

```powershell
Select-String -Path "$d\*.log" -Pattern "reliable buffer|Closing connection.*reliable|ExceededMaxReliable"
```

기대: **0건.** 걸리면 Artillery 주기(`ArtilleryFireInterval=1.8`)나 `re.Profiling.KeepFiring` 연발 경로가 원인이므로 발사 빈도를 측정해 기록하고 스펙에 천장으로 남긴다.

- [ ] **Step 3: 임시 궤도 덤프 프로브 삽입 (⚠️ 커밋 금지)**

**기존 `ForEachEntityChunk` 안에서 덤프한다.** 같은 `Execute` 안에서 `ForEachEntityChunk`를 두 번 부르지 마라 — 청크 순회는 실행 컨텍스트를 소비하므로 두 번째 호출의 동작이 보장되지 않는다.

`REBulletSimProcessor.cpp`의 `Execute`를 다음으로 바꾼다(람다 캡처가 `[]`에서 `[&]`로 바뀌는 것에 주의):

```cpp
void UREBulletSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletSim);
	CSV_SCOPED_TIMING_STAT(REBullet, BulletSim);

	// TEMP #84 probe — 커밋 금지: 10초 시점에 1회만 생존 탄 좌표를 덤프한다.
	const UWorld* ProbeWorld = EntityManager.GetWorld();
	static bool bDumped = false;
	const bool bDumpNow = !bDumped && ProbeWorld && ProbeWorld->GetTimeSeconds() > 10.f;
	if (bDumpNow) { bDumped = true; }
	int32 DumpIdx = 0;

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Context)
	{
		const float Dt = Context.GetDeltaTimeSeconds();
		const int32 Num = Context.GetNumEntities();
		const TArrayView<FTransformFragment> Transforms = Context.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FBulletSimFragment> Sims       = Context.GetMutableFragmentView<FBulletSimFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			FBulletSimFragment& Sim = Sims[i];
			Transforms[i].GetMutableTransform().AddToTranslation(Sim.Velocity * Dt);
			Sim.Lifetime -= Dt;

			if (bDumpNow)   // TEMP #84 probe — 커밋 금지
			{
				UE_LOG(LogTemp, Log, TEXT("[RE] TrajDump %d %s"), DumpIdx++,
					*Transforms[i].GetTransform().GetLocation().ToString());
			}

			if (Sim.Lifetime <= 0.f)
			{
				Context.Defer().DestroyEntity(Context.GetEntity(i));
			}
		}
	});
}
```

Editor + Server 빌드 후 재쿡.

- [ ] **Step 4: 덤프 수집 + 좌표 차이 계산**

```powershell
scripts\dedi-verify.ps1
$d = (Get-ChildItem Saved\DediVerify | Sort-Object Name | Select-Object -Last 1).FullName
$rx = 'TrajDump (\d+) X=([-\d.]+) Y=([-\d.]+) Z=([-\d.]+)'
function Load($p) { @{} + (Select-String -Path $p -Pattern $rx | ForEach-Object {
    $m = $_.Matches[0]; @{ K = [int]$m.Groups[1].Value
                           V = [double[]]@($m.Groups[2].Value, $m.Groups[3].Value, $m.Groups[4].Value) } }) }
$S = Load "$d\server.log"; $C = Load "$d\client1.log"
"server dump = $($S.Count) / client dump = $($C.Count)"
```

`TrajDump` 인덱스는 청크 순회 순서라 양쪽이 같다는 보장이 없다. **인덱스로 짝짓지 말고**, 각 서버 좌표에 대해 클라 좌표 중 최근접 거리를 구해 그 최댓값을 본다:

```powershell
$worst = 0.0
foreach ($s in $S) {
  $best = [double]::MaxValue
  foreach ($c in $C) {
    $dx=$s.V[0]-$c.V[0]; $dy=$s.V[1]-$c.V[1]; $dz=$s.V[2]-$c.V[2]
    $dist=[Math]::Sqrt($dx*$dx+$dy*$dy+$dz*$dz); if ($dist -lt $best) { $best=$dist }
  }
  if ($best -gt $worst) { $worst = $best }
}
"worst nearest-neighbour distance = $worst uu"
```

**게이트: `worst` < 10 uu (10cm).** 초과하면 시간 보정이나 생성기 입력 중 하나가 어긋난 것 — superpowers:systematic-debugging.
덤프 건수가 양쪽에서 크게 다르면(예: 클라가 절반) 스폰 유실이므로 그것부터 해결한다.

- [ ] **Step 5: 실RHI 클라 스크린샷**

서버를 별도로 띄운 뒤 클라를 창모드로 접속시켜 탄막이 화면에 보이는지 PNG로 남긴다. `-nullrhi`로는 검증 불가하다.

```powershell
$stage = "E:\UnrealProjects\Project_RE\Saved\StagedBuilds\WindowsServer\Project_RE"
Start-Process "$stage\Binaries\Win64\Project_REServer.exe" -ArgumentList "-log","-port=7777" -WindowStyle Hidden
Start-Sleep -Seconds 20
& "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" 127.0.0.1:7777 `
  -game -windowed -ResX=1280 -ResY=720 -nosplash -NoSound
```

창에서 `` ` `` 콘솔 → `shot` 으로 스크린샷. 산출물은 `Saved/Screenshots/`.
기대: **클라 화면에 보스 탄막이 보인다.** #70이 "안 보이는 게 정상"으로 남긴 항목의 해소 증거다. 확인 후 서버 프로세스 종료.

- [ ] **Step 6: 임시 프로브 revert + 재빌드 + 재쿡**

```bash
git checkout -- Source/Project_RE/Mass/REBulletSimProcessor.cpp
git status --short
```

기대: `REBulletSimProcessor.cpp` 변경 없음. 이후 Editor + Server 빌드, 재쿡.

- [ ] **Step 7: `dedi-verify.ps1` 판정 추가**

`Invoke-Verdict`의 서버 블록에:

```powershell
    Assert-Log 'server' '탄막 발사(권위)'    $ServerLines '\[RE\] Boss FireDirect:.*role=ROLE_Authority'
```

클라 루프에:

```powershell
        Assert-Log $name '탄막 수신·스폰'     $span '\[RE\] Boss FireDirect:.*role=ROLE_SimulatedProxy'
```

`-SelfTest`의 합성 로그에도 같은 줄을 추가해 정상 케이스가 계속 통과하게 한다.

```powershell
scripts\dedi-verify.ps1 -SelfTest    # 기대: SelfTest OK
scripts\dedi-verify.ps1              # 기대: 전 항목 통과, EXIT=0
```

- [ ] **Step 8: 가이드 갱신**

`docs/guides/dedicated-server.md`의 판정 항목 표에서 **클라** 행 "있어야 하는 것"에 `[RE] Boss FireDirect:... role=ROLE_SimulatedProxy` 를, **서버** 행에 `role=ROLE_Authority` 버전을 추가한다.

- [ ] **Step 9: M3 프로파일 회귀**

시작 게이트가 발사 시점을 PC BeginPlay로 옮겼으므로 측정 창이 오염되지 않았는지 본다.

```powershell
scripts\profile.ps1 -Bullets 1000
```

기대: 산출물 `frames.csv`가 생성되고 `run.log`의 `RenderProbe` 유지 탄수가 종전 수준. 0발이면 게이트가 프로파일 경로를 막은 것이다.

- [ ] **Step 10: 커밋**

```bash
git add scripts/dedi-verify.ps1 docs/guides/dedicated-server.md
git commit -m "test(net): 데디 검증에 탄막 동기화 판정 추가 (#84)"
```

---

## 완료 후

### PR

```bash
git push -u origin feature/M5-bullet-sync
```

PR 규칙: **base=dev**, 이슈 #84 메타 미러링 — label `enhancement`,`networking`,`mass-entity`,`C++` / milestone `M5: 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격` / assignee `leejimin3` / project `Project_RE 개발 로드맵`. **Reviewer 생략.** 본문 6개 필드:

1. 요약
2. 변경사항
3. 이슈링크 (`Closes #84` — dev 머지로는 자동 종료가 안 되므로 머지 후 `gh issue close 84` 수동)
4. 검증 — Task 5의 `worst nearest-neighbour distance` 수치 + 클라 스크린샷 + dedi-verify 출력
5. 스코프 제외
6. 참고

### 남는 의도된 TODO (후속 이슈 몫 — 건드리지 말 것)

- `FireCurrentPattern` / `FireArtillery`의 `// TODO: 멀티는 타깃 선택 정책 필요 ... (#85)` — N인 조준 정책은 #85.
- Homing 경고 로그의 `(#67)` — 백로그.

## 하지 말 것 (스코프 밖)

- **중간 합류자 스냅샷 / 발사 기록 재생** — #85의 입장 게이트가 흡수한다. 필요해지면 최근 15초 발사 기록 약 100건(2.8KB)을 재생하면 되고, 이 계획의 시간 보정 경로를 그대로 재사용한다.
- **전원 입장 판정 · 대기 UI** — #85. 이 계획은 `TryStartBossFiring`의 조건 한 줄만 남겨둔다.
- **N인 조준 타깃 정책** — #85. `AimLoc`을 서버가 정해 보내므로 정책이 바뀌어도 RPC 계약은 그대로다.
- **Homing 패턴** — #67.
- **HitProcessor의 `GetPlayerPawn(0)`** — #86.
- **`Build.cs` / `.uproject` 수정** — 필요 없다. 필요하다고 느끼면 잘못 가고 있는 것이다.
- **밸런스 조정** — `Config/DefaultGame.ini` 값은 건드리지 않는다.
