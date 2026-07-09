# M1 #17 ISM 렌더 Processor + 탄막 데모 영상 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `UREBulletRenderProcessor::Execute`를 채워 live 탄환의 `FTransformFragment`를 매 프레임 ISM 인스턴스로 그린다. 렌더 서브시스템이 홀더 액터+ISM(빨강 엔진 구체)을 소유하고, GameMode가 지속 발사 데모 루프를 돌려 탄막 영상 소스를 만든다.

**Architecture:** 매틱 재구성 전략 — Execute가 live 탄환 트랜스폼을 모아 ISM 인스턴스 수를 그 수에 맞추고(부족하면 AddInstance, 남으면 꼬리에서 RemoveInstance → swap 없음) i번째 인스턴스를 i번째 탄환으로 UpdateTransform. `FBulletRenderFragment.InstanceIndex`는 미사용(프레임마다 i 재배정). ISM은 액터에만 붙으므로 `UREBulletRenderSubsystem`(UWorldSubsystem)이 홀더 액터를 스폰해 소유하고 Processor가 `World->GetSubsystem`으로 O(1) 획득.

**Tech Stack:** UE 5.8 C++. `UInstancedStaticMeshComponent`(Engine 모듈, 기존 링크), `UWorldSubsystem`, Mass `FMassEntityQuery`/`FMassExecutionContext`. 신규 모듈·플러그인 없음.

## Global Constraints

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `UInstancedStaticMeshComponent`는 Engine 모듈(기존 의존). MassGameplay 플러그인은 #15에서 활성(Sim/Render Processor 구동용).
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공** + **headless 프로브 로그 관측**(`[[headless-runtime-probe]]`, `MSYS_NO_PATHCONV=1` 필수).
- 로그 접두어 `[RE]` 고정. 클래스/타입명 스펙과 동일(`UREBulletRenderSubsystem`, `UREBulletRenderProcessor`).
- 브랜치: `feature/M1-bullet-archetype-spawn`(현재). PR base=dev.
- 매틱 재구성 전략 확정 — 영속 InstanceIndex/Observer Processor는 스코프 밖. Niagara/HISM/배치 최적화는 M3/M6.
- 영상 파일 녹화는 **수동**(PIE+카메라+화면녹화) — 코드 밖. 계획은 코드 + headless 카운트 검증까지.

**사용 API (전부 기존/표준 — UE 5.8 엔진 헤더 확인):**
- `UWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)` — `Public/Subsystems/WorldSubsystem.h:43`.
- `UWorld::IsGameWorld()` `World.h:4172`, `UWorld::GetNetMode()`, `UWorld::SpawnActor<AActor>()`.
- `AActor::SetRootComponent`, `UActorComponent::RegisterComponent`.
- `UInstancedStaticMeshComponent`: `AddInstance(FTransform, bool bWorldSpace=false)`(:271), `RemoveInstance(int32)`(:417), `UpdateInstanceTransform(int32, FTransform, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)`(:375), `GetInstanceCount()`(:435), `SetStaticMesh`, `CreateDynamicMaterialInstance(int32, UMaterialInterface*)`(`PrimitiveComponent.h:1629`).
- 애셋: `/Engine/BasicShapes/Sphere.Sphere`, `/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial`(둘 다 존재 확인). 머티리얼 벡터 파라미터명 `Color`.
- `FMassEntityManager::GetWorld()` `MassEntityManager.h:1353`.
- `FMassExecutionContext::GetFragmentView<T>()`(RO), `GetNumEntities()`.
- `FTransformFragment`(`Mass/EntityFragments.h`, MassCore), `FBulletTag`(`REBulletFragments.h`).
- `FTimerManager::SetTimer(FTimerHandle&, FTimerDelegate, float, bool bLoop)`, `FTimerDelegate::CreateLambda`.

---

### Task 1: `UREBulletRenderSubsystem` — 홀더 액터 + ISM 소유

**Files:**
- Create: `Source/Project_RE/Mass/REBulletRenderSubsystem.h`
- Create: `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp`

**Interfaces:**
- Consumes: 없음(엔진 API만).
- Produces: `UInstancedStaticMeshComponent* UREBulletRenderSubsystem::GetISM() const`. Task 2 Processor가 `World->GetSubsystem<UREBulletRenderSubsystem>()->GetISM()`로 사용.

- [ ] **Step 1: 헤더 작성** — `REBulletRenderSubsystem.h`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "REBulletRenderSubsystem.generated.h"

class AActor;
class UInstancedStaticMeshComponent;

/**
 *  탄막 ISM 소유자. Processor는 UMassProcessor(액터 아님)라 ISM을 직접 못 가진다.
 *  월드 BeginPlay 시 홀더 액터를 스폰해 ISM(빨강 엔진 구체)을 부착하고 GetISM()로 노출.
 *  데디서버(NM_DedicatedServer)는 렌더 불요 → 홀더/ISM 생성 skip(GetISM()이 null).
 */
UCLASS()
class UREBulletRenderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UInstancedStaticMeshComponent* GetISM() const { return ISM; }

private:
	UPROPERTY()
	TObjectPtr<AActor> Holder = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ISM = nullptr;
};
```

- [ ] **Step 2: 구현 작성** — `REBulletRenderSubsystem.cpp`

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UREBulletRenderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 데디서버는 렌더 안 함. 게임 월드(PIE/스탠드얼론)만 ISM 생성.
	if (InWorld.GetNetMode() == NM_DedicatedServer || !InWorld.IsGameWorld())
	{
		return;
	}

	Holder = InWorld.SpawnActor<AActor>();
	if (!Holder)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] RenderSubsystem: Holder spawn failed"));
		return;
	}

	ISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	Holder->SetRootComponent(ISM);
	ISM->RegisterComponent();

	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ISM->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dyn = ISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);  // 탄환 빨강
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] RenderSubsystem: ISM ready (mesh=%d)"), ISM->GetStaticMesh() != nullptr);
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
git add Source/Project_RE/Mass/REBulletRenderSubsystem.h Source/Project_RE/Mass/REBulletRenderSubsystem.cpp
git commit -m "feat(M1): add bullet render subsystem (ISM holder) (#17)"
```

---

### Task 2: `UREBulletRenderProcessor` Execute — 매틱 ISM 재구성

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` (ConfigureQueries + Execute 전체 교체)

**Interfaces:**
- Consumes: `UREBulletRenderSubsystem::GetISM()`(Task 1), `FTransformFragment`(#14), `FBulletTag`(#14).
- Produces: 없음(렌더 사이드이펙트). ISM 인스턴스 수/트랜스폼이 live 탄환을 추종.

- [ ] **Step 1: `REBulletRenderProcessor.cpp` — include 교체**

기존 include 블록(라인 3~5)을 아래로 교체:
```cpp
#include "REBulletRenderProcessor.h"
#include "REBulletFragments.h"
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
```

- [ ] **Step 2: `REBulletRenderProcessor.cpp` — 파일 상단 익명 namespace에 상수 추가**

`UREBulletRenderProcessor::UREBulletRenderProcessor()` 생성자 정의 **바로 위**에 추가:
```cpp
namespace
{
	/** 탄환 인스턴스 스케일 — 엔진 Sphere(반경 50cm)를 반경 ~10cm로 축소. */
	constexpr float BulletScale = 0.2f;
}
```

- [ ] **Step 3: `REBulletRenderProcessor.cpp` — ConfigureQueries 교체**

기존 `ConfigureQueries` 본문(FBulletSimFragment RO + FBulletRenderFragment RW 두 줄)을 교체:
```cpp
void UREBulletRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);  // 위치 읽기
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);          // 탄환만 선별
}
```

- [ ] **Step 4: `REBulletRenderProcessor.cpp` — Execute 교체**

기존 `Execute` 본문(`UE_LOG` 스텁)을 교체:
```cpp
void UREBulletRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
	UInstancedStaticMeshComponent* ISM = RS ? RS->GetISM() : nullptr;
	if (!ISM)
	{
		return;  // 데디서버 등 ISM 없으면 no-op
	}

	// 1) live 탄환 트랜스폼 수집 (청크를 가로질러 누적 → 전역 인스턴스 인덱스 연속).
	TArray<FTransform> Xf;
	EntityQuery.ForEachEntityChunk(Context, [&Xf](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(BulletScale));  // 탄환 크기 통일
			Xf.Add(B);
		}
	});

	// 2) 인스턴스 수를 M에 맞춤 (꼬리에서 add/remove → 타 인덱스 불변, swap 없음).
	const int32 M = Xf.Num();
	int32 Count = ISM->GetInstanceCount();
	while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
	while (Count > M) { ISM->RemoveInstance(Count - 1);                               --Count; }

	// 3) i번째 인스턴스 = i번째 live 탄환. dirty flush는 마지막 1회만.
	for (int32 i = 0; i < M; ++i)
	{
		ISM->UpdateInstanceTransform(i, Xf[i], /*bWorldSpace=*/true,
			/*bMarkRenderStateDirty=*/(i == M - 1), /*bTeleport=*/true);
	}

	// 프로브: 인스턴스 수 == live 탄환 수 추종 확인 (매 30틱 1회, 로그 과다 방지).
	static int32 ProbeTick = 0;
	if (((ProbeTick++) % 30) == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[RE] RenderProbe: live=%d ISM.Count=%d"), M, ISM->GetInstanceCount());
	}
}
```

- [ ] **Step 5: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletRenderProcessor.cpp
git commit -m "feat(M1): fill render processor Execute (per-frame ISM rebuild) (#17)"
```

---

### Task 3: 데모 발사 루프 + headless 카운트 검증

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h` (DemoFireTimer + DemoBoss 멤버)
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (지속 발사 타이머)

**Interfaces:**
- Consumes: Task 1/2 산출(ISM 갱신 경로), `AREBossCharacter::TriggerBulletPattern`(#16).
- Produces: 없음(검증 스캐폴딩 + 영상 소스). headless 로그로 ISM 카운트 추종 실증.

- [ ] **Step 1: `REGameMode.h` — 타이머/보스 멤버 추가**

`REGameMode.h` 상단 include에 추가(기존 include 블록 끝):
```cpp
#include "Engine/TimerHandle.h"
```

전방 선언 필요 시 파일에 없으면 추가:
```cpp
class AREBossCharacter;
```

클래스 `private:`(또는 기존 `ProbeBullet` 멤버 근처)에 추가:
```cpp
	/** 데모: 주기적 Spiral 발사로 지속 탄막(영상 소스). */
	FTimerHandle DemoFireTimer;

	UPROPERTY()
	TObjectPtr<AREBossCharacter> DemoBoss = nullptr;
```

- [ ] **Step 2: `REGameMode.cpp` — include 추가**

상단 include에 추가:
```cpp
#include "TimerManager.h"
```

- [ ] **Step 3: `REGameMode.cpp` — 보스 참조 저장 + 데모 타이머 기동**

기존 Boss 스폰 블록(현재 `if (AREBossCharacter* Boss = ...)` 블록, 라인 49~54)에서, `Boss->TriggerBulletPattern(...)` 2회 프로브 호출은 **유지**하고, 블록 안 끝에 보스 저장 + 타이머 기동 추가:
```cpp
	if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(
			AREBossCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, BossSpawnParams))
	{
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);  // #16 프로브: BaseAngle 누적 확인

		// #17 데모: 0.1초마다 Spiral 발사 → 회전 나선 탄막 지속(영상 소스 + ISM 카운트 추종 검증).
		DemoBoss = Boss;
		FTimerDelegate FireDel = FTimerDelegate::CreateLambda([this]()
		{
			if (DemoBoss)
			{
				DemoBoss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
			}
		});
		GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, 0.1f, /*bLoop=*/true);
	}
```

- [ ] **Step 4: 빌드 (컴파일 게이트)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 5: headless 런타임 프로브 (Acceptance)**

PIE 없이 headless. Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe17.log &
sleep 30
grep -E "\[RE\] (RenderSubsystem|RenderProbe)" "Saved/Logs/RE_probe17.log" | head -40
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected 로그:
```
[RE] RenderSubsystem: ISM ready (mesh=1)
[RE] RenderProbe: live=33 ISM.Count=33     ← 초기 프로브(32 스파이럴 + 1 심프로브)
[RE] RenderProbe: live=161 ISM.Count=161   ← 데모 타이머 누적, 두 수 항상 일치
...
[RE] RenderProbe: live=480 ISM.Count=480   ← 정상상태 근처 (10발/s × 16 × 3s Lifetime)
```
**합격 기준 2개:**
1. **ISM 준비:** `RenderSubsystem: ISM ready (mesh=1)` — 홀더/ISM/구체 메시 로드 성공.
2. **카운트 추종:** 모든 `RenderProbe` 줄에서 `live == ISM.Count`, 그리고 값이 0에서 증가(스폰 반영) 후 정상상태 유지(파괴 반영으로 폭주 안 함).

**nullrhi 리스크 대응:** `ISM.Count`가 항상 0이거나 `live`와 어긋나면 = nullrhi에서 ISM 인스턴스 데이터가 갱신 안 되는 것. 그 경우 **헤드리스는 `live`(순수 Mass 수집 수)만 실증**으로 강등하고(합격 기준 2를 "live가 스폰/파괴에 따라 증감"으로 축소), 실제 ISM 반영은 아래 수동 검증으로 이관. 이 분기는 프로브 결과 보고 판정.

- [ ] **Step 6: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "test(M1): add demo fire loop + ISM count probe (#17)"
```

---

## 완료 후

- **수동 영상 검증(M1 마일스톤 완료 조건):** 에디터에서 `/Game/Level/Main` PIE 실행 → 원점 보스가 0.1초마다 회전 나선 발사, 빨강 구체 탄막이 바깥으로 이동. 탑다운 카메라(기존 topdown 셋업)로 육안 확인 후 화면 녹화 → **탄막 데모 영상** 산출. 이 단계는 물리적 수동 작업이라 코드 밖.
- **프로브 스캐폴딩 처리:** GameMode의 #15/#16 프로브 로그 + #17 `RenderProbe`(Execute 내 static 카운터)는 M1 회귀 검증 수단. M1 마감/데모 확정 후 정리 대상(별도 정리 커밋). 지금 제거하면 검증 수단 소실.
- **후속 훅:** 매틱 재구성은 M3 5000발까지 동작하나 매 프레임 add/remove/update 비용은 M3 프로파일링에서 `AddInstances`(배치)/HISM로 최적화 여지. Niagara 렌더 교체는 M6. `FBulletRenderFragment.InstanceIndex`는 미사용이나 M5 시드 경로 여지로 존치(삭제 안 함).

## Self-Review

- **Spec coverage:** 스펙 §컴포넌트1(RenderSubsystem h/cpp)→T1, §컴포넌트2(RenderProcessor ConfigureQueries+Execute)→T2, §컴포넌트3(데모 발사 루프)→T3 Step1~3, §검증-헤드리스(ISM.Count==live)→T3 Step5, §검증-수동(PIE 녹화)→완료 후. §API 근거 전부 Global Constraints에 반영. 갭 없음.
- **Placeholder scan:** 코드 블록 전부 완전(TBD/추후 없음). "전방 선언 필요 시"는 조건부지만 실제 코드(`class AREBossCharacter;`) 명시 — placeholder 아님. `RenderProbe` static 카운터는 스펙이 요구한 검증 스캐폴딩.
- **Type consistency:** `GetISM() -> UInstancedStaticMeshComponent*`(T1 선언) ↔ T2 사용 일치. `OnWorldBeginPlay(UWorld&)` = 엔진 시그니처(:43). `AddInstance/RemoveInstance/UpdateInstanceTransform/GetInstanceCount` 인자 = 엔진 헤더 확인 시그니처와 일치. `TriggerBulletPattern(EBulletPattern, int32, float)` = #16 시그니처. `SetTimer(FTimerHandle&, FTimerDelegate, float, bool)` + `CreateLambda` 정확(직접 람다 오버로드 아님).
- **재구성 정합성:** 꼬리 remove(`RemoveInstance(Count-1)`)는 삭제 대상=마지막이라 swap이 자기 자신 → 타 인덱스 불변. add/remove 후 `Count==M` 보장 → 이어지는 `for i in 0..M` UpdateInstanceTransform 인덱스 전부 유효.
- **넷모드:** RenderProcessor `ExecutionFlags=5`(Standalone|Client) — headless `-game`는 NM_Standalone → Execute 실행. RenderSubsystem은 NM_DedicatedServer만 skip → NM_Standalone에서 ISM 생성. 정합.
- **수집-후-일괄 근거:** `ForEachEntityChunk` 콜백은 청크별. 전역 인스턴스 인덱스가 청크 경계 넘어 연속되려면 먼저 `Xf`로 모아야 함(콜백 안에서 직접 AddInstance 하면 인덱스 꼬임). 수집→재구성→업데이트 3단 분리로 해결.
- **Include 검증:** T1 cpp — InstancedStaticMeshComponent/StaticMesh/MaterialInstanceDynamic/Actor/World 전부 Engine 모듈(기존 링크). T2 cpp — EntityFragments.h(MassCore, #15 확인)/RenderSubsystem.h/InstancedStaticMeshComponent.h/World.h. T3 — TimerManager.h + TimerHandle.h. Build.cs 변경 불요.
- **nullrhi 리스크 명시:** T3 Step5에 분기 판정 절차 기록(ISM.Count 갱신되면 카운트 검증, 아니면 live-only로 강등 + 수동 이관). 미해결 가정 없음 — 프로브가 어느 쪽인지 결정.
