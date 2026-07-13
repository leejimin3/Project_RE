# 구현 목표: M2 #27 — 보스 탄막 피격 판정 (Mass↔플레이어)

## 컨텍스트

UE 5.8 탑다운 탄막(bullet-hell) 프로젝트 `Project_RE`. 탄환은 액터가 아니라 MassEntity(ISM 인스턴스 렌더) — 물리 오버랩 불가.
이슈 **#27** ([M2] 보스 탄막 피격 판정), 마일스톤 **M2: 플레이어 게임루프**.
이 goal: 신규 `UREBulletHitProcessor` — 매 틱 서버에서 플레이어-탄환 XY 거리 판정, 히트 시 `TakeDamage` + 엔티티 지연 파괴.

스코프 밖(손대지 말 것): 사망 처리(M5), HP바 위젯(M2 별도 이슈), i-frame, 멀티플레이어 순회, 공간 분할, EventInstigator 보스 귀속.

설계 스펙: docs/superpowers/specs/2026-07-13-bullet-player-hit-design.md
상세 플랜: docs/superpowers/plans/2026-07-13-bullet-player-hit.md
(참고 가능. 단 아래 코드가 최종 정본.)

## 선행조건 (전부 dev 머지 확인됨, 2026-07-13)

- M1 탄막 파이프라인 #14~#17: `FBulletTag` 아키타입 스폰 + SimProcessor 이동/수명 + RenderProcessor ISM + 패턴 발사 루프 — dev 존재.
- `ARECharacterBase` (M0) + ASC(#23) + 우클릭 이동(#24) — dev 존재. 플레이어 폰이 Main 레벨에서 스폰됨(`GetPlayerPawn(0)` 전제).
- MassGameplay 플러그인 활성(.uproject) — 프로세서 페이즈 드라이버. 미활성이면 Execute 0회 실행(dead code).
- 대쉬(#25, PR #32) + 자동사격(#26, PR #33) — dev 머지 완료. PIE 회피 확인에 우클릭 이동 + 대쉬(스페이스) 모두 사용 가능.

## 브랜치

`dev`에서 분기: `feature/M2-bullet-hit`

스펙/플랜/goal 문서(untracked)는 분기 시 자동으로 따라옴 → 이 브랜치 첫 커밋에 포함:

```bash
git add docs/superpowers/specs/2026-07-13-bullet-player-hit-design.md docs/superpowers/plans/2026-07-13-bullet-player-hit.md docs/superpowers/goals/2026-07-13-bullet-player-hit-goal.md
git commit -m "docs(M2): bullet-player hit detection spec + plan + goal (#27)"
```

## 전역 제약

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — MassEntity·MassGameplay(페이즈 드라이버) 이미 활성, GameplayStatics·FDamageEvent는 Engine 모듈.
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그** + **PIE 육안**.
- 로그 접두어 `[RE]` 고정. 클래스/상수명 아래 코드와 동일 (`UREBulletHitProcessor`, `HitRadius`, `BulletDamage`).
- PR base=dev.

## 검증된 API (실물 확인됨 — UE 5.8 엔진 헤더 파일:라인)

- `FVector::DistSquaredXY(V1, V2)` — `Math/Vector.h:1050` (static, sqrt 없음)
- `UGameplayStatics::GetPlayerPawn(WorldContextObject, PlayerIndex)` — `Kismet/GameplayStatics.h:201`
- `FDamageEvent` — `Engine/DamageEvents.h:15` (UE5는 명시 include 필수)
- `FMassProcessorExecutionOrder::ExecuteAfter` — `TArray<FName>`, `MassProcessor.h:54` (멤버 `ExecutionOrder`는 `:238`)
- `AActor::TakeDamage(float, FDamageEvent const&, AController*, AActor*)` — `Actor.h:3660`
- `ARECharacterBase::TakeDamage` — `Core/RECharacterBase.cpp:77` (`HasAuthority()` 가드 + `Health = Clamp(Health - Applied, 0, MaxHealth)`, Health는 Replicated)
- `ForEachEntityChunk(Context, lambda)` / `GetFragmentView<T>()` / `Defer().DestroyEntity(handle)` / `GetEntity(i)` — #15 플랜에서 검증 완료, Sim/Render 프로세서와 동일 사용
- 프로젝트 include 관례: 타 폴더는 모듈 루트 기준 (`Core/RECharacterBase.h`, `Abilities/REGameplayTags.h` 선례)

## 기존 파일 현황 (참조 대상 — 수정 없음)

- `Source/Project_RE/Mass/REBulletFragments.h` — `FBulletSimFragment`(Velocity, Lifetime), `FBulletRenderFragment`, `FBulletTag`(쿼리 필터 태그) 정의됨.
- `Source/Project_RE/Mass/REBulletSimProcessor.h/.cpp` — `AllNetModes`, 매 틱 위치 전진 + 수명 파괴. 이 프로세서 **뒤에** 히트 판정 실행해야 함(ExecuteAfter).
- `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` — 매 프레임 live 탄환 전체로 ISM 재구성 → 엔티티 파괴만 하면 렌더 제거 자동. `BulletScale = 0.5`(시각 반경 ~25cm).
- `Source/Project_RE/Core/RECharacterBase.h/.cpp` — `TakeDamage` 서버 권위 진입점(HasAuthority 가드 내장), `Health = 100.f` Replicated.
- 신규 파일 2개만 생성. 기존 파일 변경 없음.

================================================================
## TASK 1: UREBulletHitProcessor 신규
================================================================

### 1-1. `Source/Project_RE/Mass/REBulletHitProcessor.h` (신규)

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

### 1-2. `Source/Project_RE/Mass/REBulletHitProcessor.cpp` (신규)

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

### 1-3. 빌드/검증 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-4. 커밋

```bash
git add Source/Project_RE/Mass/REBulletHitProcessor.h Source/Project_RE/Mass/REBulletHitProcessor.cpp
git commit -m "feat(M2): add REBulletHitProcessor bullet-player hit detection (#27)"
```

================================================================
## TASK 2: 검증 — headless 프로브 + PIE
================================================================

신규 코드 없음. 보스가 이미 패턴 탄막 발사 중(#16) — 플레이어(PlayerStart, headless라 무입력 정지)가 탄막에 맞으면 TASK 1의 `[RE] BulletHit` 로그가 찍힌다.

### 2-1. headless 런타임 프로브

Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe27.log &
sleep 45
grep "\[RE\] BulletHit" "Saved/Logs/RE_probe27.log" | head -20
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대:
```
[RE] BulletHit: Applied=10
[RE] BulletHit: Applied=10
```
**합격 기준:** `BulletHit: Applied=10` 1회 이상. `Applied=10` = `Super::TakeDamage` 정상 반환 = `HasAuthority()` 가드 통과 = 서버에서 `Health -= 10` 실행(RECharacterBase.cpp:87).

**미관측 시:** 정지 플레이어 위치를 탄막이 안 지나는 패턴일 수 있음 — 2-2 PIE에서 직접 탄막에 걸어 들어가 확인 (프로브는 보조, PIE가 이슈 완료 기준).

### 2-2. PIE 육안 확인 (이슈 완료 기준 — 사용자 확인 필요)

에디터 PIE에서:
1. **피격:** 캐릭터를 탄막에 접촉 → Output Log `[RE] BulletHit`, 디테일 패널 `Health` 감소(100→90→...), 해당 탄환 시각적 소멸.
2. **회피:** 우클릭 이동 또는 대쉬(스페이스, #25 dev 머지됨)로 탄막 회피 → Health 무변화.

둘 다 확인 = 이슈 #27 완료 기준 충족.

## 완료 후

- PR 생성: `gh pr create` — base `dev`, head `feature/M2-bullet-hit`. 이슈 #27 메타 미러링: label `mass-entity` + `C++`, milestone `M2: 플레이어 게임루프`, assignee `leejimin3`, project 동일. PR 본문 6개 필드 전부 채움, Reviewer 생략.
- `[RE] BulletHit` 로그 존치 — 히트 시에만 출력(탄환 파괴로 재히트 없음), M2 HP바 이슈 검증에 재사용.
- `RECharacterBase.cpp:88`의 `TODO M5`(사망 처리) 마커는 그대로 둠 — M5 몫.

## 하지 말 것 (스코프 밖)

- 사망 처리 / 피격 이펙트 → M5
- HP바 위젯 → M2 별도 이슈
- i-frame(피격 무적) / 대쉬 무적 → 미요청
- 멀티 플레이어 순회 (`GetPlayerPawn(0)` 단일 고정) → 후속 마일스톤
- 공간 분할(그리드/쿼드트리) → 프로파일링에서 필요 시
- 보스 귀속 데미지(EventInstigator nullptr 유지) → 킬크레딧 필요해질 때
- 기존 파일(Sim/Render 프로세서, RECharacterBase 등) 수정 금지 — 신규 2파일만
