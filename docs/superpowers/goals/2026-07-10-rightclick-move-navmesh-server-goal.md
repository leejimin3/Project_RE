# 구현 목표: M2 #24 — 우클릭 이동 서버권위 NavMesh 실배선

## 컨텍스트

UE 5.8 C++ 탄막 슈팅 프로젝트. 탑뷰 쿼터뷰. 이슈 #24 (마일스톤 M2: 플레이어 게임루프).
이 goal이 하는 것: 뼈대만 있던 우클릭 point-and-click 이동을 **서버 권위 NavMesh 패스팔로잉**으로 실배선. 롤/로아 방식 — 클릭 검출은 클라(로컬 커서→월드 좌표), 목표 좌표만 `Server_RequestMove` RPC로 서버 전송, 서버가 `ProjectPointToNavigation` 검증 후 `SimpleMoveToLocation`으로 패스팔로잉 구동, CMC가 위치 복제. 클라는 복제 위치 보간(예측 없음). M2 싱글은 리슨서버라 로컬=서버 → 지연 0.

스코프 밖 후속 이슈(손대지 말 것): 스페이스 대쉬(#25), 자동사격(#26), 보스 탄막 피격(#27), 보스 HP/TakeDamage(#28), HP바 위젯(#29).

설계 스펙: `docs/superpowers/specs/2026-07-10-rightclick-move-navmesh-server-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-10-rightclick-move-navmesh-server.md`
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치

`dev`에서 분기: `feature/M2-rightclick-move` (이미 존재 — 스펙/플랜 커밋 있음. 이어서 작업).

## 전역 제약

- 자동화 테스트 인프라 **없음** → 게이트는 **빌드 성공**(`Result: Succeeded`) + **헤드리스 프로브 로그 관측**(`-game -nullrhi`, `MSYS_NO_PATHCONV=1` 필수, Git Bash).
- UE 엔진 경로: `/e/UE_5.8/Engine/`. 프로젝트: `E:\UnrealProjects\Project_RE\Project_RE.uproject`. 테스트 레벨: `/Game/Level/Main`.
- 빌드 커맨드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- 헤드리스 자기이동 프로브는 `HasAuthority() && FApp::IsUnattended()`로만 발동 — 실플레이(에디터/PIE)엔 무발동. 프로브 코드 영구 잔존(M1 데모 패턴).
- 커밋 co-author 트레일러: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Gitflow: feature → dev PR. main 직접 커밋 금지.
- **주의:** 워킹트리에 `Project_RE.uproject`의 `ModelContextProtocol` 플러그인 활성 변경이 있을 수 있음 — **#24와 무관**(MCP 셋업). #24 커밋에 섞지 말 것.

## 검증된 API (실물 확인됨)

- `UAIBlueprintHelperLibrary::SimpleMoveToLocation(AController* Controller, const FVector& Dest)` — 헤더 `"Blueprint/AIBlueprintHelperLibrary.h"`, 모듈 `AIModule`(Build.cs에 **기존**). 비-AIController에도 `UPathFollowingComponent`를 온디맨드 생성·구동.
- `UNavigationSystemV1::GetCurrent(UWorld*)` → `UNavigationSystemV1*`. `ProjectPointToNavigation(const FVector& Point, FNavLocation& OutLocation)` → `bool`. 헤더 `"NavigationSystem.h"`, 모듈 `NavigationSystem`(Build.cs에 **추가 필요**).
- `FApp::IsUnattended()` → `bool` (`-unattended` 커맨드라인 시 true). 헤더 `"Misc/App.h"`.
- `APlayerController::GetHitResultUnderCursor(ECollisionChannel, bool, FHitResult&)` → `bool`. `GetPawn()`, `HasAuthority()`, `IsLocalPlayerController()` 기존.
- 폰 배선: `AREGameMode` 생성자가 `DefaultPawnClass = ARECharacterBase::StaticClass()`, `PlayerControllerClass = AREPlayerController::StaticClass()` → 헤드리스 `-game`에서 폰 스폰+possess 됨.

## 기존 파일 현황 (변경 대상)

- `Source/Project_RE/Project_RE.Build.cs` — `PublicDependencyModuleNames`에 `AIModule` 이미 있음(line 17). `GameplayTasks`가 배열 마지막. `NavigationSystem` **없음**.
- `Source/Project_RE/Core/REPlayerController.h` — 현재 `PlayerTick` 오버라이드 + `MoveTarget`/`bMoveToTarget`/`AcceptanceRadius` 멤버 보유(직선 이동용). `OnClickMove`/`Server_RequestMove(FVector)`(UFUNCTION Server,Reliable)/`ClickMoveAction`/`TopDownMappingContext` 보유.
- `Source/Project_RE/Core/REPlayerController.cpp` — `OnClickMove`가 로컬 `MoveTarget` 세팅. `PlayerTick`가 직선 `AddMovementInput`. `Server_RequestMove_Implementation` 빈 뼈대.
- `Content/Level/Main.umap` — `NavMeshBoundsVolume` + `RecastNavMesh` 배치 **완료됨**(사용자 수작업, umap 32KB). Task 3는 검증만.

================================================================
## TASK 1: Build.cs — NavigationSystem 모듈 의존
================================================================

### 1-1. `Source/Project_RE/Project_RE.Build.cs` (수정)

`PublicDependencyModuleNames` 배열을 아래로 교체 (마지막에 `"NavigationSystem"` 추가, `AIModule`은 기존 유지):

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

### 1-2. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-3. 커밋

```bash
git add Source/Project_RE/Project_RE.Build.cs
git commit -m "build(M2): add NavigationSystem module dep (#24)

ProjectPointToNavigation/FNavLocation 사용 위해 NavigationSystem 추가.
AIModule(SimpleMoveToLocation)은 기존.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

================================================================
## TASK 2: REPlayerController — 서버권위 RPC 이동 + 헤드리스 프로브
================================================================

### 2-1. `Source/Project_RE/Core/REPlayerController.h` (수정)

파일 전문을 아래로 교체:

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

### 2-2. `Source/Project_RE/Core/REPlayerController.cpp` (수정)

파일 전문을 아래로 교체 (`PlayerTick` 제거, nav/AI/App include 추가, `OnClickMove`→RPC, `Server_RequestMove` 실구현, `BeginPlay` 프로브 훅):

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

### 2-3. 빌드 게이트

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-4. 커밋

```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M2): server-authority NavMesh 우클릭 이동 실배선 (#24)

OnClickMove→Server_RequestMove RPC. 서버가 ProjectPointToNavigation 검증 후
SimpleMoveToLocation 구동(PathFollowingComponent 온디맨드). 직선 이동
상태·PlayerTick 제거. 헤드리스(-unattended) 서버권위 자기이동 프로브 추가.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

================================================================
## TASK 3: Main.umap — NavMeshBoundsVolume (배치 완료됨, 검증만)
================================================================

**이미 사용자가 에디터에서 `NavMeshBoundsVolume` 배치 + 저장 완료** (umap 32KB, `NavMeshBoundsVolume`+`RecastNavMesh` 액터 존재). 신규 세션이면 아래 절차로 배치. 이미 있으면 3-1 검증만.

### 3-1. 배치 확인 (신규 세션이면 배치)

이미 배치된 경우 검증:
```bash
grep -a -o -E "NavMeshBoundsVolume|RecastNavMesh" Content/Level/Main.umap | sort | uniq -c
```
기대: `NavMeshBoundsVolume`·`RecastNavMesh` 둘 다 카운트 ≥1.

미배치(신규 세션)면 에디터로 `/Game/Level/Main` 열기 → Place Actors > Volumes > **Nav Mesh Bounds Volume** 드래그 → Scale을 플레이 가능 바닥 전체 커버로 확대 → 뷰포트 `P`로 초록 nav 확인 → (안 뜨면) Build > Build Paths → `Ctrl+S` 저장.

### 3-2. 커밋 (umap 변경분만, uproject 제외)

```bash
git add Content/Level/Main.umap
# nav BuiltData가 별도 파일로 생성됐으면 함께 add (git status로 확인):
# git add Content/Level/Main_BuiltData.uasset
git commit -m "content(M2): Main.umap에 NavMeshBoundsVolume 배치 + nav 베이크 (#24)

바닥 전체 커버 정적 navmesh. 서버권위 SimpleMoveToLocation 경로 소스.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
**주의:** `Project_RE.uproject`(ModelContextProtocol)는 이 커밋에 넣지 말 것 — #24 무관.

================================================================
## TASK 4: 헤드리스 프로브 — 서버권위 이동 실증 (Acceptance)
================================================================

### 4-1. 헤드리스 리슨서버 프로브 실행

```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_move_probe.log &
sleep 20
grep -E "\[Move\]" "Saved/Logs/RE_move_probe.log" | head -40
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```

### 4-2. 로그로 완료조건 판정

기대 로그(순서·값 근사):
```
[Move] probe start: pawn=X=... target=X=...
[Move] Server_RequestMove recv target=X=...
[Move] rejected: off-navmesh X=100000.0 Y=100000.0 ...
[Move] probe dist=500.0 loc=...
[Move] probe dist=320.4 loc=...
[Move] probe dist=90.1 loc=...
```

**합격 기준 3개:**
1. **RPC 수신:** `Server_RequestMove recv target=...` 로그 ≥1회 (정상 목표).
2. **오프메시 거부:** `rejected: off-navmesh` 로그 1회 (맵 밖 좌표).
3. **수렴:** `probe dist=` 값이 시간 경과에 따라 **감소** (서버 패스팔로잉 작동).

**리스크 대응(판정 절차, 코드 변경 아님):**
- `recv`는 뜨는데 `dist` 감소 없음 = nav는 있으나 패스팔로잉 미구동 → 에디터에서 Build Paths 재실행/볼륨 커버 재확인. 그래도 안 되면 `-windowed` 실RHI PIE 육안 이동 확인으로 이관.
- `probe: no pawn` 로그 = 1.0s 시점 possess 미완 → 2-2의 `SetTimer(ProbeMoveTimer, MoveDel, 1.0f, ...)`를 `2.0f`로 상향 후 재빌드·재실행.

## 완료 후

- 프로브 3기준 통과 시 #24 완료. 브랜치 `feature/M2-rightclick-move` → **base=dev** PR 생성.
- PR 규칙(memory `pr_creation_convention`): 이슈 #24 메타 미러링(label: networking/C++, milestone: M2, assignee, project), 6개 필드 전부 채움, Reviewer 생략.
- 남은 의도된 TODO: 없음 (#24 완결).

## 하지 말 것 (스코프 밖)

- 스페이스 대쉬 어빌리티 — #25.
- 자동사격 라인트레이스 — #26.
- 보스 탄막 피격 판정 — #27.
- 보스 HP/서버권위 TakeDamage — #28.
- HP바 월드스페이스 위젯 — #29.
- 이동 애니메이션·회전 튜닝, 동적 nav 생성, 이동 취소/재클릭 입력, 이동 속도 어트리뷰트화 — 미요청.
- `Project_RE.uproject`의 ModelContextProtocol 변경 커밋 — #24 무관, 손대지 말 것.
