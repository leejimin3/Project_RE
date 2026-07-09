# M2 #24 우클릭 이동 실배선 (NavMesh + Server RPC) Design Spec

**이슈:** #24 [M2] 우클릭 이동 실배선 (NavMesh + Server RPC)
**마일스톤:** M2 (플레이어 게임루프)
**날짜:** 2026-07-10
**브랜치:** `feature/M2-rightclick-move` (예정, `dev`에서 분기)

## 목표

현재 뼈대 상태인 우클릭 point-and-click 이동을 **서버 권위 NavMesh 패스팔로잉**으로 실배선한다. 롤/이터널리턴/로스트아크가 쓰는 아키텍처와 동일: 클릭 검출은 클라, 목표 좌표만 서버로, 패스파인딩·이동 시뮬은 서버 권위, 클라는 복제 위치 보간. M4/M5 데디케이티드 전환 시 재작성 없이 켜지는 것이 설계 목표.

## 현재 상태 (`AREPlayerController`)

- `OnClickMove`: `GetHitResultUnderCursor`로 커서 아래 지점을 **로컬** `MoveTarget`에 저장.
- `PlayerTick`: `MoveTarget`으로 **직선** `AddMovementInput` (평면 전용, `bMoveToTarget`/`AcceptanceRadius`).
- `Server_RequestMove(FVector)`: **빈 뼈대**.
- `Main.umap`: 거의 빈 레벨 (19KB, 외부 액터 0). `NavMeshBoundsVolume` **없음**.
- `Build.cs`: `NavigationSystem`/`AIModule` 의존 **없음**.

## 설계 결정 (사용자 확정)

| 갈림길 | 결정 | 근거 |
|---|---|---|
| 패스팔로잉 구동 위치 | **서버 권위** (`SimpleMoveToLocation`을 서버 PC에서) | 롤/로아 방식. 안티치트·데디 대비. M2는 리슨서버 싱글 → 로컬=서버 → 지연 0. 클라 예측 없음(복제 보간만) — 탑뷰 클릭이동의 표준 손맛. |
| NavMesh 채택 | **채택** (직선 이동 폐기) | 현 `Main.umap`은 장애물 0이라 직선으로도 충분하나, 사용자가 NavMesh 경험/확장성 위해 채택 결정. 아레나에 엄폐물 추가 시 로직 변경 없이 우회. |
| NavMesh 볼륨 공급 | **에디터 배치** (`Main.umap`에 `NavMeshBoundsVolume` 1개) | UE 표준. 정적 nav(RuntimeGeneration=Static 기본) 베이크. 런타임 스폰(코드우선)은 버그 소지·비표준이라 기각. |
| PathFollowingComponent | **온디맨드 생성** (`SimpleMoveToLocation`이 PC에 자동 부착) | `UAIBlueprintHelperLibrary::SimpleMoveToLocation` → `InitNavigationControl`이 비-AIController에도 `UPathFollowingComponent` 생성·초기화. 별도 `AIController` 불필요. |
| 클릭 검증 | **서버 `ProjectPointToNavigation`** | 오프메시(맵 밖) 클릭 거부 = 안티치트. 스냅된 nav 지점으로 이동. |

## 스코프 결정 (YAGNI)

- **포함:** `Build.cs` 모듈 의존 / `Main.umap` 볼륨 배치 / `OnClickMove`→RPC 교체 / `Server_RequestMove` 실구현 (검증+`SimpleMoveToLocation`) / 직선 이동 상태·루프 제거.
- **제외:** 이동 애니메이션·회전 튜닝, 스페이스 대쉬(#25 별건), 동적 nav 생성(레벨 지오메트리 고정), 이동 취소/재클릭 입력, 이동 속도 어트리뷰트화.

이유: #24 완료기준은 "우클릭→폰이 목표로 이동, 위치 권위=서버". 그 이상은 다른 이슈 소관 (CLAUDE.md Simplicity First).

## 기존 코드와의 충돌 정리

- `OnClickMove`의 로컬 이동 세팅(`MoveTarget`/`bMoveToTarget`)은 **폐기**되고 RPC 호출로 교체 — 내가 바꾸는 코드의 정리이므로 Surgical Changes 위반 아님.
- `PlayerTick`의 직선 이동 루프는 `SimpleMoveToLocation`(PathFollowingComponent 자동 tick)이 대체 → 제거.
- 폐기되는 멤버: `MoveTarget`, `bMoveToTarget`, `AcceptanceRadius`. `PlayerTick` 오버라이드 자체가 비면 제거(Super만 남으면 오버라이드 삭제).
- `bShowMouseCursor`/IMC/IA(`ClickMoveAction`) 배선은 **변경 없음** — 클릭 검출은 여전히 로컬.
- 조종 폰 `ARECharacterBase`는 `ACharacter`(CharacterMovementComponent 보유) → `SimpleMoveToLocation` 대상으로 적합.

## 검증 가능한 완료 조건 (Acceptance)

1. `Project_REEditor` 빌드 성공, 에러 0.
2. `Main.umap`에 `NavMeshBoundsVolume` + `RecastNavMesh-Default` 존재, nav 베이크됨.
3. 헤드리스 리슨서버 프로브에서:
   - `Server_RequestMove` 서버 수신 로그 (`[Move] Server_RequestMove recv target=...`).
   - `SimpleMoveToLocation` 경로 성공 → 폰 위치가 목표로 **수렴**(시작 대비 목표까지 거리 감소) 로그.
   - 오프메시 좌표 입력 시 **거부** 로그 (`[Move] rejected: off-navmesh`).
4. 위치 갱신이 서버 권위 (클라 로컬 `AddMovementInput` 없음).

넷 실증(원격 클라 보간)은 M4/M5. 지금 게이트 = **빌드 + nav 베이크 + 서버 RPC 이동 수렴 로그**.

## 설계

### 1. `Project_RE.Build.cs` — 모듈 의존

`PublicDependencyModuleNames`에 2종 추가:

```csharp
"NavigationSystem",   // UNavigationSystemV1::ProjectPointToNavigation
"AIModule"            // UAIBlueprintHelperLibrary::SimpleMoveToLocation, UPathFollowingComponent
```

### 2. `Main.umap` — NavMesh 볼륨 (에디터 수작업)

- 에디터로 `Main.umap` 열기.
- `NavMeshBoundsVolume` 1개 배치 → 스케일을 플레이 가능 바닥 전체 커버로 확대.
- `P` 키로 nav(초록) 생성 확인 → 저장.
- Project Settings > Navigation Mesh: 기본값(정적) 유지. 별도 RecastNavMesh 튜닝 없음.

### 3. `REPlayerController.h` — 상태 정리

```cpp
// 제거: MoveTarget, bMoveToTarget, AcceptanceRadius
// 제거: PlayerTick 오버라이드 (직선 루프 소멸 → Super만 남으면 오버라이드 자체 삭제)
// 유지: OnClickMove, Server_RequestMove(Server, Reliable), ClickMoveAction, TopDownMappingContext
```

`Server_RequestMove` 선언은 유지 (`UFUNCTION(Server, Reliable)`).

### 4. `REPlayerController.cpp` — RPC 실배선

```cpp
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "NavigationSystem.h"

void AREPlayerController::OnClickMove(const FInputActionValue& Value)
{
    // 클릭 검출은 로컬 (커서/카메라는 로컬 전용). 해석된 월드 좌표만 서버로.
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
```

- `SimpleMoveToLocation(this, ...)`: `this`=서버 PlayerController. `InitNavigationControl`이 `UPathFollowingComponent`를 PC에 온디맨드 생성·구동 → CMC 이동 → 위치 복제.
- `PlayerTick`/`MoveTarget` 로직 소멸.

## 테스트 전략

자동화 테스트 인프라 없음. 게이트:

1. **에디터 빌드 성공** (`NavigationSystem`/`AIModule` 링크 확인).
2. **헤드리스 리슨서버 프로브** — 기존 M0/M1 패턴(`-game -nullrhi`, `MSYS_NO_PATHCONV=1`). Recast는 CPU 연산이라 `-nullrhi` 무관, 정적 nav는 맵 로드 시 적재됨. 프로브 시나리오:
   - BeginPlay 후 서버에서 임의 목표로 `Server_RequestMove` 직접 호출(또는 시뮬 클릭).
   - 로그: 수신 target, 경로 성공, N틱 후 폰 위치 → 목표 거리 감소(수렴).
   - 오프메시 좌표 1회 호출 → 거부 로그.
3. **(선택) 시각 검증** — 필요 시 `-windowed` 실RHI PIE에서 우클릭 → 폰 이동 육안 확인 (`ue_visual_verify_screenshot` 패턴). nav는 nullrhi로도 검증되므로 필수 아님.

> `-nullrhi`는 ISM Bounds=0 오진 함정이 있으나 이는 렌더 경로 한정. NavMesh는 CPU라 영향 없음 (memory: `ue_visual_verify_screenshot`).

## 파일 요약

- Modify: `Source/Project_RE/Project_RE.Build.cs` (`NavigationSystem`/`AIModule` 의존 추가)
- Modify: `Content/Level/Main.umap` (에디터 — `NavMeshBoundsVolume` 배치, nav 베이크)
- Modify: `Source/Project_RE/Core/REPlayerController.h` (직선 이동 상태·`PlayerTick` 제거, RPC 선언 유지)
- Modify: `Source/Project_RE/Core/REPlayerController.cpp` (`OnClickMove`→RPC, `Server_RequestMove` 실구현, `PlayerTick`/직선 루프 제거, nav/AI include)
