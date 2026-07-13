# M2 #27 보스 탄막 피격 판정 (Mass↔플레이어) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 신규 `UREBulletHitProcessor`가 매 틱 서버에서 플레이어-탄환 XY 거리 판정 → 히트 시 `TakeDamage` + 엔티티 지연 파괴.

**Architecture:** 기존 Sim/Render 프로세서 패턴의 세 번째 프로세서. `ExecutionFlags = Standalone|Server`(서버 권위), `bRequiresGameThreadExecution = true`(TakeDamage 액터 호출), `ExecuteAfter(SimProcessor)`(이동 후 같은 프레임 판정). 탄환은 액터가 아니므로 물리 오버랩 불가 — `DistSquaredXY` 전수 비교.

**Tech Stack:** UE 5.8 C++, MassEntity(이미 Build.cs 의존) + Engine(GameplayStatics/DamageEvents). 신규 모듈·플러그인 없음.

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — MassEntity·MassGameplay(페이즈 드라이버) 이미 활성, GameplayStatics·FDamageEvent는 Engine 모듈.
- **브랜치:** `feature/M2-bullet-hit` — `dev`에서 분기. **주의: 분기 전 dash WIP(`REGA_Dash.h`, `REPlayerController.*` 수정분) stash 또는 커밋 필요** (dev에 dash 파일 없음 → 클린 checkout 불가 상태, 2026-07-13 기준). 스펙/플랜 문서도 이 브랜치 첫 커밋에 포함. PR base=dev.
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그** (`[[headless-runtime-probe]]`) + **PIE 육안**.
- 로그 접두어 `[RE]` 고정. 클래스/상수명 스펙(`2026-07-13-bullet-player-hit-design.md`)과 동일.
- YAGNI: 사망 처리(M5), HP바(별도 이슈), i-frame, 멀티 순회, 공간 분할, EventInstigator 귀속 전부 스코프 밖.

**검증된 API (UE 5.8 실물 — 파일:라인):**
- `FVector::DistSquaredXY(V1, V2)` — `Math/Vector.h:1050` (static, sqrt 없음)
- `UGameplayStatics::GetPlayerPawn(WorldContextObject, PlayerIndex)` — `Kismet/GameplayStatics.h:201`
- `FDamageEvent` — `Engine/DamageEvents.h:15` (UE5는 명시 include 필수)
- `FMassProcessorExecutionOrder::ExecuteAfter` — `TArray<FName>`, `MassProcessor.h:54` (멤버 `ExecutionOrder`는 `:238`)
- `AActor::TakeDamage(float, FDamageEvent const&, AController*, AActor*)` — `Actor.h:3660`
- `ARECharacterBase::TakeDamage` — `Core/RECharacterBase.cpp:73` (`HasAuthority()` 가드 + Health Clamp 차감)
- `ForEachEntityChunk` / `GetFragmentView` / `Defer().DestroyEntity` / `GetEntity(i)` — #15 플랜에서 검증 완료, 동일 사용
- 프로젝트 include 관례: 타 폴더는 모듈 루트 기준 (`Core/RECharacterBase.h`, `Abilities/REGameplayTags.h` 선례)

---

### Task 1: UREBulletHitProcessor 신규

**Files:**
- Create: `Source/Project_RE/Mass/REBulletHitProcessor.h`
- Create: `Source/Project_RE/Mass/REBulletHitProcessor.cpp`

**Interfaces:**
- Consumes: `FTransformFragment`(RO, Sim이 전진시킨 위치), `FBulletTag`(필터), `ARECharacterBase::TakeDamage`(서버 권위 데미지 진입점), `UREBulletSimProcessor::StaticClass()`(ExecuteAfter 순서).
- Produces: 없음(런타임 동작). 파괴된 엔티티는 RenderProcessor가 다음 재구성에서 자동 제거.

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/Mass/REBulletHitProcessor.h` 신규 (Sim/Render 헤더와 동일 골격):
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REBulletHitProcessor.generated.h"

/**
 *  탄막→플레이어 피격 판정 Processor.
 *  ExecutionFlags = Standalone|Server: 판정/데미지는 서버 권위. 클라는 복제 Health만 소비.
 *  탄환은 액터가 아님(ISM 인스턴스) → 물리 오버랩 불가, XY 거리 비교로 판정.
 */
UCLASS()
class UREBulletHitProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREBulletHitProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
```

- [ ] **Step 2: cpp 작성**

`Source/Project_RE/Mass/REBulletHitProcessor.cpp` 신규:
```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletHitProcessor.h"
#include "REBulletFragments.h"
#include "REBulletSimProcessor.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Core/RECharacterBase.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	/** 히트 반경(cm) — 탄환 시각 반경 25(BulletScale 0.5 × Sphere 50) + 플레이어 캡슐 반경 ~35. */
	constexpr float HitRadius = 60.f;
	/** 탄환 1발 데미지 — 100 HP 기준 10발 사망. */
	constexpr float BulletDamage = 10.f;
}

UREBulletHitProcessor::UREBulletHitProcessor()
	: EntityQuery(*this)
{
	// 서버 권위 판정만 — 싱글(Standalone) + 데디서버(Server). 클라 실행 없음.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);

	// TakeDamage는 액터 호출 — 게임 스레드 전용.
	bRequiresGameThreadExecution = true;

	// 이동 후 판정 — Sim이 위치를 전진시킨 뒤 같은 프레임에 히트 체크.
	ExecutionOrder.ExecuteAfter.Add(UREBulletSimProcessor::StaticClass()->GetFName());
}

void UREBulletHitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
}

void UREBulletHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	ARECharacterBase* Player = Cast<ARECharacterBase>(Pawn);
	if (!Player)
	{
		return;  // 플레이어 없으면 no-op (레벨 전환 등)
	}

	const FVector PlayerLoc = Player->GetActorLocation();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			// 탑다운 — XY 평면 거리만 비교 (탄환 Z와 캡슐 중심 Z 불일치 함정 회피)
			if (FVector::DistSquaredXY(Transforms[i].GetTransform().GetLocation(), PlayerLoc)
				<= HitRadius * HitRadius)
			{
				const float Applied = Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
				UE_LOG(LogTemp, Log, TEXT("[RE] BulletHit: Applied=%.0f"), Applied);
			}
		}
	});
}
```

- [ ] **Step 3: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletHitProcessor.h Source/Project_RE/Mass/REBulletHitProcessor.cpp
git commit -m "feat(M2): add REBulletHitProcessor bullet-player hit detection (#27)"
```

---

### Task 2: 검증 — headless 프로브 + PIE

신규 코드 없음. 보스가 이미 패턴 탄막을 발사 중(#16)이므로 플레이어(PlayerStart, headless라 무입력 정지)가 탄막에 맞으면 Task 1의 `[RE] BulletHit` 로그가 찍힌다.

**Files:** 없음 (관측만)

**Interfaces:**
- Consumes: Task 1의 `[RE] BulletHit` 로그, 기존 보스 발사 루프(#16), `ARECharacterBase` Health Replicated.
- Produces: 이슈 #27 완료 기준 실증.

- [ ] **Step 1: headless 런타임 프로브**

Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe27.log &
sleep 45
grep "\[RE\] BulletHit" "Saved/Logs/RE_probe27.log" | head -20
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected:
```
[RE] BulletHit: Applied=10
[RE] BulletHit: Applied=10
...
```
**합격 기준:** `BulletHit: Applied=10` 1회 이상. `Applied=10` = `Super::TakeDamage` 정상 반환 = `HasAuthority()` 가드 통과 = 서버에서 `Health -= 10` 실행됨(RECharacterBase.cpp:83). 히트 탄환은 같은 프레임 지연 파괴.

**미관측 시:** 정지 플레이어 위치를 탄막이 안 지나는 패턴일 수 있음 — Step 2 PIE에서 직접 탄막에 걸어 들어가 확인 (프로브는 보조, PIE가 이슈 완료 기준).

- [ ] **Step 2: PIE 육안 확인 (이슈 완료 기준)**

에디터에서 PIE 실행 후:
1. **피격:** 캐릭터를 탄막에 접촉 → Output Log에 `[RE] BulletHit`, 디테일 패널 `Health` 감소 (100 → 90 → ...), 해당 탄환 시각적으로 소멸.
2. **회피:** 우클릭 이동으로 탄막 회피 → Health 무변화. (대쉬 #25는 dev 미머지 — 이 브랜치에 없음.)

둘 다 확인되면 이슈 #27 완료 기준 충족: "탄환이 플레이어 반경 진입 시 서버에서 Health 감소 + 탄환 제거. PIE에서 회피/피격 동작 확인."

- [ ] **Step 3: PR 생성**

`gh pr create` — base `dev`, head `feature/M2-bullet-hit`. 이슈 #27 메타(label `mass-entity`+`C++`, milestone M2, assignee) 미러링, 본문 6개 필드 규칙 준수 (`[[pr-creation-convention]]`).

---

## 완료 후

- `[RE] BulletHit` 로그는 존치 — 히트 시에만 출력(탄환 파괴로 재히트 없음), M2 HP바 이슈 검증에도 재사용 가능.
- 후속 M2: HP바 위젯(월드스페이스)이 Replicated Health 소비. 사망 처리는 M5 (RECharacterBase.cpp:84 TODO).

## Self-Review

- **Spec coverage:** 스펙 §생성자→T1 Step2 생성자(플래그/GT/순서), §ConfigureQueries→T1 Step2, §Execute→T1 Step2, §상수→익명 namespace `HitRadius`/`BulletDamage`, §검증 1(빌드)→T1 Step3, §검증 2·3(PIE 피격/회피)→T2 Step2, §검증 4(headless)→T2 Step1. 갭 없음.
- **Placeholder scan:** 코드 블록 전부 완전. TBD/TODO 없음 (RECharacterBase의 M5 TODO는 기존 코드 인용).
- **Type consistency:** `EntityQuery(*this)` 초기화·`ConfigureQueries(TSharedRef)` 시그니처 = Sim/Render 실물 동일. `DistSquaredXY` static 2인자, `TakeDamage` 4인자 시그니처 = Actor.h:3660 일치. `TConstArrayView<FTransformFragment>` + `GetFragmentView`(RO) 짝 = Render 프로세서 선례. `ExecuteAfter`는 `TArray<FName>` ← `StaticClass()->GetFName()` = FName. 스펙과 클래스/상수명 동일.
- **로그 추가 (스펙 대비 +1줄):** 스펙 Execute에는 없던 `UE_LOG` 1줄 추가 — headless 프로브 관측 수단(Global Constraints의 검증 게이트 요구). 스펙 위반 아닌 검증 계측.
