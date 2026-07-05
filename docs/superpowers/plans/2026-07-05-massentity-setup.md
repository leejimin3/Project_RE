# M0 #2 MassEntity 셋업 + 런타임 프로브 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mass 코어 모듈(`MassEntity`,`MassCore`)을 빌드에 배선하고, `AREGameMode::BeginPlay`에서 `UMassEntitySubsystem`으로 엔티티 1개를 생성해 Output Log로 실증한다.

**Architecture:** `MassEntity`/`MassCore`는 UE 5.8 엔진 런타임 모듈이라 .uproject 플러그인 토글 없이 Build.cs 의존 추가만으로 링크된다. 기존 `AREGameMode`(생성자만 존재)에 `BeginPlay` 오버라이드를 추가하고, throwaway 테스트 프래그먼트 1개로 엔티티를 생성한다. 스포너/프로세서/트레잇은 M1 범위라 제외.

**Tech Stack:** UE 5.8 C++, MassEntity/MassCore 엔진 모듈.

## Global Constraints

- 엔진 빌드 경로: `E:\UE_5.8\Engine\Build\BatchFiles\Build.bat`
- 빌드 타깃: `Project_REEditor Win64 Development`, `-project="E:\UnrealProjects\Project_RE\Project_RE.uproject"`
- `.uproject` 플러그인 변경 금지 (MassEntity/MassCore는 엔진 모듈). MassGameplay 플러그인은 M1에서 활성화.
- YAGNI: MassMovement/MassRepresentation/MassActors/StructUtils/프로세서/스포너 전부 M0 밖.
- API 매크로(`PROJECT_RE_API`) 불필요 — 게임 모듈 단일, 기존 `AREGameMode` 스타일과 동일.
- 클래스명/로그 문자열은 스펙과 동일하게: 프래그먼트 `FRETestFragment`, 로그 접두어 `[RE]`.
- 브랜치: `feature/M0-mass-setup` (이미 체크아웃됨).

**참고 — 테스트 전략:** 자동화 테스트 인프라 없음. Task 1은 **빌드 성공**을 게이트로, Task 2는 **빌드 성공 + PIE Output Log 관측**을 게이트로 삼는다. (프로브 자체가 런타임 스모크 테스트.)

**주의:** 새 파일 추가는 없고 기존 파일만 수정하므로 프로젝트 파일 재생성은 보통 불필요. 모듈 링크 에러 발생 시 `Project_RE.uproject` 우클릭 → "Generate Visual Studio project files" 후 재빌드.

---

### Task 1: Build.cs에 Mass 모듈 배선

**Files:**
- Modify: `Source/Project_RE/Project_RE.Build.cs:11-22` (`PublicDependencyModuleNames`)

**Interfaces:**
- Consumes: 없음(엔진 모듈만).
- Produces: 게임 모듈이 `MassEntity`/`MassCore` 심볼(`UMassEntitySubsystem`,`FMassEntityManager`,`FMassFragment`)에 링크 가능해짐 — Task 2가 사용.

- [ ] **Step 1: `PublicDependencyModuleNames`에 모듈 2개 추가**

`Source/Project_RE/Project_RE.Build.cs`의 `PublicDependencyModuleNames.AddRange(...)` 배열 마지막 항목 `"Slate"` 뒤에 콤마 추가하고 두 줄 삽입. 수정 후 배열은 아래와 같다:

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
			"MassCore"
		});
```

- [ ] **Step 2: 빌드 (모듈 링크 게이트)**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0. (아직 Mass 코드는 없지만 두 모듈이 해석·링크됨을 증명.)

- [ ] **Step 3: 커밋**

```bash
git add Source/Project_RE/Project_RE.Build.cs
git commit -m "build(M0): add MassEntity + MassCore module deps (#2)"
```

---

### Task 2: `AREGameMode` 런타임 프로브

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h` (include + `FRETestFragment` + `BeginPlay` 선언)
- Modify: `Source/Project_RE/Core/REGameMode.cpp` (include + `BeginPlay` 구현)

**Interfaces:**
- Consumes: Task 1의 `MassEntity`/`MassCore` 링크.
- Produces: 없음(최종 통합). PIE 실행 시 `[RE] Mass entity created: Index=... Serial=...` 로그.

- [ ] **Step 1: `REGameMode.h` 수정 — include + 프래그먼트 + BeginPlay 선언**

현재 `REGameMode.h`는 아래와 같다:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "REGameMode.generated.h"

/**
 *  탑뷰 게임모드. 기본 폰/컨트롤러를 RE 클래스로 지정.
 *  (서버 전용 로직은 M0 #3에서 추가)
 */
UCLASS()
class AREGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AREGameMode();
};
```

전체를 아래로 교체 (include `MassEntityTypes.h` 추가, `FRETestFragment` 추가, `BeginPlay` 선언 추가):

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MassEntityTypes.h"
#include "REGameMode.generated.h"

/**
 *  Mass 스모크 테스트용 throwaway 프래그먼트.
 *  M0 #2 프로브 전용 — M1에서 실제 탄막 프래그먼트로 교체·이동한다.
 */
USTRUCT()
struct FRETestFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 Dummy = 0;
};

/**
 *  탑뷰 게임모드. 기본 폰/컨트롤러를 RE 클래스로 지정.
 *  BeginPlay에서 Mass 엔티티 1개를 생성해 서브시스템 가동을 실증한다.
 *  (서버 전용 로직은 M0 #3에서 추가)
 */
UCLASS()
class AREGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AREGameMode();

protected:
	virtual void BeginPlay() override;
};
```

- [ ] **Step 2: `REGameMode.cpp` 수정 — include + BeginPlay 구현**

현재 `REGameMode.cpp`는 아래와 같다:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
}
```

전체를 아래로 교체 (`MassEntitySubsystem.h` include + `BeginPlay` 구현 추가). 생성자는 그대로 유지:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
}

void AREGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Mass 스모크 테스트: 서브시스템 얻고 엔티티 1개 생성 → 로그.
	// GameMode는 서버 권위라 HasAuthority 가드 불필요.
	if (UMassEntitySubsystem* Mass = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
	{
		FMassEntityManager& EM = Mass->GetMutableEntityManager();
		FMassArchetypeHandle Arch = EM.CreateArchetype({ FRETestFragment::StaticStruct() });
		FMassEntityHandle E = EM.CreateEntity(Arch);
		UE_LOG(LogTemp, Log, TEXT("[RE] Mass entity created: Index=%d Serial=%d"), E.Index, E.SerialNumber);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] UMassEntitySubsystem NULL"));
	}
}
```

- [ ] **Step 3: 빌드**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 4: PIE 런타임 검증 (최종 Acceptance)**

에디터로 프로젝트 열기(사용자 수동) → `/Game/Level/Main` 자동 로드 → PIE 재생.
`Window > Output Log` 확인:
- `[RE] Mass entity created: Index=0 Serial=1` 형태 로그 출력 (Index/Serial 값은 다를 수 있음).
- `[RE] UMassEntitySubsystem NULL` 경고가 **아니어야** 함.

로그가 나오면 이슈 #2 완료기준(`UMassEntitySubsystem` 접근 가능) 충족.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "feat(M0): add Mass entity probe in AREGameMode::BeginPlay (closes #2)"
```

---

## Self-Review

- **Spec coverage:** 스펙 §1 빌드배선→T1, §2 테스트프래그먼트→T2 Step1, §3 프로브→T2 Step2, §4 Acceptance→T1 Step2(빌드)+T2 Step3(빌드)+T2 Step4(PIE). 배경/스코프결정은 Global Constraints에 반영. 갭 없음.
- **Placeholder scan:** TBD/TODO 없음. 모든 코드 스텝에 완전한 코드 블록 존재. 프래그먼트 주석의 "M1에서 교체"는 스펙 명시된 의도된 마커.
- **Type consistency:** `FRETestFragment`(T2 정의)→T2 프로브에서 `FRETestFragment::StaticStruct()` 사용 일치. `UMassEntitySubsystem`/`FMassEntityManager`/`FMassArchetypeHandle`/`FMassEntityHandle` 전부 UE 5.8 헤더 검증된 실제 타입. 로그 문자열 `[RE] Mass entity created` 스펙 Acceptance와 일치.
- **Include 검증(실물):** `MassEntityTypes.h`가 `FMassFragment` 전이 포함(엔진 `MassCommonFragments.h` 동일 패턴). `MassEntitySubsystem.h`→`MassEntityManager.h` 체인으로 매니저/핸들 노출. `CreateArchetype(TConstArrayView<const UScriptStruct*>)`·`CreateEntity(FMassArchetypeHandle)`는 `MassEntityManager.h:162,284` 확인.
