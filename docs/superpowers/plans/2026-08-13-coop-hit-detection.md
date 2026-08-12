# N인 서버권위 피격 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mass 탄막 피격 판정이 플레이어 인덱스 0만 검사하는 것을 고쳐 N인 협동에서 전원이 정상 피격되게 한다 (#86).

**Architecture:** 두 히트 프로세서가 verbatim 중복하던 전처리부를 공용 헬퍼 `GatherHitTargets` 로 뽑는다(이번 버그가 정확히 그 중복 탓이다). 헬퍼가 살아있고 대쉬 중이 아닌 플레이어만 수집하고, 각 프로세서는 그 목록을 탄마다 순회한다. 직격탄은 첫 명중에서 멈추고 소멸, 곱사탄은 반경 안 전원.

**Tech Stack:** UE 5.8, C++ (Mass Entity 프로세서, GAS 태그 조회).

설계 스펙: `docs/superpowers/specs/2026-08-13-coop-hit-detection-design.md`

## Global Constraints

- 브랜치: `feature/M5-coop-hit` (이미 생성됨, 스펙 커밋 1건 있음)
- ⚠️ **본체 작업트리(`E:\UnrealProjects\Project_RE`)에서 실행 전제.** 아래 명령의 `-Project` 가 절대경로다. 격리 worktree에서 그대로 돌리면 worktree가 아니라 본체를 빌드하고, 캐시 때문에 수 초 만에 `Succeeded` 가 떠서 검증이 통과한 것처럼 보이지만 아무것도 검증하지 않는다.
- 빌드 게이트:
  ```powershell
  $BB = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat"
  $UP = "E:\UnrealProjects\Project_RE\Project_RE.uproject"
  & $BB Project_REEditor Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  & $BB Project_REServer Win64 Development -Project="$UP" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`. 각 ~90초, 타임아웃 400000ms.
- **에디터가 켜져 있으면 빌드가 실패한다** (`Unable to build while Live Coding is active`). 닫아둔 상태 유지.
- **데디 실행 전에는 재쿡이 필요하다:**
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
    -project="$UP" -noP4 -platform=Win64 -server -noclient -serverconfig=Development `
    -cook -stage -pak -skipbuild -utf8output
  ```
  `-skipbuild` 를 빼먹지 마라. 약 2분, 타임아웃 500000ms.
- 자동화 테스트 인프라 없음 → 게이트 = 빌드 + 헤드리스 로그 프로브 + 데디 실측.
- 로그 접두어 `[RE]` 고정.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- `Project_RE.uproject` 는 **절대 스테이징하지 않는다** — `EngineAssociation` GUID가 머신 종속이다. `git status` 에 수정으로 뜨는 것이 정상.
- 주석은 한국어. 주변 스타일에 맞춘다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- YAGNI: 스펙에 없는 기능·추상화·설정값 추가 금지.
- **내 변경이 만든 고아 include는 지운다.** 이 이슈는 `GetPlayerPawn`·대쉬 태그·ASC 조회를 프로세서에서 헬퍼로 옮기므로 그 include들이 프로세서에서 미사용이 된다. 남기지 마라. 단 **원래부터 있던 다른 미사용 코드는 건드리지 않는다.**

## File Structure

| 파일 | 책임 | 변경 |
|---|---|---|
| `Mass/REHitTargets.h` / `.cpp` | 피격 대상 수집 (생존·비대쉬 필터) — 두 프로세서 공용 | **신규** |
| `Mass/REBulletHitProcessor.cpp` | 직격탄 판정 — 첫 명중만, 명중 시 소멸 | 수정 |
| `Mass/REArcHitProcessor.cpp` | 곱사탄 착지 판정 — 반경 내 전원, 소멸 없음 | 수정 |

프로세서 헤더(`.h`)는 변경하지 않는다 — 시그니처가 그대로다.

---

### Task 1: 공용 피격 대상 수집 헬퍼

두 프로세서의 전처리부가 지금 verbatim 중복이고, 이번에 고치는 버그가 그 중복 탓이다(같은 모양의 `GetPlayerPawn(0)` 이 두 곳에). 전처리부가 복잡해지므로 먼저 하나로 뽑는다.

**Files:**
- Create: `Source/Project_RE/Mass/REHitTargets.h`
- Create: `Source/Project_RE/Mass/REHitTargets.cpp`

**Interfaces:**
- Produces: `struct FREHitTarget { ARECharacterBase* Player; FVector Location; }`
- Produces: `void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out)`
- Task 2·3이 둘 다 소비한다.

- [ ] **Step 1: 헤더 생성**

`Source/Project_RE/Mass/REHitTargets.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ARECharacterBase;
class UWorld;

/** 피격 판정 대상 1명 — 액터와 수집 시점 위치. UStruct 아님(함수 인자 전용 경량 구조체). */
struct FREHitTarget
{
	ARECharacterBase* Player   = nullptr;
	FVector           Location = FVector::ZeroVector;
};

/**
 *  피격 판정 대상 수집 (#86). 살아있고 대쉬 중이 아닌 플레이어만 담는다.
 *  서버 판정 프로세서 2종 공용 — 한쪽만 고쳐지는 사고를 막는다(이 이슈의 버그가 그 중복 탓이었다).
 *  Out은 Reset 후 채운다. World가 null이면 빈 배열을 남기고 반환한다.
 */
void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out);
```

- [ ] **Step 2: 구현 생성**

`Source/Project_RE/Mass/REHitTargets.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHitTargets.h"
#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out)
{
	Out.Reset();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			continue;   // 접속 종료 중인 PC가 섞일 수 있다
		}
		ARECharacterBase* Player = Cast<ARECharacterBase>(It->Get()->GetPawn());
		if (!Player || !Player->IsAlive())
		{
			// 사망자 제외 — 시체가 탄을 흡수해 뒤에 선 생존자를 가리지 않게 한다.
			continue;
		}
		// 대쉬 무적은 본인만 (#25/#39). 목록에서 빠지면 거리 비교 자체가 없으므로
		// "판정만 스킵하고 탄환은 파괴하지 않는다"는 기존 의미가 그대로 유지된다.
		const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
		if (ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing))
		{
			// 대쉬 중엔 매 프레임 찍히므로 Verbose — 검증 시 -LogCmds="LogTemp Verbose"로 관측.
			// 프로세서에 있던 같은 진단을 여기로 옮긴 것이다(이제 스킵 판단이 여기서 난다).
			UE_LOG(LogTemp, Verbose, TEXT("[RE] HitTargets: skipped (State.Dashing)"));
			continue;
		}
		Out.Add(FREHitTarget{ Player, Player->GetActorLocation() });
	}
}
```

- [ ] **Step 3: 빌드 게이트 (Editor)**

이 태스크는 아직 호출부가 없으므로 Editor 타겟만으로 충분하다.
기대: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Mass/REHitTargets.h Source/Project_RE/Mass/REHitTargets.cpp
git commit -m "feat(coop): 피격 대상 수집 공용 헬퍼 추가 (#86)"
```

---

### Task 2: 직격탄 판정 N인 대응

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletHitProcessor.cpp`

**Interfaces:**
- Consumes: `FREHitTarget`, `GatherHitTargets` (Task 1)

- [ ] **Step 1: include 정리**

`REBulletHitProcessor.cpp` include 블록에 추가:

```cpp
#include "REHitTargets.h"
```

**아래 셋을 삭제한다** — 이 태스크가 그 사용처를 헬퍼로 옮기므로 미사용이 된다:

```cpp
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
```

`Core/RECharacterBase.h` 는 **남긴다** — `T.Player->TakeDamage(...)` 호출에 완전한 타입이 필요하다(헤더는 전방 선언만 한다).

- [ ] **Step 2: `Execute` 전문 교체**

```cpp
void UREBulletHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletHit);
	CSV_SCOPED_TIMING_STAT(REBullet, BulletHit);

	// 살아있고 대쉬 중이 아닌 플레이어 전원 (#86). 대상이 없으면 탄을 순회할 이유가 없다.
	TArray<FREHitTarget> Targets;
	GatherHitTargets(EntityManager.GetWorld(), Targets);
	if (Targets.IsEmpty())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			// 탑다운 — XY 평면 거리만 비교 (탄환 Z와 캡슐 중심 Z 불일치 함정 회피)
			const FVector BulletLoc = Transforms[i].GetTransform().GetLocation();
			for (const FREHitTarget& T : Targets)
			{
				if (FVector::DistSquaredXY(BulletLoc, T.Location) <= HitRadius * HitRadius)
				{
					const float Applied = T.Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
					Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
					UE_LOG(LogTemp, Log, TEXT("[RE] BulletHit: Applied=%.0f"), Applied);
					break;   // 투사체 하나는 한 명만 — 몸으로 막는 탱 플레이가 성립한다
				}
			}
		}
	});
}
```

`HitRadius` / `BulletDamage` 는 파일 상단 익명 네임스페이스의 기존 상수다. 값을 바꾸지 마라.

- [ ] **Step 3: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 4: 싱글 회귀 프로브**

1인이면 `Targets` 가 원소 1개라 판정이 종전과 같아야 한다. 재쿡이 필요 없는 standalone 헤드리스로 확인한다.

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_t2.log"
```

`-unattended` 를 **빼고** 실행한다(프로브가 4.4초에 프로세스를 종료시켜 피격까지 못 간다). 60초쯤 뒤 종료.

```bash
grep -c "BulletHit: Applied=" Saved/Logs/RE_86_t2.log
grep -n "Player died\|EndGame" Saved/Logs/RE_86_t2.log | head -3
```

기대: `BulletHit: Applied=` 가 다수 발생하고 `[RE] Player died` → `[RE] EndGame: DEFEAT` 로 이어진다. 0건이면 판정이 통째로 죽은 것이다.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletHitProcessor.cpp
git commit -m "feat(coop): 직격탄 판정을 전 생존자 대상으로 전환 (#86)"
```

---

### Task 3: 곱사탄 판정 N인 대응

곱사탄은 착지 반경 안 **전원**을 때리고 엔티티를 소멸시키지 않는다(`REArcSimProcessor` 가 착지 시 자체 소멸시킨다). 직격탄과 달리 `break` 가 없다.

**Files:**
- Modify: `Source/Project_RE/Mass/REArcHitProcessor.cpp`

**Interfaces:**
- Consumes: `FREHitTarget`, `GatherHitTargets` (Task 1)

- [ ] **Step 1: include 정리**

`REArcHitProcessor.cpp` include 블록에 추가:

```cpp
#include "REHitTargets.h"
```

**아래 셋을 삭제한다** (사용처가 헬퍼로 옮겨져 미사용이 된다):

```cpp
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Kismet/GameplayStatics.h"
```

`Core/RECharacterBase.h` 는 남긴다 — `TakeDamage` 호출에 완전한 타입이 필요하다.

- [ ] **Step 2: `Execute` 전문 교체**

```cpp
void UREArcHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcHit);

	// 살아있고 대쉬 중이 아닌 플레이어 전원 (#86).
	TArray<FREHitTarget> Targets;
	GatherHitTargets(EntityManager.GetWorld(), Targets);
	if (Targets.IsEmpty())
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FArcBulletFragment> Arcs = Ctx.GetFragmentView<FArcBulletFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			const FArcBulletFragment& A = Arcs[i];
			if (A.Elapsed < A.FlightTime)
			{
				continue;   // 아직 비행 중 — 착지 프레임만 판정
			}
			// 착지: 마커 반경 안 플레이어 전원에게 범위 데미지. XY 평면 거리(탑다운).
			// break 없음 — 범위 폭발이라 겹친 인원이 모두 맞는다. 소멸은 Sim이 착지 시 처리한다.
			for (const FREHitTarget& T : Targets)
			{
				if (FVector::DistSquaredXY(A.Target, T.Location) <= A.Radius * A.Radius)
				{
					const float Applied = T.Player->TakeDamage(A.Damage, FDamageEvent(), nullptr, nullptr);
					UE_LOG(LogTemp, Log, TEXT("[RE] ArcHit: Applied=%.0f R=%.0f"), Applied, A.Radius);
				}
			}
		}
	});
}
```

- [ ] **Step 3: 빌드 게이트 (Editor + Server)**

기대: 양쪽 `Result: Succeeded`.

- [ ] **Step 4: 싱글 회귀 프로브 — Artillery 경로**

Artillery 페이즈는 로테이션 랜덤이라 나올 때까지 시간이 걸린다.

```bash
MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -nosplash -stdout -NoSound -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_t3.log"
```

60초쯤 뒤 종료.

```bash
grep -n "Boss FireArtillery\|ArcHit: Applied=" Saved/Logs/RE_86_t3.log | head -10
```

기대: `Boss FireArtillery` 가 나오고, 플레이어가 착지 반경 안에 있었다면 `ArcHit: Applied=` 도 나온다.
**`ArcHit` 이 0건이어도 실패가 아니다** — 플레이어가 정지해 있어 반경 밖일 수 있다. 그 경우 `Boss FireArtillery` 가 나왔는지만 확인하고, 곱사탄 실제 피격은 Task 4의 2인 실측에서 본다. 판단을 보고서에 적어라.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REArcHitProcessor.cpp
git commit -m "feat(coop): 곱사탄 착지 판정을 반경 내 전원 대상으로 전환 (#86)"
```

---

### Task 4: 2인 데디 실측

이 이슈가 여는 것들을 실제로 확인한다. **#85가 임시 프로브로만 증명했던 경로들이 여기서 자연 전투로 성립한다.**

**Files:** 커밋할 코드 없음(Step 6 문서 제외). 관측 전용.

- [ ] **Step 1: 재쿡**

Global Constraints의 `BuildCookRun` 실행. **빼먹으면 옛 산출물을 검증한다.**

- [ ] **Step 2: 1인 회귀 — dedi-verify**

```powershell
scripts\dedi-verify.ps1
```

기대: 12항목 전부 PASS, `EXIT=0`. 여기가 깨지면 2인을 볼 것도 없다.

- [ ] **Step 3: 2인 페어 기동**

**서버에 `-unattended` 를 주지 않는다** — 주면 첫 클라의 서버측 프로브가 약 4.4초에 `RequestExit` 으로 서버를 내려 두 번째 클라가 붙을 시간이 없다.

```powershell
$stage = "E:\UnrealProjects\Project_RE\Saved\StagedBuilds\WindowsServer\Project_RE"
$srv = Start-Process "$stage\Binaries\Win64\Project_REServer.exe" -PassThru -WindowStyle Hidden `
  -ArgumentList "-log","-port=7777","-ExecCmds=`"re.Coop.ExpectedPlayers 2`"","-abslog=`"E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_srv.log`""
Start-Sleep -Seconds 20
```

서버 로그에 `IpNetDriver listening` 이 뜬 것을 확인한 뒤 클라 2개를 연달아 붙인다:

```powershell
$ed = "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$up = "E:\UnrealProjects\Project_RE\Project_RE.uproject"
$c1 = Start-Process $ed -PassThru -WindowStyle Hidden -ArgumentList `
  "`"$up`"","127.0.0.1:7777","-game","-nullrhi","-log","-abslog=`"E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_c1.log`""
$c2 = Start-Process $ed -PassThru -WindowStyle Hidden -ArgumentList `
  "`"$up`"","127.0.0.1:7777","-game","-nullrhi","-log","-abslog=`"E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_c2.log`""
```

클라에도 `-unattended` 를 주지 않는다. 두 클라가 붙으면 `Boss firing started (2/2 ready)` 가 서버 로그에 뜬다. 두 플레이어가 100 HP를 탄막으로 소진할 때까지 수십 초 기다린다.

- [ ] **Step 4: 두 플레이어 모두 피격 — 이 이슈의 핵심**

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_srv.log" -Pattern "Player died|All 2 players dead|EndGame"
```

기대: `[RE] Player died 1/2` **와** `[RE] Player died 2/2` 가 **둘 다** 나오고 `All 2 players dead` → `EndGame: DEFEAT` 로 이어진다.

`Player died 1/2` 만 나오고 몇 분이 지나도 `2/2` 가 없으면 **이 이슈가 해결되지 않은 것**이다 — 여전히 한 명만 맞고 있다.

- [ ] **Step 5: 양 클라 결과 화면 — 임시 프로브 없이**

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_c1.log" -Pattern "Client_NotifyDeath|Client_ShowResult"
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_c2.log" -Pattern "Client_NotifyDeath|Client_ShowResult"
```

기대: **두 클라 모두** `Client_NotifyDeath` 와 `Client_ShowResult: DEFEAT`.

#85는 이 경로를 임시 프로브로만 증명할 수 있었다. 이번엔 프로브 없이 나와야 한다 — 그게 이 이슈의 완료 증거다.

- [ ] **Step 6: 곱사탄 반경 전원 피격**

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_srv.log" -Pattern "ArcHit: Applied="
```

두 플레이어는 `SpawnSpacing=250` 만큼 떨어져 있고 Artillery 마커 반경은 `ArtilleryRadius=120` 이다. 즉 **한 착지점이 둘 다 덮는 일은 드물다.** 같은 착지 프레임에 `ArcHit` 이 2건 연속 나오면 전원 피격이 확인된 것이고, 안 나와도 실패가 아니다.

확인 불가로 끝나면 그 사실을 보고서에 적어라. 반경보다 이격이 크다는 기하학적 이유를 함께 적으면 다음 사람이 재시도하지 않는다.

- [ ] **Step 7: 대쉬 무적 개별화 — 관측 가능하면**

헤드리스 클라에는 입력이 없어 대쉬를 자발적으로 하지 않는다. 서버측 프로브(`-unattended`)는 이 실행에서 꺼져 있다. 즉 **이 절차로는 대쉬 개별화를 직접 관측할 수 없다.**

코드 근거로 대신한다: `GatherHitTargets` 가 대쉬 태그 보유자만 `continue` 로 건너뛰고 나머지는 `Out` 에 남기므로, 한 명의 대쉬가 다른 사람의 판정에 관여할 경로가 없다. 이 판단을 보고서에 적어라.

- [ ] **Step 8: 시체 통과 확인**

`Player died 1/2` 이후에도 남은 생존자가 계속 피격되는지 본다.

```powershell
Select-String -Path "E:\UnrealProjects\Project_RE\Saved\Logs\RE_86_srv.log" -Pattern "BulletHit: Applied=" | Select-Object -Last 5
```

기대: 첫 사망 이후에도 `BulletHit` 이 계속 발생하고 결국 `Player died 2/2` 에 도달한다. 첫 사망 직후 `BulletHit` 이 끊기면 시체가 여전히 탄을 흡수하는 것이다.

- [ ] **Step 9: 프로세스 정리**

```powershell
foreach ($p in @($srv,$c1,$c2)) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
```

- [ ] **Step 10: 가이드 갱신 + 커밋**

`docs/guides/dedicated-server.md` 의 2인 수동 검증 절차에 붙은 **#86 경고 문단을 제거하거나 해소됨으로 갱신한다** — 이제 두 번째 사망이 자연 전투로 도달 가능하다. 그 문단은 #85가 남긴 것이고 이 이슈가 그 전제를 없앴다.

```bash
git add docs/guides/dedicated-server.md
git commit -m "docs(coop): 2인 검증의 #86 제약 해소 반영 (#86)"
```

---

## 완료 후

### 머지 전 풀 유니티 빌드 확인

이 프로젝트의 PR 빌드 게이트는 adaptive non-unity로 돌아서, **동명 지역/익명 네임스페이스 변수가 풀 유니티 빌드에서만 C4459로 실패**할 수 있다. 두 프로세서가 같은 이름의 지역 변수(`Targets`)를 쓰므로 머지 전에 한 번 확인한다.

```powershell
& $BB Project_REEditor Win64 Development -Project="$UP" -WaitMutex -NoHotReload
```
증분이 아닌 상태에서 통과하는지 보고, 의심되면 `Intermediate` 의 해당 모듈 산출물을 지우고 재빌드한다.

### PR

```bash
git push -u origin feature/M5-coop-hit
```

PR 규칙: **base=dev**, 이슈 #86 메타 미러링 — label `enhancement`,`networking`,`mass-entity`,`C++` / milestone `M5: 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격` / assignee `leejimin3` / project `Project_RE 개발 로드맵`. **Reviewer 생략.** 본문 6개 필드:

1. 요약
2. 변경사항
3. 이슈링크 (`Closes #86` — dev 머지로는 자동 종료가 안 되므로 머지 후 `gh issue close 86` 수동)
4. 검증 — Task 4의 서버/클라 로그 발췌. 특히 `Player died 2/2` 와 양 클라 `Client_ShowResult`
5. 스코프 제외 — 성능 실측이 #88에 막혀 이월된 것을 명시
6. 참고

### 이슈 완료조건 수정

#86 본문의 "5000발 프로파일 회귀 측정" 완료조건은 #88(프로파일 하네스 고장) 때문에 지금 충족할 수 없다. PR 본문과 이슈 코멘트에 **이월 사실과 사유**를 남긴다. 비용 분석(탄당 비교 1회→N회, 5000발×4명 = 2만 회 `DistSquaredXY`)으로 대체한다.

## 하지 말 것 (스코프 밖)

- **#88 프로파일 하네스 수리** — M6 별도 이슈
- **탄막 밀도·데미지 밸런스 조정** — `Config/DefaultGame.ini` 값은 건드리지 않는다
- **dedi-verify N인 자동 판정** → #87
- **`Variant_Combat/AI/EnvQueryContext_Player.cpp` 의 `GetPlayerPawn(0)`** — 참조 0건 엔진 템플릿 잔재. 선재 죽은 코드라 건드리지 않는다
- **탄이 겹친 두 명 중 최근접을 고르게 하기** — 스펙이 "임의 선택 수용"으로 확정했다. `break` 를 유지하라
- **내부 루프에서 생존 재확인** — 스펙이 "죽은 그 프레임 한정 흡수는 값을 따져 수용"으로 확정했다
- **`Build.cs` / `.uproject` 수정** — 필요 없다
