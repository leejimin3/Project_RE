# 우클릭 이동 서버권위 NavMesh 실배선 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 우클릭 point-and-click 이동을 서버 권위 NavMesh 패스팔로잉으로 실배선한다 (#24).

**Architecture:** 롤/로아 방식 — 클릭 검출은 클라(로컬 커서→월드 좌표), 목표 좌표만 `Server_RequestMove` RPC로 서버 전송. 서버가 `ProjectPointToNavigation`으로 검증 후 `UAIBlueprintHelperLibrary::SimpleMoveToLocation`으로 패스팔로잉 구동 → `CharacterMovementComponent`가 위치 복제. 클라는 복제 위치 보간(예측 없음). M2 싱글은 리슨서버라 로컬=서버 → 지연 0.

**Tech Stack:** UE 5.8 C++, `NavigationSystem`(ProjectPointToNavigation), `AIModule`(SimpleMoveToLocation/PathFollowingComponent), Enhanced Input, `NavMeshBoundsVolume`(정적 nav).

## Global Constraints

- 자동화 테스트 인프라 **없음** → 게이트는 **빌드 성공**(`Result: Succeeded`) + **헤드리스 프로브 로그 관측**(`-game -nullrhi`, `MSYS_NO_PATHCONV=1` 필수, Git Bash).
- UE 엔진 경로: `/e/UE_5.8/Engine/`. 프로젝트: `E:\UnrealProjects\Project_RE\Project_RE.uproject`. 테스트 레벨: `/Game/Level/Main`.
- 헤드리스 프로브 자기이동 훅은 `HasAuthority() && FApp::IsUnattended()`로만 발동 — 실플레이(에디터/PIE, `-unattended` 없음)엔 무발동, 게임플레이 오염 0. 프로브 코드 영구 잔존(M1 데모 패턴과 동일 철학).
- 커밋 co-author 트레일러: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Gitflow: 브랜치 `feature/M2-rightclick-move` (이미 `dev`에서 분기, 스펙 커밋 존재).

---

## File Structure

- `Source/Project_RE/Project_RE.Build.cs` — 모듈 의존에 `NavigationSystem` 추가 (`AIModule`은 기존 line 17).
- `Source/Project_RE/Core/REPlayerController.h` — 직선 이동 상태 제거, RPC 선언 유지, 헤드리스 프로브 멤버 추가.
- `Source/Project_RE/Core/REPlayerController.cpp` — `OnClickMove`→RPC, `Server_RequestMove` 실구현, `PlayerTick` 직선 루프 제거, 헤드리스 자기이동 프로브.
- `Content/Level/Main.umap` — `NavMeshBoundsVolume` 에디터 배치 (수작업, Task 3).

---

## Task 1: Build.cs — NavigationSystem 모듈 의존 추가

**Files:**
- Modify: `Source/Project_RE/Project_RE.Build.cs:11-27`

**Interfaces:**
- Consumes: 없음.
- Produces: `NavigationSystem` 모듈 링크 → Task 2에서 `UNavigationSystemV1`/`FNavLocation` 사용 가능.

- [ ] **Step 1: `NavigationSystem` 의존 추가**

`PublicDependencyModuleNames` 배열의 `"GameplayTasks"` 다음 줄에 추가 (`AIModule`은 이미 line 17에 존재하므로 추가 안 함):

```csharp
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"MassEntity",
			"MassCore",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks",
			"NavigationSystem"
		});
```

- [ ] **Step 2: 빌드로 모듈 링크 확인**

Run (Git Bash):
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0. (코드 미변경이라 링크만 확인 — `NavigationSystem` 해석 성공.)

- [ ] **Step 3: 커밋**

```bash
git add Source/Project_RE/Project_RE.Build.cs
git commit -m "build(M2): add NavigationSystem module dep (#24)

ProjectPointToNavigation/FNavLocation 사용 위해 NavigationSystem 추가.
AIModule(SimpleMoveToLocation)은 기존.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: REPlayerController — 서버권위 RPC 이동 + 헤드리스 프로브

직선 이동을 폐기하고 `Server_RequestMove` RPC로 서버권위 패스팔로잉을 배선한다. 헤드리스 검증용 자기이동 프로브를 `HasAuthority() && FApp::IsUnattended()` 게이트로 넣는다.

**Files:**
- Modify: `Source/Project_RE/Core/REPlayerController.h`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: Task 1의 `NavigationSystem` 모듈. 기존 `AREPlayerController::Server_RequestMove(FVector)` 선언(`UFUNCTION(Server, Reliable)`).
- Produces: 런타임 동작 — 클라 우클릭 → 서버 `SimpleMoveToLocation` 이동. 헤드리스 로그 태그 `[Move]` (Task 4 프로브가 grep).

- [ ] **Step 1: `.h` — 직선 이동 상태 제거 + 프로브 멤버 추가**

`REPlayerController.h`를 아래로 교체. 변경점: `PlayerTick` 오버라이드 제거, `MoveTarget`/`bMoveToTarget`/`AcceptanceRadius` 제거, 헤드리스 프로브용 `RunHeadlessMoveProbe` 선언 + `ProbeMoveTimer`/`ProbeLogTimer`/`ProbeTarget` 멤버 추가. `OnClickMove`/`Server_RequestMove`/`ClickMoveAction`/`TopDownMappingContext`는 유지.

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "REPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 *  탑뷰 PlayerController. 우클릭으로 커서 아래 지점으로 폰을 이동시킨다.
 *  클릭 검출은 로컬, 이동 권위는 서버(Server_RequestMove RPC → NavMesh 패스팔로잉).
 *  IA/IMC는 uasset 없이 코드로 생성(transient).
 */
UCLASS()
class AREPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** 우클릭 핸들러: 커서 아래 지점을 서버로 이동 요청 */
	void OnClickMove(const FInputActionValue& Value);

	/** 이동 요청 서버 RPC. 서버가 nav 검증 후 SimpleMoveToLocation 구동. */
	UFUNCTION(Server, Reliable)
	void Server_RequestMove(FVector Target);

	UPROPERTY()
	UInputAction* ClickMoveAction;

	UPROPERTY()
	UInputMappingContext* TopDownMappingContext;

private:
	/** 헤드리스(-unattended) 자기이동 프로브. 서버 권위에서만 발동. */
	void RunHeadlessMoveProbe();

	FTimerHandle ProbeMoveTimer;
	FTimerHandle ProbeLogTimer;
	FVector ProbeTarget = FVector::ZeroVector;
};
```

- [ ] **Step 2: `.cpp` — include + OnClickMove RPC 교체 + Server_RequestMove 실구현 + PlayerTick 제거**

`REPlayerController.cpp`를 아래로 교체. `PlayerTick` 함수와 `GameFramework/Pawn.h`(직선 이동에만 쓰였음) 제거, nav/AI include 추가, `OnClickMove`를 RPC 호출로, `Server_RequestMove_Implementation` 실구현. `BeginPlay` 끝에 헤드리스 프로브 훅 추가.

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REPlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputActionValue.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Pawn.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "NavigationSystem.h"
#include "Misc/App.h"
#include "TimerManager.h"

void AREPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 입력은 로컬 컨트롤러에만 배선 (M4 데디 대비)
	if (!IsLocalPlayerController())
	{
		return;
	}

	// uasset 없이 코드로 IA/IMC 생성 (transient — 매 실행 생성)
	ClickMoveAction = NewObject<UInputAction>(this, TEXT("IA_ClickMove"));
	ClickMoveAction->ValueType = EInputActionValueType::Boolean;

	TopDownMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_TopDown"));
	TopDownMappingContext->MapKey(ClickMoveAction, EKeys::RightMouseButton);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(ClickMoveAction, ETriggerEvent::Triggered, this, &AREPlayerController::OnClickMove);
	}
}

void AREPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalPlayerController())
	{
		// 탑뷰 클릭 이동 — 마우스 커서 표시
		bShowMouseCursor = true;
		DefaultMouseCursor = EMouseCursor::Default;

		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			if (TopDownMappingContext)
			{
				Subsystem->AddMappingContext(TopDownMappingContext, 0);
			}
		}
	}

	// 헤드리스(-unattended) 서버권위 이동 프로브. 실플레이(PIE/에디터)엔 무발동.
	if (HasAuthority() && FApp::IsUnattended())
	{
		RunHeadlessMoveProbe();
	}
}

void AREPlayerController::OnClickMove(const FInputActionValue& Value)
{
	// 클릭 검출은 로컬(커서/카메라는 로컬 전용). 해석된 월드 좌표만 서버로.
	FHitResult Hit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, Hit) && Hit.bBlockingHit)
	{
		Server_RequestMove(Hit.ImpactPoint);
	}
}

void AREPlayerController::Server_RequestMove_Implementation(FVector Target)
{
	// 서버 권위 — nav 검증 후 패스팔로잉 구동.
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	FNavLocation NavLoc;
	if (!NavSys || !NavSys->ProjectPointToNavigation(Target, NavLoc))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Move] rejected: off-navmesh %s"), *Target.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Move] Server_RequestMove recv target=%s"), *NavLoc.Location.ToString());
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, NavLoc.Location);
}

void AREPlayerController::RunHeadlessMoveProbe()
{
	// 폰 possess 완료(BeginPlay 직후 possess 타이밍 여유) 후 1.0s에 자기이동 1회 + 오프메시 거부 1회.
	FTimerDelegate MoveDel = FTimerDelegate::CreateLambda([this]()
	{
		APawn* P = GetPawn();
		if (!P)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Move] probe: no pawn"));
			return;
		}
		// 시작점에서 +X 500 만큼 떨어진 목표(nav 위 예상).
		ProbeTarget = P->GetActorLocation() + FVector(500.f, 0.f, 0.f);
		UE_LOG(LogTemp, Log, TEXT("[Move] probe start: pawn=%s target=%s"),
			*P->GetActorLocation().ToString(), *ProbeTarget.ToString());

		// 정상 이동 요청.
		Server_RequestMove(ProbeTarget);

		// 오프메시 거부 검증: 맵 밖 좌표 1회.
		Server_RequestMove(FVector(100000.f, 100000.f, 0.f));

		// 0.5s마다 목표까지 거리 로그(수렴 관측), 5s간.
		FTimerDelegate LogDel = FTimerDelegate::CreateLambda([this]()
		{
			if (APawn* Pn = GetPawn())
			{
				const float Dist = FVector::Dist2D(Pn->GetActorLocation(), ProbeTarget);
				UE_LOG(LogTemp, Log, TEXT("[Move] probe dist=%.1f loc=%s"),
					Dist, *Pn->GetActorLocation().ToString());
			}
		});
		GetWorld()->GetTimerManager().SetTimer(ProbeLogTimer, LogDel, 0.5f, /*bLoop=*/true);
	});
	GetWorld()->GetTimerManager().SetTimer(ProbeMoveTimer, MoveDel, 1.0f, /*bLoop=*/false);
}
```

- [ ] **Step 3: 빌드 게이트**

Run (Git Bash):
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0. (`PlayerTick` 제거·RPC·프로브 컴파일 성공. `SimpleMoveToLocation`/`ProjectPointToNavigation` 심볼 해석.)

- [ ] **Step 4: 커밋**

```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M2): server-authority NavMesh 우클릭 이동 실배선 (#24)

OnClickMove→Server_RequestMove RPC. 서버가 ProjectPointToNavigation 검증 후
SimpleMoveToLocation 구동(PathFollowingComponent 온디맨드). 직선 이동
상태·PlayerTick 제거. 헤드리스(-unattended) 서버권위 자기이동 프로브 추가.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Main.umap — NavMeshBoundsVolume 배치 (에디터 수작업)

**이 태스크는 사용자가 UE 에디터에서 수행한다** (`NavMeshBoundsVolume`는 브러시 볼륨이라 코드로 안심음 — 스펙 결정). 배치 전엔 Task 4 프로브가 "off-navmesh"로 전부 거부되므로 순서 필수.

**Files:**
- Modify: `Content/Level/Main.umap` (에디터 저장)

**Interfaces:**
- Consumes: 없음.
- Produces: `Main.umap`에 nav 데이터 → Task 4의 `ProjectPointToNavigation`/`SimpleMoveToLocation`이 경로 반환.

- [ ] **Step 1: 에디터에서 볼륨 배치**

1. UE 에디터로 `Content/Level/Main` 열기.
2. Place Actors > Volumes > **Nav Mesh Bounds Volume** 을 레벨에 드래그.
3. Details에서 Location=바닥 중앙, Scale을 플레이 가능 바닥 전체 덮도록 확대 (예: 바닥이 4000×4000이면 볼륨이 그 이상 커버). 높이(Z)는 폰 캡슐이 들어갈 만큼(예: 200~500).
4. 뷰포트에서 **`P` 키** → nav 영역이 **초록**으로 표시되는지 확인.
5. (초록 안 뜨면) 메뉴 Build > **Build Paths** 실행.

- [ ] **Step 2: 저장 + nav 베이크 확인**

1. `Ctrl+S`로 `Main.umap` 저장 (nav BuildData 직렬화).
2. World Outliner에 `RecastNavMesh-Default` 액터가 자동 생성됐는지 확인.

Expected: 뷰포트 `P`에서 바닥 위 초록 nav 표시. Outliner에 `NavMeshBoundsVolume` + `RecastNavMesh-Default` 존재.

- [ ] **Step 3: 커밋**

```bash
git add Content/Level/Main.umap
git commit -m "content(M2): Main.umap에 NavMeshBoundsVolume 배치 + nav 베이크 (#24)

바닥 전체 커버 정적 navmesh. 서버권위 SimpleMoveToLocation 경로 소스.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

> `Main.umap` 외에 nav BuildData가 별도 파일(`Content/Level/Main_BuiltData.uasset` 등)로 생성될 수 있음 — `git status`로 확인해 함께 add.

---

## Task 4: 헤드리스 프로브 — 서버권위 이동 실증 (Acceptance)

Task 1~3 완료 후 헤드리스 실행으로 완료조건 검증.

**Files:**
- 없음 (실행·관측만).

**Interfaces:**
- Consumes: Task 2의 `[Move]` 로그, Task 3의 nav 데이터.
- Produces: 없음 (게이트 판정).

- [ ] **Step 1: 헤드리스 리슨서버 프로브 실행**

Run (Git Bash, `MSYS_NO_PATHCONV=1` 필수):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_move_probe.log &
sleep 20
grep -E "\[Move\]" "Saved/Logs/RE_move_probe.log" | head -40
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

- [ ] **Step 2: 로그로 완료조건 판정**

기대 로그(순서·값은 근사):
```
[Move] probe start: pawn=X=... target=X=...
[Move] Server_RequestMove recv target=X=...           ← 정상 목표 nav 스냅 수신
[Move] rejected: off-navmesh X=100000.0 Y=100000.0 ... ← 맵 밖 거부
[Move] probe dist=500.0 loc=...                         ← 초기 거리
[Move] probe dist=320.4 loc=...                         ← 감소(이동 중)
[Move] probe dist=90.1 loc=...                          ← 계속 감소 → 수렴
```

**합격 기준 3개:**
1. **RPC 수신:** `Server_RequestMove recv target=...` 로그 1회 이상 (정상 목표).
2. **오프메시 거부:** `rejected: off-navmesh` 로그 1회 (맵 밖 좌표).
3. **수렴:** `probe dist=` 값이 시간 경과에 따라 **감소** (폰이 목표로 이동 = 서버 패스팔로잉 작동).

**리스크 대응(판정 절차, 코드 변경 아님):**
- `recv`는 뜨는데 `dist` 감소가 없으면 = nav는 있으나 패스팔로잉 미구동. `Build Paths` 재실행/볼륨 커버 재확인(Task 3). 그래도 안 되면 헤드리스 possess/`-nullrhi` 타이밍 이슈 가능 → `-windowed` 실RHI PIE 육안 이동 확인으로 이관(`ue_visual_verify_screenshot` 패턴).
- `probe: no pawn` 로그면 = 1.0s 시점 possess 미완 → 프로브 타이머 지연을 2.0s로 상향(Task 2 `SetTimer(..., 1.0f, ...)` → `2.0f`) 후 재실행.

- [ ] **Step 3: 완료 — PR 준비**

프로브 3기준 통과 시 #24 완료. 브랜치 `feature/M2-rightclick-move` → `dev` PR 생성 (PR 규칙: 이슈 메타 미러링, base=dev — memory `pr_creation_convention`).

---

## Self-Review

**Spec coverage:**
- 서버권위 아키텍처 → Task 2 (OnClickMove RPC + Server_RequestMove). ✅
- `NavigationSystem` 모듈 → Task 1. ✅ (`AIModule` 기존 확인)
- NavMeshBoundsVolume 에디터 배치 → Task 3. ✅
- `ProjectPointToNavigation` 검증(오프메시 거부) → Task 2 구현 + Task 4 기준 2. ✅
- `SimpleMoveToLocation` 서버 구동 → Task 2. ✅
- 직선 이동 상태·PlayerTick 제거 → Task 2 Step 1~2. ✅
- 헤드리스 프로브 검증 → Task 4. ✅

**Placeholder scan:** 코드/커맨드 전부 실체. TODO/TBD 없음. ✅

**Type consistency:** `Server_RequestMove(FVector)` 선언(.h)↔구현(`_Implementation`)↔호출(OnClickMove/프로브) 일치. `ProbeTarget`/`ProbeMoveTimer`/`ProbeLogTimer` 멤버 선언(.h)↔사용(.cpp) 일치. 로그 태그 `[Move]`가 Task 4 grep 패턴과 일치. ✅
