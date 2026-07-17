# 구현 목표: M3.5 ② — 플레이어/보스 애니메이션 (보스 가시화 + 발사/대쉬 모션)

## 컨텍스트
UE 5.8 C++ 탑뷰 탄막(bullet-hell) 프로젝트 `Project_RE`. M4 데디 서버 전 중간점검 3건 중 두 번째.
이 goal: 보스 가시화(Quinn 메시+ABP_Unarmed — 현재 보스는 메시 없어 안 보임) + 플레이어 발사 몽타주 + 대쉬 모션. 전부 코스메틱 — 게임 로직/판정/복제 무변경.
**선행: M3.5 ①(좌클릭 공격)이 dev에 머지되어 있어야 한다** — `UREAttackComponent::FireInDirection` 훅 사용.
스코프 밖 후속 이슈(손대지 말 것): M3.5 ③ 스탯 DeveloperSettings 이관, 사망/피격 모션, M4 Multicast.
설계 스펙: docs/superpowers/specs/2026-07-17-character-animations-design.md
상세 플랜: docs/superpowers/plans/2026-07-17-character-animations.md
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치
`dev`에서 분기 (① 머지 후): `feature/M3.5-animations`

## 전역 제약
- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 검증 게이트 = **빌드 성공(에러 0)** + **headless 프로브 로그(몽타주 길이 >0)** + **실RHI 스크린샷(보스 렌더)**. `-nullrhi`로는 렌더 검증 불가 — 스크린샷은 실RHI `-windowed`로만 (메시 가시성 오진 함정).
- 범위: 발사 + 대쉬 + 이동(보스 가시화)만. 사망/피격 리액트 스코프 밖 (스펙 확정).
- 애셋 (모두 기존, 신규 uasset 없음): `SKM_Quinn_Simple`, `ABP_Unarmed`, `MM_Pistol_Fire_Montage`, `MM_Dash`.
- 한글 주석 스타일 유지. 커밋: Conventional Commits, 태스크당 1커밋, 푸터 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- 빌드 명령:
  ```
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0.
- GitHub 이슈 미생성 상태 — 커밋 제목에 `(#N)` 생략. 이슈가 생기면 미러링.

## 검증된 API (실물 확인됨)
- `ConstructorHelpers::FObjectFinder<USkeletalMesh>` + `GetMesh()->SetSkeletalMesh/SetRelativeLocation/SetRelativeRotation` — `RECharacterBase` 생성자에 실물 (플레이어 Manny 로드, 오프셋 Z-90/Yaw-90).
- `ConstructorHelpers::FClassFinder<UAnimInstance>` + `GetMesh()->SetAnimInstanceClass` — 동일 위치 실물 (`/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed`).
- `UAnimInstance::Montage_Play(Montage, 1.0f)` — `Variant_Combat/CombatCharacter.cpp:205`에 실물 (같은 마네퀸 스켈레톤에서 몽타주 재생 검증됨).
- `UAnimInstance::PlaySlotAnimationAsDynamicMontage(UAnimSequenceBase*, FName SlotNodeName, float BlendInTime, float BlendOutTime)` — 엔진 `Animation/AnimInstance.h` 공개 API. 슬롯명 기본 `"DefaultSlot"`.
- 애셋 실존 확인: `Content/Characters/Mannequins/Meshes/SKM_Quinn_Simple.uasset`, `Anims/Pistol/MM_Pistol_Fire_Montage.uasset`(UAnimMontage), `Anims/Unarmed/Jump/MM_Dash.uasset`(UAnimSequence).
- `FScreenshotRequest::RequestScreenshot(path, false, false)` — `UnrealClient.h`. 과거 #29 HP바 검증에서 사용 실적.

## 기존 파일 현황 (변경 대상)
- `Source/Project_RE/Core/REBossCharacter.cpp`: 생성자에 HP바 생성만 — 메시/ABP 로드 없음(안 보이는 원인). include에 ConstructorHelpers류 없음.
- `Source/Project_RE/Core/REAttackComponent.h/.cpp` (① 산출물): `FireInDirection(const FVector&)` — rate limit 통과 시 `LastFireTime = Now;` 후 트레이스·데미지, true 반환.
- `Source/Project_RE/Abilities/REGA_Dash.h/.cpp`: `ActivateAbility`에서 `Char`(ARECharacterBase*) 확보 → RootMotion 태스크 `Task->ReadyForActivation()`. 모션 재생 없음.
- `Source/Project_RE/Core/REGameMode.cpp`: `BeginPlay` — 보스 (600,0,90) 스폰 + 데모 발사 타이머. 스크린샷 TEMP 프로브 삽입 지점.

================================================================
## TASK 1: 보스 가시화 — Quinn 메시 + ABP_Unarmed
================================================================

### 1-1. Source/Project_RE/Core/REBossCharacter.cpp (수정)

include 블록에 추가:
```cpp
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"
```

`AREBossCharacter::AREBossCharacter()`의 HealthBar 블록 뒤에 추가:
```cpp
	// 보스 가시화 (M3.5 ②) — Quinn 메시 로드 (실패해도 크래시 없이 진행).
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (MeshAsset.Succeeded())
	{
		GetMesh()->SetSkeletalMesh(MeshAsset.Object);
		GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
		GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	}

	// 로코모션 애님BP — 고정형 보스라 idle 상태 재생이 목적.
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (AnimAsset.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimAsset.Class);
	}
```
(캡슐/콜리전 무변경 — 헤드리스 프로브 지오메트리 영향 없음. 메시는 폰 블로킹 콜리전 없음, 플레이어 메시와 동일 패턴.)

### 1-2. 빌드/검증 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

headless 회귀 확인 (메시는 코스메틱 — 기존 로그 동일해야 정상):
```bash
MSYS_NO_PATHCONV=1 timeout 30 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[Attack\]|\[Dash\]|Boss Spiral"
```
기대: `[Attack] hit boss` / `[Dash] activate ok=1` / `Boss Spiral` 로그 기존과 동일.

### 1-3. 커밋

```bash
git add Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M3.5): 보스 가시화 — Quinn 메시 + ABP_Unarmed 로드

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 2: 플레이어 발사 모션
================================================================

### 2-1. Source/Project_RE/Core/REAttackComponent.h (수정)

`#include` 블록 아래 전방선언 추가:
```cpp
class UAnimMontage;
```

private 섹션 `LastFireTime` 위에 추가:
```cpp
	/** 발사 모션 몽타주. 코스메틱 — 싱글/리슨은 서버 재생 = 화면 표시. */
	UPROPERTY()
	TObjectPtr<UAnimMontage> FireMontage;
```

### 2-2. Source/Project_RE/Core/REAttackComponent.cpp (수정)

include 추가:
```cpp
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "UObject/ConstructorHelpers.h"
```

생성자에 추가:
```cpp
	// 발사 모션 (M3.5 ②) — 실패해도 크래시 없이 진행(모션만 생략).
	static ConstructorHelpers::FObjectFinder<UAnimMontage> MontageAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Pistol/MM_Pistol_Fire_Montage.MM_Pistol_Fire_Montage"));
	if (MontageAsset.Succeeded())
	{
		FireMontage = MontageAsset.Object;
	}
```

`FireInDirection`의 `LastFireTime = Now;` 직후에 추가:
```cpp
	// 발사 모션 — 코스메틱, 판정과 무관하게 발사 자체에 재생.
	// TODO M4: 데디에선 원격 클라에 안 보임 — Multicast RPC로 교체.
	if (FireMontage)
	{
		if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
		{
			if (UAnimInstance* AnimInst = OwnerChar->GetMesh() ? OwnerChar->GetMesh()->GetAnimInstance() : nullptr)
			{
				const float Len = AnimInst->Montage_Play(FireMontage, 1.0f);
				UE_LOG(LogTemp, Log, TEXT("[Attack] fire montage len=%.2f"), Len);
			}
		}
	}
```

### 2-3. 빌드 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-4. 커밋

```bash
git add Source/Project_RE/Core/REAttackComponent.h Source/Project_RE/Core/REAttackComponent.cpp
git commit -m "feat(M3.5): 발사 모션 — MM_Pistol_Fire_Montage 재생

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 3: 대쉬 모션
================================================================

### 3-1. Source/Project_RE/Abilities/REGA_Dash.h (수정)

`#include` 아래 전방선언 추가:
```cpp
class UAnimSequence;
```

private 섹션 `DashStrength` 위에 추가:
```cpp
	/** 대쉬 모션 (AnimSequence — ABP DefaultSlot에 다이나믹 몽타주로 재생). 코스메틱. */
	UPROPERTY()
	TObjectPtr<UAnimSequence> DashAnim;
```

### 3-2. Source/Project_RE/Abilities/REGA_Dash.cpp (수정)

include 추가:
```cpp
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"
```

생성자에 추가:
```cpp
	// 대쉬 모션 (M3.5 ②) — 실패해도 크래시 없이 진행.
	static ConstructorHelpers::FObjectFinder<UAnimSequence> DashAnimAsset(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Dash.MM_Dash"));
	if (DashAnimAsset.Succeeded())
	{
		DashAnim = DashAnimAsset.Object;
	}
```

`ActivateAbility`의 `Task->ReadyForActivation();` 직후에 추가:
```cpp
	// 대쉬 모션 — 코스메틱, RootMotion 이동(위 태스크)과 독립.
	// TODO M4: 데디 원격 클라 표시용 Multicast 검토.
	if (DashAnim)
	{
		if (UAnimInstance* AnimInst = Char->GetMesh() ? Char->GetMesh()->GetAnimInstance() : nullptr)
		{
			const float Len = AnimInst->PlaySlotAnimationAsDynamicMontage(
				DashAnim, FName("DefaultSlot"), /*BlendInTime=*/0.1f, /*BlendOutTime=*/0.1f);
			UE_LOG(LogTemp, Log, TEXT("[Dash] anim len=%.2f"), Len);
		}
	}
```

### 3-3. 빌드 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 3-4. 커밋

```bash
git add Source/Project_RE/Abilities/REGA_Dash.h Source/Project_RE/Abilities/REGA_Dash.cpp
git commit -m "feat(M3.5): 대쉬 모션 — MM_Dash 슬롯 다이나믹 몽타주 재생

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 4: 검증 게이트 — headless 로그 + 실RHI 스크린샷
================================================================

### 4-1. headless 몽타주 길이 게이트

```bash
MSYS_NO_PATHCONV=1 timeout 30 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "fire montage|anim len"
```
기대:
```
[Attack] fire montage len=<0보다 큰 값>
[Dash] anim len=<0보다 큰 값>
```
판정:
- 둘 다 len>0 → 통과.
- `len=0.00` → 해당 애셋 로드 실패 또는 AnimInstance 없음. ConstructorHelpers 경로 재확인 후 수정 커밋.
- 로그 전무 → 프로브(①의 발사/기존 대쉬)가 안 돌았거나 멤버 null. TASK 2/3 배선 재확인.

### 4-2. TEMP 스크린샷 프로브 삽입 (커밋 금지)

`Source/Project_RE/Core/REGameMode.cpp`에 TEMP include 추가 (`FScreenshotRequest`/`FPaths`용):
```cpp
#include "UnrealClient.h"
#include "Misc/Paths.h"
```

`REGameMode::BeginPlay` 끝에 임시 추가:
```cpp
	// TEMP M3.5-② probe — 커밋 금지: 2.1s(대쉬 중)/5s(보스+탄막) 스크린샷, 15s 종료.
	FTimerHandle ShotT1, ShotT2, QuitT;
	GetWorld()->GetTimerManager().SetTimer(ShotT1, FTimerDelegate::CreateLambda([]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shot1.png"), false, false);
	}), 2.1f, false);
	GetWorld()->GetTimerManager().SetTimer(ShotT2, FTimerDelegate::CreateLambda([]()
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shot2.png"), false, false);
	}), 5.f, false);
	GetWorld()->GetTimerManager().SetTimer(QuitT, FTimerDelegate::CreateLambda([this]()
	{
		GEngine->Exec(GetWorld(), TEXT("quit"));
	}), 15.f, false);
```
(주의: `-windowed` 실행은 `-unattended`가 아니라 헤드리스 프로브 미발동 — 대쉬/발사 모션은 스크린샷에 안 찍힘. 스크린샷 게이트는 **보스 가시화**용, 모션 게이트는 4-1 로그.)

### 4-3. 실RHI 창모드 실행 (셰이더 컴파일로 첫 실행 오래 걸림 — timeout 300s)

```bash
MSYS_NO_PATHCONV=1 timeout 300 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -windowed -ResX=1280 -ResY=720 -nosplash -stdout -NoSound
```
15초 quit 타이머로 자체 종료. 스크린샷은 async write — 종료 직후 없으면 잠깐 뒤 재확인.

### 4-4. PNG 검증

`Saved/Shot1.png`, `Saved/Shot2.png`를 Read 툴로 확인:
- 보스 위치(플레이어 우측 +X 600)에 **Quinn 마네퀸 메시가 보임** → 통과. (기존: 아무것도 없음)
- 플레이어 Manny 메시도 기존대로 보임 (회귀 확인).

### 4-5. TEMP 프로브 제거

4-2에서 넣은 TEMP 블록·include 삭제 후:
```bash
git diff --stat
```
기대: `REGameMode.cpp` 변경 없음 (TEMP 완전 제거).

## 완료 후
- 최종 확인: `git diff dev --stat` — `REBossCharacter.cpp`, `REAttackComponent.h/.cpp`, `REGA_Dash.h/.cpp`만 변경.
- PR: base=`dev`, 제목 `feat(M3.5): 플레이어/보스 애니메이션 — 보스 가시화 + 발사/대쉬 모션`. 이슈를 만들었으면 메타 미러링. 본문: 목적/변경/검증(스크린샷 첨부)/스코프 경계. 푸터 `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- 남은 의도된 TODO 마커: `TODO M4` 2건 (발사/대쉬 Multicast) — M4 몫, 이번에 구현 금지.

## 하지 말 것 (스코프 밖)
- 사망/피격 리액트 모션 (스펙에서 제외 합의).
- Multicast RPC 구현 (M4 몫 — TODO 주석만).
- 스탯 Settings 이관 — M3.5 ③.
- ABP_Unarmed 애셋 수정 (uasset 편집 금지 — 코드 온리).
- 보스 스폰 좌표/캡슐 변경 (프로브 지오메트리 계약 — 메모리 경고).
