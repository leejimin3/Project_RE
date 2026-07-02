# 설계 문서 — M0 #1: 탑뷰 카메라 + PlayerController 뼈대

- **날짜:** 2026-07-02
- **마일스톤:** M0 (셋업 + 아키텍처 결정)
- **이슈:** #1 [M0] 탑뷰 카메라 + PlayerController 뼈대
- **브랜치:** `feature/M0-topdown-camera`

## 배경 / 문제

Project_RE는 UE5 MassEntity 기반 탑뷰 보스 탄막 게임(면접 포트폴리오). 최종 목표는 데디케이티드 서버 협동 멀티(M4~M5)이며, M0의 역할은 데디 전환을 "재작성"이 아닌 "켜기"로 만드는 아키텍처 토대를 까는 것.

현재 `Source/`에는 UE ThirdPerson + Combat 템플릿 기본 코드만 존재한다(모두 언리얼 제공 예시). 게임에 맞는 시점(탑뷰 쿼터뷰)과 입력 뼈대가 없다.

이슈 #1은 이 첫 단추 — LoL/이터널리턴식 탑뷰로 시점을 바꾸고, 우클릭 이동 입력을 받을 PlayerController 뼈대를 세운다. 실제 이동 로직은 M2 범위.

## 목표 / 완료 기준

- PIE 실행 시 **탑뷰 쿼터뷰 카메라로 캐릭터가 화면에 보인다.**
- 우클릭 입력이 코드까지 도달한다(핸들러 로그로 확인). 실제 이동은 M2.

## 핵심 결정

1. **신규 `ARE*` 베이스 클래스 생성.** 기존 `AProject_RE*`/`Variant_*`는 언리얼 예시이므로 참고만 하고 방치. 재사용/리네임하지 않는다.
2. **관리 용이성 최우선.** 모든 설정을 C++/텍스트로 유지해 Claude Code가 편집·git diff·버전관리 가능하게 한다. 불투명한 uasset(Blueprint, Input 데이터에셋)은 최소화한다.
3. **카메라는 캐릭터에 부착.** SpringArm + Camera를 폰에 달고 절대회전으로 고정. 별도 카메라 액터/PlayerCameraManager 커스텀은 M0 과설계로 배제(필요 시 M2+에서 승격).
4. **폰 비주얼은 C++ 순수.** BP 자식 폰을 만들지 않고, 생성자에서 `ConstructorHelpers`로 마네킹 에셋을 로드한다. 하드코딩 경로 1~2줄이 유일한 비용이며, 그 대가로 전부 코드로 관리된다.
5. **입력 에셋도 코드 생성.** `UInputAction`/`UInputMappingContext`를 uasset이 아닌 `NewObject`로 런타임 생성(transient). 리매핑 UI가 필요해지면 그때 uasset으로 승격.
6. **레벨은 기존 `/Game/Level/Main` 사용.** 사용자가 생성·저장 완료.

## 아키텍처

신규 C++ 3종, 위치 `Source/Project_RE/Core/`. 각 클래스는 단일 책임을 가지며 독립적으로 이해·검증 가능.

### `ARECharacterBase : ACharacter`
플레이어 폰. 시점과 비주얼을 소유.

- 컴포넌트: `USpringArmComponent`(CameraBoom) + `UCameraComponent`(TopDownCamera).
- 생성자에서 마네킹 로드(ConstructorHelpers):
  - Mesh: `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`
  - AnimBP: `/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed`
- 카메라 고정(절대 하향):
  - SpringArm: `bUsePawnControlRotation=false`, `bInheritPitch/Yaw/Roll=false`, `SetRelativeRotation(Pitch=-50°)`, `TargetArmLength=1500`.
  - Camera: `FOV=90`, Perspective, `bUsePawnControlRotation=false`.
  - 폰: `bUseControllerRotationYaw/Pitch/Roll=false`.
- HP/복제는 이 이슈 범위 밖(#3에서 추가).

### `AREPlayerController : APlayerController`
입력 뼈대. 우클릭을 받을 준비만.

- `SetupInputComponent()`: `NewObject<UInputAction>`(ValueType=Boolean) + `NewObject<UInputMappingContext>` 생성 → `IMC->MapKey(ClickMoveAction, EKeys::RightMouseButton)` → `UEnhancedInputComponent::BindAction(ClickMoveAction, ETriggerEvent::Triggered, this, &OnClickMove)`. (`SetupInputComponent()`가 `BeginPlay()`보다 먼저 호출되므로 IA/IMC 생성·바인딩은 여기서 수행.)
- `BeginPlay()`: `EnhancedInputLocalPlayerSubsystem::AddMappingContext`로 IMC 등록.
- `OnClickMove()`: `UE_LOG`만 출력. 주석 `// TODO M2: 커서 히트 → Server RPC 이동 요청`.
- 입력 처리는 로컬 컨트롤러에만 적용 — `SetupInputComponent()`/`BeginPlay()` 모두 `IsLocalPlayerController()` 가드 안에서 처리(M4 데디 대비).

### `AREGameMode : AGameModeBase`
- 생성자: `DefaultPawnClass = ARECharacterBase::StaticClass()`, `PlayerControllerClass = AREPlayerController::StaticClass()`.

### 데이터 흐름
```
PIE 시작 → AREGameMode가 ARECharacterBase 스폰 + AREPlayerController 소유
         → Character의 SpringArm/Camera가 탑뷰 뷰 제공 (뷰타겟)
우클릭 → EnhancedInput(IMC/IA, 코드생성) → AREPlayerController::OnClickMove() → 로그
```

## 파일 변경

- 신규: `Source/Project_RE/Core/RECharacterBase.{h,cpp}`
- 신규: `Source/Project_RE/Core/REPlayerController.{h,cpp}`
- 신규: `Source/Project_RE/Core/REGameMode.{h,cpp}`
- 편집: `Config/DefaultEngine.ini`
  - `GameDefaultMap` / `EditorStartupMap` = `/Game/Level/Main`
  - `GlobalDefaultGameMode` = `/Script/Project_RE.REGameMode`
- 편집: `Source/Project_RE/Project_RE.Build.cs` — `PublicIncludePaths`에 `Project_RE/Core` 추가. (`EnhancedInput` 모듈은 이미 의존에 존재 — 확인만.)

## 참고 (기존 패턴 재사용)

- `Source/Project_RE/Project_RECharacter.{h,cpp}` — SpringArm+Camera 컴포넌트 구성 방식.
- `Source/Project_RE/Project_REPlayerController.cpp` — `SetupInputComponent()`/`BeginPlay()`의 EnhancedInput 서브시스템 배선 흐름.

## 범위 밖 (다음 이슈)

- HP·복제·`HasAuthority()` 가드·`Server_` RPC → #3.
- 실제 point-and-click 이동, 대쉬, 자동사격 → M2.
- MassEntity 플러그인/Processor → #2, #4.

## 오류 처리 / 엣지

- ConstructorHelpers 로드 실패 시: `Succeeded()` 체크 후 실패해도 크래시 없이 진행(메시 없이 캡슐만 보임). 로그로 경고.
- 서버/데디 넷모드에서 카메라·입력은 로컬 컨트롤러에만 적용(`IsLocalPlayerController()` 가드) — M4 데디 전환 대비.

## 검증

1. `Project_RE` C++ 빌드 성공(에러 0).
2. PIE 실행 → 탑뷰(약 −50° 하향)로 마네킹 캐릭터가 화면에 보임.
3. 우클릭 → Output Log에 `OnClickMove` 로그 출력.
4. 이슈 #1 완료기준("PIE 실행 시 탑뷰 카메라로 캐릭터가 보임") 충족 확인.
