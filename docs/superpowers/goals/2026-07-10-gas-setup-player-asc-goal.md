# 구현 목표: [M2 #23] GAS 플러그인 활성 + 플레이어 ASC 셋업

## 컨텍스트
UE 5.8 C++ 탑뷰 탄막(bullet-hell) 프로젝트. 이슈 **#23** (마일스톤 **M2: 플레이어 게임루프**).
이 goal이 하는 것: `GameplayAbilities`(GAS) 플러그인/모듈을 활성하고, 플레이어 폰 `ARECharacterBase`에 `UAbilitySystemComponent`(ASC, Mixed 복제)를 부착해 서버·클라 양쪽 `InitAbilityActorInfo`가 실행되게 한다. **어빌리티 자체는 구현 안 함** — ASC 초기화 토대만.

스코프 밖(손대지 말 것): 실제 대쉬 어빌리티(#25), `AttributeSet`(HP는 기존 `Replicated float` 유지), `GameplayEffect`/`GameplayCue`, 어빌리티 입력 바인딩, GAS로의 HP 전환.

설계 스펙: `docs/superpowers/specs/2026-07-10-gas-setup-player-asc-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-10-gas-setup-player-asc.md`
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치
`dev`에서 분기: `feature/M2-gas-setup`
(이미 생성돼 있을 수 있음. 없으면 `git checkout dev && git pull && git checkout -b feature/M2-gas-setup`. 이미 존재하면 체크아웃만.)

## 전역 제약
- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 자동 테스트 인프라 없음 → 게이트 = **에디터 빌드 성공(에러 0)** + **헤드리스 런타임 프로브 로그 관측**.
- ASC 소유 = Pawn(`ARECharacterBase`), 복제 모드 = `Mixed`. (사용자 확정 — 변경 금지)
- 기존 `Health`/`TakeDamage`/카메라·메시 로드 **변경 금지** (Surgical). 기존 스타일(한글 주석) 유지.
- 커밋: Conventional Commits, 한 태스크 = 한 커밋. `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` 푸터.

**빌드 명령 (공통):**
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0. (에디터 열려 있으면 종료 후 실행 — 파일락 회피.)

## 검증된 API (실물 확인됨)
- `UAbilitySystemComponent` — 헤더 `#include "AbilitySystemComponent.h"`.
- `IAbilitySystemInterface` — 헤더 `#include "AbilitySystemInterface.h"`. 순수 가상 `virtual UAbilitySystemComponent* GetAbilitySystemComponent() const`.
- `UAbilitySystemComponent::SetIsReplicated(bool)`, `::SetReplicationMode(EGameplayEffectReplicationMode)`, `::InitAbilityActorInfo(AActor* Owner, AActor* Avatar)`.
- `EGameplayEffectReplicationMode::Mixed`.
- `ACharacter::PossessedBy(AController*)` / `APawn::OnRep_PlayerState()` — 오버라이드 대상.
- `AActor::GetLocalRole()` → `ENetRole`(UENUM). `UEnum::GetValueAsString(GetLocalRole())` → `ROLE_Authority`/`ROLE_AutonomousProxy` 문자열 (추가 include 불필요, 엔진 코어).
- 모듈: `GameplayAbilities`, `GameplayTags`, `GameplayTasks` (Build.cs). 플러그인: `GameplayAbilities` (.uproject).

## 기존 파일 현황 (변경 대상)
- `Project_RE.uproject` — `Plugins`: `ModelingToolsEditorMode`(Editor), `StateTree`, `GameplayStateTree`, `MassGameplay`. **GAS 미활성.** 마지막 항목 = `MassGameplay`.
- `Source/Project_RE/Project_RE.Build.cs` — `PublicDependencyModuleNames` 마지막 두 항목 = `"MassEntity", "MassCore"`. GAS 모듈 없음.
- `Source/Project_RE/Core/RECharacterBase.h` — `class ARECharacterBase : public ACharacter`. 이미 `TakeDamage`/`GetLifetimeReplicatedProps` 선언, `Health`(Replicated)/`MaxHealth`, 카메라 멤버. GAS 없음.
- `Source/Project_RE/Core/RECharacterBase.cpp` — 생성자에 카메라/메시 세팅 + `Health = MaxHealth;`(23행). include에 `#include "Net/UnrealNetwork.h"`(11행) 존재. `TakeDamage`/`GetLifetimeReplicatedProps` 구현 있음. 파일 끝 = `GetLifetimeReplicatedProps` 닫는 `}`.
- `Source/Project_RE/Core/REGameMode.cpp` — `DefaultPawnClass = ARECharacterBase::StaticClass()` (프로브 시 이 폰이 Possess됨). 맵 `/Game/Level/Main` 사용.

================================================================
## TASK 1: GAS 플러그인 + 모듈 의존 활성
================================================================

### 1-1. `Project_RE.uproject` (수정)
`Plugins` 배열의 `MassGameplay` 블록을 아래로 교체 (뒤에 GAS 항목 추가):
```json
		{
			"Name": "MassGameplay",
			"Enabled": true
		},
		{
			"Name": "GameplayAbilities",
			"Enabled": true
		}
```

### 1-2. `Source/Project_RE/Project_RE.Build.cs` (수정)
`PublicDependencyModuleNames`의 마지막 부분:
```csharp
			"MassEntity",
			"MassCore"
		});
```
를 아래로 교체:
```csharp
			"MassEntity",
			"MassCore",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks"
		});
```

### 1-3. 빌드/검증 게이트
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

정적 확인:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "GameplayAbilities" Project_RE.uproject && grep -n "GameplayAbilities\|GameplayTags\|GameplayTasks" Source/Project_RE/Project_RE.Build.cs
```
기대: `.uproject` 플러그인 1매치 + `Build.cs` 3매치.

### 1-4. 커밋
```bash
cd E:/UnrealProjects/Project_RE && git add Project_RE.uproject Source/Project_RE/Project_RE.Build.cs && git commit -m "build(M2): enable GameplayAbilities plugin + GAS modules (#23)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

================================================================
## TASK 2: ARECharacterBase — ASC 부착 + IAbilitySystemInterface + ActorInfo 초기화
================================================================

### 2-1. `Source/Project_RE/Core/RECharacterBase.h` (수정)

**(a) include 블록** — 기존:
```cpp
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RECharacterBase.generated.h"

class USpringArmComponent;
class UCameraComponent;
```
를 아래로 교체:
```cpp
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "RECharacterBase.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;
```

**(b) 클래스 선언부** — 기존:
```cpp
UCLASS()
class ARECharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ARECharacterBase();

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 현재 체력. 서버 권위, 클라 복제. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;
```
를 아래로 교체:
```cpp
UCLASS()
class ARECharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ARECharacterBase();

	//~ IAbilitySystemInterface — GAS가 ASC를 찾는 진입점.
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	//~ 서버: Possess 시점 ASC ActorInfo 세팅.
	virtual void PossessedBy(AController* NewController) override;

	//~ 클라: PlayerState 복제 도착 시 ASC ActorInfo 세팅.
	virtual void OnRep_PlayerState() override;

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 게임플레이 어빌리티 시스템 컴포넌트. Pawn 소유, Mixed 복제. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
	UAbilitySystemComponent* AbilitySystemComponent;

	/** ASC ActorInfo 초기화 공용 헬퍼 (서버/클라 양쪽에서 호출). */
	void InitASCActorInfo();

	/** 현재 체력. 서버 권위, 클라 복제. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;
```

### 2-2. `Source/Project_RE/Core/RECharacterBase.cpp` (수정)

**(a) include** — `#include "Net/UnrealNetwork.h"`(11행) 다음에 추가:
```cpp
#include "AbilitySystemComponent.h"
```

**(b) 생성자** — `Health = MaxHealth;`(23행) 다음에 추가:
```cpp

	// GAS: ASC 부착 — Pawn 소유, 복제 켜고 Mixed 모드(오너 클라만 GE 복제).
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
```

**(c) 파일 맨 끝** (`GetLifetimeReplicatedProps` 닫는 `}` 다음)에 함수 4종 추가:
```cpp

UAbilitySystemComponent* ARECharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ARECharacterBase::InitASCActorInfo()
{
	// OwnerActor=AvatarActor=this (Pawn 소유). 오너 클라 판정은 Pawn→Controller 소유 체인으로 엔진이 처리.
	AbilitySystemComponent->InitAbilityActorInfo(this, this);
	UE_LOG(LogTemp, Log, TEXT("[GAS] ASC ActorInfo set (role=%s)"), *UEnum::GetValueAsString(GetLocalRole()));
}

void ARECharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// 서버 권위 경로 — Possess 즉시 ActorInfo 세팅.
	InitASCActorInfo();
}

void ARECharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	// 클라 경로 — PlayerState 복제 도착 후 ActorInfo 세팅.
	InitASCActorInfo();
}
```

### 2-3. 빌드/검증 게이트

**빌드:**
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

**헤드리스 프로브 A — 서버(Authority) 경로** (표준 `-game`, 로컬 플레이어=Authority):
```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_gas_probe.log &
sleep 30
grep "\[GAS\]" "Saved/Logs/RE_gas_probe.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대 로그 포함:
```
[GAS] ASC ActorInfo set (role=ROLE_Authority)
```
크래시/`Assertion failed` 없어야 함.

**헤드리스 프로브 B — 클라(AutonomousProxy) 경로** (리슨서버 + 클라 2프로세스):
```bash
cd E:/UnrealProjects/Project_RE && \
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main?listen" \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_gas_server.log & \
sleep 12 && \
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" "127.0.0.1" \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_gas_client.log & \
sleep 30
echo "--- server ---"; grep "\[GAS\]" "Saved/Logs/RE_gas_server.log"
echo "--- client ---"; grep "\[GAS\]" "Saved/Logs/RE_gas_client.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대:
- `--- server ---`: `role=ROLE_Authority`.
- `--- client ---`: `[GAS] ASC ActorInfo set (role=ROLE_AutonomousProxy)`.

> 클라 로그가 비면 서버 리슨 기동 지연 → `sleep 12` 늘려 재시도. **3회 내 미접속이면 중단하고 사용자 보고(무한 재시도 금지).**

### 2-4. 커밋
```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp && git commit -m "feat(M2): attach ASC to ARECharacterBase + init actor info both roles (closes #23)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

## 완료 후
- 브랜치 `feature/M2-gas-setup` → PR. **base=`dev`.** 이슈 #23 메타 미러링: label `setup`,`C++` + milestone `M2: 플레이어 게임루프` + assignee + project — 6개 필드 전부. PR 규칙은 메모리 `pr_creation_convention` 준수.
- 후속: #25 스페이스 대쉬 어빌리티(GAS + 쿨다운)가 이 ASC 위에 어빌리티를 부여.

## 하지 말 것 (스코프 밖)
- `AttributeSet` 생성/HP 전환 — HP는 기존 `Replicated float` 그대로. (#28 이후)
- 실제 대쉬/어빌리티 클래스(`UGameplayAbility`) — #25.
- `GameplayEffect`/`GameplayCue`/어빌리티 입력 바인딩 — #25.
- PlayerState 소유 ASC로의 이관 — M5 필요 시.
- 기존 `TakeDamage`/`Health`/`GetLifetimeReplicatedProps`/카메라·메시 코드 수정.
