# 탄환 소멸 폭발 연출 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 탄환이 피격·착지로 소멸할 때 Niagara 폭발을 띄운다. 수명 만료에는 붙이지 않는다.

**Architecture:** 복제하지 않는다 — `REBulletHitProcessor` 를 `AllNetModes` 로 열어 클라가 자기 시뮬로 판정하고, `TakeDamage` 만 서버 권위로 가드한다. 곡사탄은 시뮬(워커 스레드)을 건드리지 않고 별도 클라 전용 GT 프로세서를 시뮬 앞에 배치해 폭발만 스폰한다.

**Tech Stack:** UE 5.8, Niagara(플러그인 활성화 필요), Mass Entity, `PythonScriptPlugin`(에셋 부트스트랩)

**Spec:** `docs/superpowers/specs/2026-08-15-bullet-explosion-design.md`

## Global Constraints

- **수명 만료(`REBulletSimProcessor.cpp:45`)에는 폭발을 붙이지 않는다.** 정상상태 4,800발 / 수명 15초 = 초당 약 320발 만료. 붙이는 순간 다른 모든 설계가 무의미해진다.
- **데미지 권위는 서버에만.** `TakeDamage` 호출은 반드시 `Player->HasAuthority()` 가드 안에 둔다.
- **`REArcSimProcessor` 의 스레드 배치를 바꾸지 않는다.** 워커에 남겨야 곡사탄이 수천 개로 늘어도 버틴다.
- **데디서버에서는 Niagara 를 스폰하지 않는다.** 렌더가 없다.
- **스폰은 `ENCPoolMethod::AutoRelease`** — 엔진 내장 컴포넌트 풀링. 인자 하나이고 안 쓸 이유가 없다.
- **PowerShell 파일은 UTF-8 BOM 포함으로 저장한다.** BOM 없으면 PS 5.1 이 ANSI 로 읽어 한글이 깨지고 파싱이 실패한다(#82 사고).
- **`Project_RE.uproject` 를 스테이징/커밋하지 않는다.** 머신 로컬 `EngineAssociation` GUID 때문에 항상 수정됨으로 뜨는 것이 정상이다. **단 Task 1 은 예외** — 플러그인 활성화가 이 파일에 들어가므로 그 줄만 의도적으로 커밋한다(아래 참조).
- **worktree 금지 — 본체 작업 디렉터리에서.** 에디터 실행과 프로파일 런이 머신 로컬 `uproject` 를 쓴다.
- 브랜치: `feature/M6-bullet-explosion` (이미 생성됨, 스펙 커밋 `82940b9` 존재)

---

## File Structure

| 파일 | 책임 | 상태 |
|---|---|---|
| `Project_RE.uproject` | Niagara 플러그인 활성화 | 수정 (Task 1 한정) |
| `Source/Project_RE/Project_RE.Build.cs` | `Niagara` 모듈 의존 | 수정 |
| `scripts/make_explosion_fx.py` | 폭발 에셋 부트스트랩 | **신규** |
| `Content/FX/NS_REBulletExplosion.uasset` | 폭발 Niagara 시스템 (정본) | **신규 — 스크립트 산출물** |
| `Source/Project_RE/Mass/REExplosionFx.h/.cpp` | 폭발 스폰 단일 진입점 | **신규** |
| `Source/Project_RE/Mass/REHitTargets.cpp` | 대상 수집을 클라에서도 동작하게 | 수정 |
| `Source/Project_RE/Mass/REBulletHitProcessor.cpp` | `AllNetModes` + 데미지 가드 + 폭발 | 수정 |
| `Source/Project_RE/Mass/REArcFxProcessor.h/.cpp` | 곡사탄 착지 폭발 (클라 전용, GT) | **신규** |
| `docs/guides/profiling.md` | 폭발 부하 측정 기록 | 수정 |

---

### Task 1: Niagara 플러그인 활성화 + 모듈 의존

**Files:**
- Modify: `Project_RE.uproject`
- Modify: `Source/Project_RE/Project_RE.Build.cs`

**Interfaces:**
- Produces: `#include "NiagaraFunctionLibrary.h"` / `#include "NiagaraSystem.h"` 가 컴파일되는 상태. Task 3·5·6 이 이에 의존한다.

- [ ] **Step 1: 현재 플러그인 목록 확인**

Run:
```
python -c "import json,io; d=json.load(io.open('Project_RE.uproject',encoding='utf-8-sig')); [print(' ', p['Name'], p.get('Enabled')) for p in d.get('Plugins',[])]"
```

Expected: `ModelingToolsEditorMode` / `StateTree` / `GameplayStateTree` / `MassGameplay` / `GameplayAbilities` / `ModelContextProtocol` 여섯 개. **Niagara 는 없다.**

- [ ] **Step 2: `uproject` 에 Niagara 추가**

`Plugins` 배열 끝에 항목을 더한다. 다른 항목과 같은 형식을 유지한다:

```json
		{
			"Name": "Niagara",
			"Enabled": true
		}
```

주의: 이 파일은 평소 커밋 금지 대상이지만, **플러그인 목록은 프로젝트 설정이라 커밋해야 한다.** `EngineAssociation` 줄은 건드리지 말 것.

- [ ] **Step 3: `Build.cs` 에 모듈 추가**

`Source/Project_RE/Project_RE.Build.cs` 의 `PublicDependencyModuleNames.AddRange(new string[] {` 목록에 추가:

```csharp
			"Niagara",
```

- [ ] **Step 4: 빌드**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
Expected: `Result: Succeeded`

- [ ] **Step 5: 플러그인이 실제로 로드되는지 확인**

**"켰다고 가정" 금지.** `MassGameplay` 를 안 켜서 Mass 프로세서 `Execute` 가 0회 돌던 전례가 있다 — 코드는 멀쩡한데 실행이 안 돼 한참 못 봤다.

Run:
```
.\scripts\profile.ps1 -Bullets 1000 -Label niagaracheck
$d = (Get-ChildItem .\Saved\Profiling\RE_Mass_1000_niagaracheck_* -Directory | Select-Object -Last 1).FullName
@(Select-String -LiteralPath "$d\run.log" -Pattern 'LogNiagara').Count
```

Expected: **1건 이상.** `LogNiagara` 카테고리가 로그에 나타나면 모듈이 로드된 것이다. 0건이면 멈춰라.

- [ ] **Step 6: 커밋**

```bash
git add Project_RE.uproject Source/Project_RE/Project_RE.Build.cs
git commit -m "build: Niagara 플러그인 활성화 + 모듈 의존 추가 (#98)"
```

`git status` 로 `Project_RE.uproject` 에 **플러그인 항목만** 들어갔는지 확인하라 — `EngineAssociation` 이 섞이면 되돌려라.

---

### Task 2: 폭발 에셋 부트스트랩

**Files:**
- Create: `scripts/make_explosion_fx.py`
- Create (산출물): `Content/FX/NS_REBulletExplosion.uasset`

**Interfaces:**
- Consumes: Task 1 의 Niagara 플러그인
- Produces: 에셋 경로 **`/Game/FX/NS_REBulletExplosion`**. Task 3 이 이 경로를 로드한다.

- [ ] **Step 1: 스크립트 작성**

엔진이 `/Niagara/DefaultAssets/Templates/Systems/SimpleExplosion` 을 제공한다. 처음부터 저작하지 않고 **복제**한다.

`scripts/make_explosion_fx.py` 를 만든다:

```python
"""
NS_REBulletExplosion 부트스트랩 생성 (#98).

엔진 템플릿 SimpleExplosion 을 복제해 프로젝트 소유로 만든다. 이 스크립트는 '정본'이
아니라 '초안 생성기'다 - 생성 후 에디터에서 손보면 낡는다. 정본은
Content/FX/NS_REBulletExplosion.uasset 이다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
  재생성하려면 커맨드라인에 --force 를 더한다(수동 튜닝이 날아간다).
"""
import unreal

SRC  = "/Niagara/DefaultAssets/Templates/Systems/SimpleExplosion"
PKG  = "/Game/FX"
NAME = "NS_REBulletExplosion"
FULL = PKG + "/" + NAME

if not unreal.EditorAssetLibrary.does_asset_exist(SRC):
    unreal.log_error("RE_FX: 엔진 템플릿이 없다: %s" % SRC)
    raise SystemExit(1)

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    if "--force" not in unreal.SystemLibrary.get_command_line().split():
        unreal.log_error("RE_FX: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % FULL)
        raise SystemExit(1)
    unreal.log_warning("RE_FX: --force - 기존 에셋을 지우고 재생성한다: %s" % FULL)
    unreal.EditorAssetLibrary.delete_asset(FULL)

if not unreal.EditorAssetLibrary.duplicate_asset(SRC, FULL):
    unreal.log_error("RE_FX: 복제 실패 %s -> %s" % (SRC, FULL))
    raise SystemExit(1)

unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log("RE_FX: 생성 완료 %s" % FULL)
```

색·크기 튜닝은 하지 않는다. **먼저 화면에 나오는 것을 확인한 뒤** Task 7 에서 스크린샷을 보고 조정한다 — #97 에서 발광을 미리 정했다가 블룸으로 씻겨 여섯 번 다시 돌린 전례가 있다.

- [ ] **Step 2: 실행**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'E:\UnrealProjects\Project_RE\Project_RE.uproject' -ExecutePythonScript="E:/UnrealProjects/Project_RE/scripts/make_explosion_fx.py" -unattended -nosplash -nop4 -abslog="E:\UnrealProjects\Project_RE\Saved\make_fx.log"
```

- [ ] **Step 3: 확인**

Run:
```
Select-String -Path .\Saved\make_fx.log -Pattern 'RE_FX|LogPython: Error' | ForEach-Object { $_.Line }
Test-Path .\Content\FX\NS_REBulletExplosion.uasset
```

Expected: `RE_FX: 생성 완료 /Game/FX/NS_REBulletExplosion` 이 보이고, `LogPython: Error` 는 **0건**, `Test-Path` 가 `True`.

**`LogPython: Error` 가 하나라도 있으면 멈춰라.** 복제가 실패해도 파일이 부분 생성될 수 있어 존재 여부만으로는 판정이 안 된다.

- [ ] **Step 4: 커밋**

```bash
git add scripts/make_explosion_fx.py Content/FX/NS_REBulletExplosion.uasset
git commit -m "feat(fx): 폭발 Niagara 에셋 부트스트랩 생성 (#98)"
```

---

### Task 3: 폭발 스폰 단일 진입점

**Files:**
- Create: `Source/Project_RE/Mass/REExplosionFx.h`
- Create: `Source/Project_RE/Mass/REExplosionFx.cpp`

**Interfaces:**
- Consumes: Task 1 의 `Niagara` 모듈, Task 2 의 `/Game/FX/NS_REBulletExplosion`
- Produces: `void SpawnBulletExplosion(const UWorld* World, const FVector& Location);` — Task 5·6 이 이 함수만 부른다. 스폰 방식(개별/풀링/배칭)을 바꿀 때 이 파일 하나만 고치면 된다.

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/Mass/REExplosionFx.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  탄환 소멸 폭발 스폰 — 단일 진입점 (#98).
 *
 *  스폰 방식을 나중에 바꿀 때(개별 → NDC 배칭) 이 함수 하나만 고치면 되도록
 *  호출부를 여기로 모은다. 현재는 엔진 컴포넌트 풀(AutoRelease)을 쓴 개별 스폰이다.
 *
 *  데디서버에서는 아무것도 하지 않는다 — 렌더가 없다.
 *  게임 스레드에서만 부를 것. Niagara 스폰은 GT 전용이다.
 */
namespace REExplosionFx
{
	void SpawnBulletExplosion(const UWorld* World, const FVector& Location);
}
```

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/Mass/REExplosionFx.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionFx.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"

namespace
{
	/** 폭발 에셋. Task 2 의 부트스트랩이 만든다. */
	const TCHAR* ExplosionAssetPath = TEXT("/Game/FX/NS_REBulletExplosion.NS_REBulletExplosion");

	/** 로드 캐시 — 매 폭발마다 LoadObject 하지 않는다. */
	UNiagaraSystem* GCachedSystem = nullptr;
	bool GLoadAttempted = false;
}

void REExplosionFx::SpawnBulletExplosion(const UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;   // 데디서버는 렌더가 없다
	}

	if (!GLoadAttempted)
	{
		GLoadAttempted = true;
		GCachedSystem = LoadObject<UNiagaraSystem>(nullptr, ExplosionAssetPath);
		if (!GCachedSystem)
		{
			// 조용한 무동작 금지 — 폭발이 안 나오는 것을 눈치채기 어렵다.
			UE_LOG(LogTemp, Error, TEXT("[RE] NS_REBulletExplosion 로드 실패 — 폭발이 표시되지 않는다 (#98)"));
		}
	}
	if (!GCachedSystem)
	{
		return;
	}

	// AutoRelease: 엔진 컴포넌트 풀에서 꺼내 쓰고 자동 반납. one-shot fx 전용 경로다.
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, GCachedSystem, Location,
		FRotator::ZeroRotator, FVector(1.f),
		/*bAutoDestroy=*/true, /*bAutoActivate=*/true,
		ENCPoolMethod::AutoRelease);
}
```

- [ ] **Step 3: 빌드**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
Expected: `Result: Succeeded`

빌드가 `ENCPoolMethod` 를 못 찾으면 `#include "NiagaraComponentPoolMethodEnum.h"` 를 더하라 — `NiagaraFunctionLibrary.h` 가 전방 선언만 가질 수 있다.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Mass/REExplosionFx.h Source/Project_RE/Mass/REExplosionFx.cpp
git commit -m "feat(fx): 폭발 스폰 단일 진입점 추가 (#98)"
```

---

### Task 4: 대상 수집을 클라에서도 동작하게

**Files:**
- Modify: `Source/Project_RE/Mass/REHitTargets.cpp`

**Interfaces:**
- Produces: `GatherHitTargets` 가 클라에서도 **모든** 복제된 캐릭터를 찾는 상태. Task 5 가 이에 의존한다.

- [ ] **Step 1: 순회 방식 교체**

현재는 `GetPlayerControllerIterator` 를 쓴다. **클라에서는 로컬 컨트롤러 하나만 나온다** — M5 가 N인 협동으로 열어놨으므로 그대로 두면 자기 피격만 보이고 동료 피격은 안 보인다.

현재 코드(`REHitTargets.cpp` 18-24행 부근):

```cpp
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (!It->IsValid())
		{
			continue;   // 접속 종료 중인 PC가 섞일 수 있다
		}
		ARECharacterBase* Player = Cast<ARECharacterBase>(It->Get()->GetPawn());
```

이것으로 교체:

```cpp
	// 컨트롤러가 아니라 캐릭터를 순회한다 — 클라에서 GetPlayerControllerIterator 는
	// 로컬 컨트롤러 하나만 돌려주므로, 그대로 두면 클라 판정(#98)이 동료 피격을 놓친다.
	for (TActorIterator<ARECharacterBase> It(World); It; ++It)
	{
		ARECharacterBase* Player = *It;
```

- [ ] **Step 2: 인클루드 추가**

`REHitTargets.cpp` 상단 인클루드에 추가:

```cpp
#include "EngineUtils.h"   // TActorIterator
```

- [ ] **Step 3: 빌드**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REServer Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
Expected: 양쪽 `Result: Succeeded`

- [ ] **Step 4: 서버 판정이 안 깨졌는지 확인**

이 변경은 서버 동작도 바꾼다(컨트롤러 없는 캐릭터가 대상에 들어올 수 있다).

Run:
```
.\scripts\dedi-verify.ps1
```
Expected: 전 항목 PASS, `EXIT=0`. 특히 `server: 탄막 발사(권위)` 와 `client1: 탄막 수신·스폰`.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REHitTargets.cpp
git commit -m "fix(mass): 대상 수집을 캐릭터 순회로 — 클라에서도 전원 탐지 (#98)"
```

---

### Task 5: 직선탄 피격 — 판정 개방 + 데미지 가드 + 폭발

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletHitProcessor.cpp`

**Interfaces:**
- Consumes: Task 3 의 `REExplosionFx::SpawnBulletExplosion`, Task 4 의 클라 대응 `GatherHitTargets`
- Produces: 클라에서도 직선탄이 피격 시 사라지고 폭발이 뜨는 상태

- [ ] **Step 1: 넷모드 개방**

현재(`REBulletHitProcessor.cpp:32` 부근):

```cpp
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
```

이것으로 교체:

```cpp
	// 판정을 클라에도 연다 — 클라가 자기 시뮬로 탄을 지우고 폭발을 띄운다(#98).
	// 데미지는 아래 HasAuthority 가드로 서버에만 남는다. 복제 RPC 는 쓰지 않는다:
	// 클라는 이미 ServerTime 으로 탄 위치를 자체 계산하므로(#84) 판정 근거가 있다.
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
```

`bRequiresGameThreadExecution = true` 는 **그대로 둔다** — Niagara 스폰에도 필요하다.

- [ ] **Step 2: 데미지 가드 + 폭발 스폰**

현재(`:70` 부근):

```cpp
				if (FVector::DistSquaredXY(BulletLoc, T.Location) <= HitRadius * HitRadius)
				{
					const float Applied = T.Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
					Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
					UE_LOG(LogTemp, Log, TEXT("[RE] BulletHit: Applied=%.0f"), Applied);
					break;   // 투사체 하나는 한 명만 — 몸으로 막는 탱 플레이가 성립한다
				}
```

이것으로 교체:

```cpp
				if (FVector::DistSquaredXY(BulletLoc, T.Location) <= HitRadius * HitRadius)
				{
					// 데미지는 서버 권위. 클라는 파괴와 폭발만 한다 (#98).
					if (T.Player->HasAuthority())
					{
						const float Applied = T.Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
						UE_LOG(LogTemp, Log, TEXT("[RE] BulletHit: Applied=%.0f"), Applied);
					}
					REExplosionFx::SpawnBulletExplosion(World, BulletLoc);
					Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
					break;   // 투사체 하나는 한 명만 — 몸으로 막는 탱 플레이가 성립한다
				}
```

**`World` 지역 변수를 먼저 만들어야 한다.** 현재 `Execute` 는 `EntityManager.GetWorld()` 를 인라인으로만 쓴다. 람다가 `[&]` 캡처이므로 지역 변수를 선언해두면 그대로 보인다.

`Execute` 의 `GatherHitTargets` 호출부를 이렇게 바꾼다:

```cpp
	// 살아있고 대쉬 중이 아닌 플레이어 전원 (#86). 대상이 없으면 탄을 순회할 이유가 없다.
	UWorld* World = EntityManager.GetWorld();
	TArray<FREHitTarget> Targets;
	GatherHitTargets(World, Targets);
	if (Targets.IsEmpty())
	{
		return;
	}
```

- [ ] **Step 3: 인클루드 추가**

```cpp
#include "REExplosionFx.h"
```

- [ ] **Step 4: 빌드**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REServer Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
Expected: 양쪽 `Result: Succeeded`

- [ ] **Step 5: 데미지 권위가 안 깨졌는지 확인**

**클라에서 `BulletHit: Applied=` 로그가 찍히면 안 된다.** 찍히면 데미지가 이중 적용된다.

Run:
```
.\scripts\dedi-verify.ps1
$d = (Get-ChildItem .\Saved\DediVerify\* -Directory | Select-Object -Last 1).FullName
Get-ChildItem "$d\*.log" | ForEach-Object {
  $n = @(Select-String -LiteralPath $_.FullName -Pattern 'BulletHit: Applied=').Count
  "  $($_.Name): BulletHit 로그 $n 건"
}
```

Expected: `dedi-verify` 전 항목 PASS + **클라 로그에 `BulletHit: Applied=` 0건.** 서버 로그에는 있을 수 있다(플레이어가 맞았다면).

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletHitProcessor.cpp
git commit -m "feat(fx): 직선탄 피격 폭발 + 판정 클라 개방(데미지는 서버 권위) (#98)"
```

---

### Task 6: 곡사탄 착지 — 클라 전용 FX 프로세서

**Files:**
- Create: `Source/Project_RE/Mass/REArcFxProcessor.h`
- Create: `Source/Project_RE/Mass/REArcFxProcessor.cpp`

**Interfaces:**
- Consumes: Task 3 의 `REExplosionFx::SpawnBulletExplosion`
- Produces: 곡사탄 착지 시 폭발. `REArcSimProcessor` 는 **건드리지 않는다**

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/Mass/REArcFxProcessor.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "REArcFxProcessor.generated.h"

/**
 *  곡사탄 착지 폭발 (#98).
 *
 *  REArcSimProcessor 는 워커 스레드에서 도는데 Niagara 스폰은 게임 스레드 전용이다.
 *  시뮬을 GT 로 옮기면 곡사탄이 수천 개로 늘었을 때 확장 여지를 잃으므로, 폭발만
 *  떼어 이 프로세서가 GT 에서 처리한다. 시뮬 앞에 배치해 파괴 전에 위치를 읽는다.
 */
UCLASS()
class UREArcFxProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcFxProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
```

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/Mass/REArcFxProcessor.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcFxProcessor.h"
#include "REBulletFragments.h"
#include "REExplosionFx.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"   // FTransformFragment
#include "Engine/World.h"

UREArcFxProcessor::UREArcFxProcessor()
	: EntityQuery(*this)
{
	// 클라·싱글 전용 — 데디서버는 렌더가 없다.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);

	// Niagara 스폰은 게임 스레드 전용.
	bRequiresGameThreadExecution = true;

	// 시뮬보다 먼저 돈다 — 시뮬이 착지한 탄을 파괴하기 전에 위치를 읽어야 한다.
	ExecutionOrder.ExecuteBefore.Add(TEXT("REArcSimProcessor"));
}

void UREArcFxProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcFxProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	if (!World)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(Context, [World](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FArcBulletFragment> A = Ctx.GetFragmentView<FArcBulletFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			// REArcSimProcessor 와 같은 착지 조건. 이 프로세서가 먼저 돌므로 아직 살아 있다.
			if (A[i].Elapsed >= A[i].FlightTime)
			{
				REExplosionFx::SpawnBulletExplosion(World, T[i].GetTransform().GetLocation());
			}
		}
	});
}
```

- [ ] **Step 3: 빌드**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REServer Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
Expected: 양쪽 `Result: Succeeded`

- [ ] **Step 4: 실행 순서가 실제로 잡혔는지 확인**

`ExecuteBefore` 는 이름이 틀려도 조용히 무시된다. 순서가 안 잡히면 시뮬이 먼저 파괴해 **폭발이 한 번도 안 뜬다.**

Run:
```
.\scripts\profile.ps1 -Bullets 1000 -Label arcfx
$d = (Get-ChildItem .\Saved\Profiling\RE_Mass_1000_arcfx_* -Directory | Select-Object -Last 1).FullName
@(Select-String -LiteralPath "$d\run.log" -Pattern 'NS_REBulletExplosion 로드 실패').Count
@(Select-String -LiteralPath "$d\run.log" -Pattern 'Ensure condition failed|Assertion failed|appError').Count
```

Expected: 두 값 모두 **0**. 폭발이 실제로 떴는지는 Task 7 의 스크린샷에서 확인한다.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REArcFxProcessor.h Source/Project_RE/Mass/REArcFxProcessor.cpp
git commit -m "feat(fx): 곡사탄 착지 폭발 — 클라 전용 GT 프로세서 (#98)"
```

---

### Task 7: 육안 확인 · 부하 측정 · 문서

**Files:**
- Modify: `docs/guides/profiling.md`

**Interfaces:**
- Consumes: Task 5·6 의 폭발
- Produces: 폭발이 화면에 뜬다는 확인과, 개별 스폰이 버티는 규모의 실측값

- [ ] **Step 1: 폭발 스크린샷**

#97 이 만든 `re.Debug.ScreenshotFrame` 을 쓴다. 곡사탄 페이즈는 로테이션 중 4초뿐이라 프레임을 넉넉히 잡는다.

Run:
```
Remove-Item -Recurse -Force .\Saved\Screenshots -ErrorAction SilentlyContinue
$ed='E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$p = Start-Process -FilePath $ed -PassThru -ArgumentList @(
  '"E:\UnrealProjects\Project_RE\Project_RE.uproject"','/Game/Level/Main','-game','-windowed','-ResX=1600','-ResY=900',
  '-ExecCmds="re.Profiling.KeepFiring 1,re.Debug.ScreenshotFrame 2600"',
  '-abslog="E:\UnrealProjects\Project_RE\Saved\fxshot.log"','-nosplash','-NoSound')
Start-Sleep -Seconds 60
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $p.WaitForExit(8000) | Out-Null }
Get-ChildItem .\Saved\Screenshots -Recurse -Filter *.png | ForEach-Object { $_.FullName }
```

**`re.Cheat.PlayerInvincible` 을 넣지 마라** — 무적이면 직선탄 피격이 일어나지 않아 폭발을 못 본다.

PNG 를 열어 폭발이 보이는지 확인한다. 안 보이면 멈추고 원인을 찾아라 — 후보: 실행 순서 미적용(Task 6 Step 4), 에셋 로드 실패, 데디 가드 오작동, 폭발이 너무 작거나 어두움.

- [ ] **Step 2: 색·크기 조정 (필요시)**

폭발이 보이는데 톤이 안 맞으면 `Content/FX/NS_REBulletExplosion` 을 에디터에서 조정한다. #97 실측: **발광이 과하면 블룸이 화면을 씻어 색이 죽는다.** 탄환이 파스텔 분홍/하늘(`sat=0.18, desat=0.40`)이므로 폭발도 그 톤에 맞춘다.

- [ ] **Step 3: 동시 폭발 부하 측정**

개별 스폰(+`AutoRelease` 풀링)이 몇 개까지 버티는지 잰다. 곡사탄 동시 수를 올려 측정한다 — `REBossCharacter.h` 의 `ArtilleryCount` 를 임시로 올린다(측정 후 되돌린다).

Run (각 값마다):
```
.\scripts\profile.ps1 -Bullets 5000 -Label fx<N>
.\scripts\profile-stats.ps1 -RunDir (Get-ChildItem .\Saved\Profiling\RE_Mass_5000_fx<N>_* -Directory | Select-Object -Last 1).FullName
```

`ArtilleryCount` 를 12(현재) / 50 / 200 으로 바꿔가며 `Frame p99` 와 `GT` 를 기록한다. **측정 후 12로 되돌리고, 되돌렸는지 `git diff` 로 확인하라.**

- [ ] **Step 4: 상한 재측정**

판정이 클라에서도 돌게 됐으므로 확인한다.

Run:
```
.\scripts\profile.ps1 -Bullets 50000 -Label fx
.\scripts\profile-stats.ps1 -RunDir (Get-ChildItem .\Saved\Profiling\RE_Mass_50000_fx_* -Directory | Select-Object -Last 1).FullName
```

Expected: `Frame p99` ≤ 16.6 ms. 넘으면 원인을 규명하라 — 현재 상한은 50,000발이다.

- [ ] **Step 5: 문서 기록**

`docs/guides/profiling.md` 에 짧은 절을 더한다:

1. 폭발이 어디서 스폰되는지 (`REExplosionFx::SpawnBulletExplosion` 한 곳)
2. Step 3 의 측정표 — `ArtilleryCount` 별 `Frame p99` / `GT`
3. 그 숫자가 뜻하는 것: 개별 스폰이 버티는 동시 폭발 규모. 넘어서면 NDC 배칭이 필요하다
4. `re.Cheat.PlayerInvincible` 을 켜면 직선탄 폭발이 관측되지 않는다는 함정

**측정하지 않은 값을 쓰지 마라.**

- [ ] **Step 6: 커밋**

```bash
git add docs/guides/profiling.md
git commit -m "docs(profiling): 폭발 부하 측정 기록 (#98)"
```

---

## Self-Review

**1. 스펙 커버리지**

| 스펙 절 | 담당 |
|---|---|
| §2 수명 만료 금지 | Global Constraints. 어느 태스크도 `REBulletSimProcessor` 를 건드리지 않는다 |
| §3 클라 판정 개방 + 데미지 가드 | Task 5 Step 1·2 |
| §3 `REArcHitProcessor` 는 서버 유지 | 어느 태스크도 건드리지 않는다 (의도적) |
| §4 대상 수집 클라 대응 | Task 4 |
| §5 직선탄은 기존 GT 활용 | Task 5 (`bRequiresGameThreadExecution` 유지 명시) |
| §5 곡사탄 별도 FX 프로세서 + `ExecuteBefore` | Task 6 |
| §5 데디 스폰 금지 | Task 3 (`NM_DedicatedServer` 조기 반환) + Task 6 (`Standalone\|Client`) |
| §6 템플릿 복제 + Python 부트스트랩 | Task 2 |
| §7 `AutoRelease` 풀링 | Task 3 Step 2 |
| §7 스폰 단일 함수 | Task 3 |
| §7 규모 측정 | Task 7 Step 3·5 |
| §8 플러그인 + 실제 로드 확인 | Task 1 Step 4·5 |
| §9 검증 게이트 7종 | Task 1 Step 5, Task 4 Step 4, Task 5 Step 5, Task 6 Step 4, Task 7 Step 1·3·4 |

빠진 요구사항 없음.

**2. 플레이스홀더 스캔**

TBD/TODO 없음. 모든 코드 단계에 실제 코드가 있고, 모든 검증 단계에 실제 명령과 기대값이 있다. Niagara API 시그니처는 엔진 헤더로 확인했다(`NiagaraFunctionLibrary.h:93`, `NiagaraComponentPoolMethodEnum.h:20`).

**3. 이름 일관성**

`REExplosionFx::SpawnBulletExplosion(const UWorld*, const FVector&)` 가 Task 3 에서 정의되고 Task 5·6 이 같은 시그니처로 호출한다. 에셋 경로 `/Game/FX/NS_REBulletExplosion` 이 Task 2(생성)와 Task 3(로드)에서 일치한다. `UREArcFxProcessor` 클래스명이 헤더·구현·`ExecuteBefore` 대상(`REArcSimProcessor`)과 어긋나지 않는다.

**4. 알려진 위험**

- `ExecutionOrder.ExecuteBefore` 는 이름이 틀려도 조용히 무시된다 → Task 6 Step 4 에서 폭발 미발생으로 드러나고, Task 7 Step 1 의 스크린샷이 최종 확인이다
- `ENCPoolMethod` 가 전방 선언만일 수 있다 → Task 3 Step 3 에 대응 인클루드를 명시했다
- Task 4 의 캐릭터 순회는 **서버 동작도 바꾼다**(컨트롤러 없는 캐릭터가 대상에 들어올 수 있다) → Task 4 Step 4 의 `dedi-verify` 가 게이트다
