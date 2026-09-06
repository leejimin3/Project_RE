# 구현 목표: M1 #17 — ISM 렌더 Processor + 탄막 데모 영상

## 컨텍스트

UE 5.8 C++ 탄막(bullet-hell) 프로젝트. Mass Entity로 보스 탄막을 시뮬/렌더한다. 이 goal은 **이슈 #17**(마일스톤 "M1: Mass 보스 탄막 스폰 + 이동 (싱글)"의 마지막 이슈).

하는 것: `UREBulletRenderProcessor::Execute`를 채워 live 탄환의 `FTransformFragment`를 매 프레임 ISM(Instanced Static Mesh) 인스턴스로 그린다. 렌더 서브시스템이 홀더 액터+ISM(빨강 엔진 구체)을 소유하고, GameMode가 지속 발사 데모 루프를 돌려 탄막 영상 소스를 만든다. 데디서버는 렌더 skip(`ExecutionFlags=5`).

**스코프 밖(손대지 말 것):** 영속 InstanceIndex/Observer Processor, Niagara 렌더(M6), HISM/배치 최적화(M3), 커스텀 탄환 애셋, 영상 파일 녹화/편집(수동). 상세는 최하단 "하지 말 것".

설계 스펙: `docs/superpowers/specs/2026-07-09-bullet-ism-render-processor-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-09-bullet-ism-render-processor.md`
(참고 가능. 단 **아래 코드가 최종 정본** — spec/plan과 어긋나면 이 goal을 따른다.)

## 브랜치

`dev`에서 분기하지 않는다. **현재 브랜치 `feature/M1-bullet-archetype-spawn`에서 계속 작업**(#14/#15/#16 후속). PR base=`dev`.

## 전역 제약

- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `UInstancedStaticMeshComponent`는 Engine 모듈(기존 의존). MassGameplay 플러그인은 #15에서 이미 활성(Sim/Render Processor 구동용).
- 자동화 테스트 인프라 없음 → 게이트는 **빌드 성공**(`Result: Succeeded`) + **headless 프로브 로그 관측**(`-game -nullrhi`, `MSYS_NO_PATHCONV=1` 필수).
- 로그 접두어 `[RE]` 고정. 클래스/타입명 정확히: `UREBulletRenderSubsystem`, `UREBulletRenderProcessor`.
- 매틱 재구성 전략 확정 — 영속 InstanceIndex/Observer Processor는 스코프 밖.
- 영상 파일 녹화는 수동(PIE+카메라+화면녹화) — 코드 밖. 이 goal은 코드 + headless 카운트 검증까지.

## 검증된 API (실물 확인됨 — UE 5.8 엔진 헤더)

- `UWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)` — `Public/Subsystems/WorldSubsystem.h:43` (virtual).
- `UWorld::IsGameWorld()` `World.h:4172`, `UWorld::GetNetMode()`, `UWorld::SpawnActor<AActor>()`.
- `AActor::SetRootComponent`, `UActorComponent::RegisterComponent`.
- `UInstancedStaticMeshComponent` (`Components/InstancedStaticMeshComponent.h`):
  - `int32 AddInstance(const FTransform&, bool bWorldSpace=false)` :271
  - `bool RemoveInstance(int32 InstanceIndex)` :417
  - `bool UpdateInstanceTransform(int32, const FTransform&, bool bWorldSpace=false, bool bMarkRenderStateDirty=false, bool bTeleport=false)` :375
  - `int32 GetInstanceCount() const` :435
  - `SetStaticMesh(UStaticMesh*)`, `SetMaterial`, `GetStaticMesh()`
- `UPrimitiveComponent::CreateDynamicMaterialInstance(int32 ElementIndex, UMaterialInterface* SourceMaterial=NULL, FName=NAME_None)` `PrimitiveComponent.h:1629`.
- 애셋(둘 다 존재 확인): `/Engine/BasicShapes/Sphere.Sphere`, `/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial`. 머티리얼 벡터 파라미터명 `Color`.
- `FMassEntityManager::GetWorld()` `MassEntityManager.h:1353`.
- `FMassExecutionContext::GetFragmentView<T>()`(RO 뷰), `GetNumEntities()`.
- `FTransformFragment` (`Mass/EntityFragments.h`, MassCore — Build.cs 불요, #14/#15 확인). `FBulletTag` (`REBulletFragments.h`, #14).
- `FTimerManager::SetTimer(FTimerHandle&, FTimerDelegate, float Rate, bool bLoop)`, `FTimerDelegate::CreateLambda`. (직접 람다 오버로드 아님 — 델리게이트 경유.)

## 기존 파일 현황 (변경 대상)

- **`Source/Project_RE/Mass/REBulletRenderProcessor.h`** — 변경 없음. `UREBulletRenderProcessor : UMassProcessor`, `ExecutionFlags=Standalone|Client(5)`, 멤버 `FMassEntityQuery EntityQuery`. `ConfigureQueries`/`Execute` 오버라이드 선언 존재.
- **`Source/Project_RE/Mass/REBulletRenderProcessor.cpp`** — 스텁 상태. 생성자에서 `ExecutionFlags=5` 설정. `ConfigureQueries`가 현재 `FBulletSimFragment(RO)`+`FBulletRenderFragment(RW)` 요구. `Execute`는 `UE_LOG` 스텁. → TASK 2에서 전면 교체.
- **`Source/Project_RE/Mass/REBulletFragments.h`** — 변경 없음. `FBulletSimFragment{Velocity,Lifetime}`, `FBulletRenderFragment{InstanceIndex=INDEX_NONE}`, `FBulletTag`(빈 태그) 정의. `FBulletRenderFragment`는 이번에 **미사용**(존치, 삭제 금지).
- **`Source/Project_RE/Core/REGameMode.h`** — `AREGameMode : AGameModeBase`. `private:`에 `FMassEntityHandle ProbeBullet; float ProbeElapsed=0.f;` 존재. `AREBossCharacter` 전방선언 **없음**. `Engine/TimerHandle.h` include **없음**.
- **`Source/Project_RE/Core/REGameMode.cpp`** — `BeginPlay`에서 #14~#16 프로브(보스 스폰+Spiral 2회 트리거, SimProbe 탄환 1발, 제너레이터 단위 로그) 수행. 보스 스폰 블록: `if (AREBossCharacter* Boss = GetWorld()->SpawnActor<AREBossCharacter>(...))`. 이 프로브들은 **유지**(회귀 검증 수단).
- **`Source/Project_RE/Core/REBossCharacter.*`** — `TriggerBulletPattern(EBulletPattern, int32 Seed, float StartTime)` 존재(#16 배선 완료). 변경 없음.

================================================================
## TASK 1: `UREBulletRenderSubsystem` — 홀더 액터 + ISM 소유
================================================================

### 1-1. `Source/Project_RE/Mass/REBulletRenderSubsystem.h` (신규)

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

### 1-2. `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp` (신규)

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

### 1-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-4. 커밋

```bash
git add Source/Project_RE/Mass/REBulletRenderSubsystem.h Source/Project_RE/Mass/REBulletRenderSubsystem.cpp
git commit -m "feat(M1): add bullet render subsystem (ISM holder) (#17)"
```

================================================================
## TASK 2: `UREBulletRenderProcessor` Execute — 매틱 ISM 재구성
================================================================

`Source/Project_RE/Mass/REBulletRenderProcessor.cpp` 전면 교체. 아래가 교체 후 **파일 전문**(생성자 `ExecutionFlags=5`는 유지).

### 2-1. `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` (수정 — 전문)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderProcessor.h"
#include "REBulletFragments.h"
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
	/** 탄환 인스턴스 스케일 — 엔진 Sphere(반경 50cm)를 반경 ~10cm로 축소. */
	constexpr float BulletScale = 0.2f;
}

UREBulletRenderProcessor::UREBulletRenderProcessor()
	: EntityQuery(*this)
{
	// 5: 데디서버(Server) skip, 싱글/클라만 렌더
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);
}

void UREBulletRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);  // 위치 읽기
	EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);          // 탄환만 선별
}

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

### 2-2. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-3. 커밋

```bash
git add Source/Project_RE/Mass/REBulletRenderProcessor.cpp
git commit -m "feat(M1): fill render processor Execute (per-frame ISM rebuild) (#17)"
```

================================================================
## TASK 3: 데모 발사 루프 + headless 카운트 검증
================================================================

### 3-1. `Source/Project_RE/Core/REGameMode.h` (수정)

상단 include 블록 끝(`#include "REGameMode.generated.h"` **앞**)에 추가:
```cpp
#include "Engine/TimerHandle.h"
```

`FRETestFragment` 정의 아래(또는 클래스 선언 앞)에 전방선언 추가:
```cpp
class AREBossCharacter;
```

클래스 `private:` 블록의 기존 `ProbeBullet`/`ProbeElapsed` 아래에 추가:
```cpp
	/** 데모: 주기적 Spiral 발사로 지속 탄막(영상 소스). */
	FTimerHandle DemoFireTimer;

	UPROPERTY()
	TObjectPtr<AREBossCharacter> DemoBoss = nullptr;
```

교체 후 `private:` 블록은 아래 형태:
```cpp
private:
	// #15 프로브 전용: 이동/수명 관측용 테스트 탄환. #17 데모 씬에서 제거 예정.
	FMassEntityHandle ProbeBullet;
	float ProbeElapsed = 0.f;

	/** 데모: 주기적 Spiral 발사로 지속 탄막(영상 소스). */
	FTimerHandle DemoFireTimer;

	UPROPERTY()
	TObjectPtr<AREBossCharacter> DemoBoss = nullptr;
```

### 3-2. `Source/Project_RE/Core/REGameMode.cpp` (수정)

상단 include에 추가:
```cpp
#include "TimerManager.h"
```

기존 보스 스폰 블록(현재 `if (AREBossCharacter* Boss = ...)` — Spiral 2회 트리거만 있는 블록)을 아래로 교체(2회 프로브 트리거 **유지** + 저장/타이머 추가):
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

### 3-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 3-4. headless 런타임 프로브 (Acceptance)

PIE 없이 headless. Git Bash에서(`MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe17.log &
sleep 30
grep -E "\[RE\] (RenderSubsystem|RenderProbe)" "Saved/Logs/RE_probe17.log" | head -40
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대 로그:
```
[RE] RenderSubsystem: ISM ready (mesh=1)
[RE] RenderProbe: live=33 ISM.Count=33     ← 초기 프로브(32 스파이럴 + 1 심프로브)
[RE] RenderProbe: live=161 ISM.Count=161   ← 데모 타이머 누적, 두 수 항상 일치
...
[RE] RenderProbe: live=480 ISM.Count=480   ← 정상상태 근처 (10발/s × 16 × 3s Lifetime)
```
**합격 기준 2개:**
1. **ISM 준비:** `RenderSubsystem: ISM ready (mesh=1)` — 홀더/ISM/구체 메시 로드 성공.
2. **카운트 추종:** 모든 `RenderProbe` 줄에서 `live == ISM.Count`, 값이 0에서 증가(스폰 반영) 후 정상상태 유지(파괴 반영으로 폭주 안 함).

**nullrhi 리스크 대응(판정 절차):** `ISM.Count`가 항상 0이거나 `live`와 어긋나면 = nullrhi에서 ISM 인스턴스 데이터가 갱신 안 되는 것. 그 경우 **헤드리스는 `live`(순수 Mass 수집 수)만 실증**으로 강등하고(합격 기준 2를 "live가 스폰/파괴에 따라 증감"으로 축소), 실제 ISM 반영은 수동 PIE 검증으로 이관. 이 분기는 프로브 결과 보고 판정 — 코드 변경 아님.

### 3-5. 커밋

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "test(M1): add demo fire loop + ISM count probe (#17)"
```

## 완료 후

**수동 영상 검증(M1 마일스톤 완료 조건):** 에디터에서 `/Game/Level/Main` PIE 실행 → 원점 보스가 0.1초마다 회전 나선 발사, 빨강 구체 탄막이 바깥으로 이동. 탑다운 카메라(기존 topdown 셋업)로 육안 확인 후 화면 녹화 → **탄막 데모 영상** 산출. 물리적 수동 작업이라 코드 밖.

**PR 생성 규칙:**
- base=`dev`, head=`feature/M1-bullet-archetype-spawn`.
- 이슈 #17 메타 미러링: label / milestone(M1) / assignee / project 전부 이슈와 동일하게 설정.
- 본문 6개 필드 전부 채움. Reviewer 생략.
- 본문 작성 전 이슈 #17 원문과 대조.

**의도된 잔존 마커(후속 이슈 몫, 지금 지우지 말 것):** GameMode #15/#16 프로브 로그 + #17 `RenderProbe`(Execute 내 static 카운터)는 M1 회귀 검증 수단. M1 마감/데모 확정 후 별도 정리 커밋에서 제거.

## 하지 말 것 (스코프 밖)

- **영속 InstanceIndex / Observer Processor 파괴 훅** — 매틱 재구성 전략이라 불요. `FBulletRenderFragment.InstanceIndex`는 미사용이나 존치(삭제 금지, M5 시드 경로 여지).
- **ISM `AddInstances`(배치) 최적화 / HISM / Nanite / LOD / GPU 인스턴싱 튜닝** — M3 프로파일링 몫.
- **Niagara 렌더** — M6(ISM→Niagara 교체).
- **커스텀 탄환 메시/발광 머티리얼** — 엔진 구체+빨강으로 충분.
- **카메라/조명/포스트 셋업** — 기존 topdown 셋업 재사용, 신규 금지.
- **영상 파일 녹화/편집** — 수동 물리 단계, 코드 밖.
- **Build.cs / .uproject 변경** — 불요.
- **GameMode 프로브(#15/#16/#17) 제거** — M1 마감 전까지 존치.
