# M2 #23 GAS 셋업 + 플레이어 ASC Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `GameplayAbilities`(GAS) 플러그인/모듈을 활성하고 플레이어 폰 `ARECharacterBase`에 `UAbilitySystemComponent`(ASC, Mixed 복제)를 부착해 서버·클라 양쪽 `InitAbilityActorInfo`가 실행되게 한다.

**Architecture:** ASC를 Pawn(`ARECharacterBase`)이 소유한다(새 PlayerState 없음). `IAbilitySystemInterface` 구현으로 GAS가 ASC를 찾는다. ActorInfo 초기화는 서버=`PossessedBy`, 클라=`OnRep_PlayerState` 양쪽에서 공용 헬퍼로 호출하며, 실제 `GetLocalRole()`을 로그로 찍어 프로브가 role별 경로를 관측한다. 어빌리티 자체·AttributeSet은 범위 밖(#25). HP는 기존 `Replicated float` 유지.

**Tech Stack:** UE 5.8 C++, GameplayAbilities(GAS) 플러그인, `UAbilitySystemComponent`, `IAbilitySystemInterface`, `EGameplayEffectReplicationMode::Mixed`.

## Global Constraints

- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 자동 테스트 인프라 없음 → 게이트 = **에디터 빌드 성공(에러 0)** + **헤드리스 런타임 프로브 로그 관측**.
- ASC 소유 = Pawn(`ARECharacterBase`), 복제 모드 = `Mixed`. (사용자 확정)
- 기존 `Health`/`TakeDamage`/카메라·메시 로드 **변경 금지** (Surgical). 기존 스타일(한글 주석) 유지.
- 커밋: Conventional Commits, 한 태스크 = 한 커밋. `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` 푸터.

### 빌드 명령 (공통)

```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0. (에디터 열려 있으면 종료 후 실행 — 파일락 회피.)

### 헤드리스 프로브 명령 (공통, Git Bash)

`MSYS_NO_PATHCONV=1` 필수. 맵 `/Game/Level/Main`은 `AREGameMode`(DefaultPawnClass=`ARECharacterBase`)를 사용 → 로컬 플레이어 폰 Possess 발생.

---

### Task 1: GAS 플러그인 + 모듈 의존 활성

**Files:**
- Modify: `Project_RE.uproject`
- Modify: `Source/Project_RE/Project_RE.Build.cs`

**Interfaces:**
- Consumes: 없음.
- Produces: `GameplayAbilities`/`GameplayTags`/`GameplayTasks` 모듈이 링크됨 → Task 2에서 `UAbilitySystemComponent`, `IAbilitySystemInterface` 사용 가능.

- [ ] **Step 1: `.uproject`에 GAS 플러그인 추가**

`Project_RE.uproject`의 `Plugins` 배열 마지막 항목 `MassGameplay` 블록:

```json
		{
			"Name": "MassGameplay",
			"Enabled": true
		}
```

를 아래로 교체 (뒤에 GAS 항목 추가):

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

- [ ] **Step 2: `Build.cs`에 모듈 의존 추가**

`Project_RE.Build.cs`의 `PublicDependencyModuleNames` 마지막 항목:

```csharp
			"MassEntity",
			"MassCore"
		});
```

를 아래로 교체 (GAS 3종 추가):

```csharp
			"MassEntity",
			"MassCore",
			"GameplayAbilities",
			"GameplayTags",
			"GameplayTasks"
		});
```

- [ ] **Step 3: 빌드 검증 (모듈 링크 확인)**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0. (GAS 모듈이 의존에 걸려 컴파일/링크됨.)

- [ ] **Step 4: 정적 존재 확인**

Run:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "GameplayAbilities" Project_RE.uproject && grep -n "GameplayAbilities\|GameplayTags\|GameplayTasks" Source/Project_RE/Project_RE.Build.cs
```
Expected: `.uproject` 플러그인 1매치 + `Build.cs` 3매치.

- [ ] **Step 5: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Project_RE.uproject Source/Project_RE/Project_RE.Build.cs && git commit -m "build(M2): enable GameplayAbilities plugin + GAS modules (#23)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: ARECharacterBase — ASC 부착 + IAbilitySystemInterface + ActorInfo 초기화

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Consumes: Task 1의 GAS 모듈.
- Produces:
  - `UAbilitySystemComponent* ARECharacterBase::GetAbilitySystemComponent() const override` — 부착된 ASC 반환 (`IAbilitySystemInterface`).
  - `UAbilitySystemComponent* AbilitySystemComponent` (protected) — Mixed 복제, Pawn 소유. #25 대쉬 어빌리티가 여기에 어빌리티를 부여한다.
  - `void PossessedBy(AController*) override` / `void OnRep_PlayerState() override` — 각각 서버/클라 ActorInfo 초기화 진입점.

- [ ] **Step 1: `.h` — 인터페이스 상속 + 멤버/함수 선언 추가**

`RECharacterBase.h` 상단 include 블록 — 기존:

```cpp
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RECharacterBase.generated.h"

class USpringArmComponent;
class UCameraComponent;
```

를 아래로 교체 (`AbilitySystemInterface.h` include + `UAbilitySystemComponent` 전방선언 추가):

```cpp
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "RECharacterBase.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UAbilitySystemComponent;
```

클래스 선언부 — 기존:

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

를 아래로 교체 (인터페이스 상속 + GAS 선언 추가, 기존 TakeDamage/HP는 그대로 유지):

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

- [ ] **Step 2: `.cpp` — include + 생성자 ASC 생성 + 함수 4종 구현**

`RECharacterBase.cpp` include 블록 — 기존 `#include "Net/UnrealNetwork.h"` (11행) 다음에 추가:

```cpp
#include "AbilitySystemComponent.h"
```

생성자 안, `Health = MaxHealth;` (23행) 다음에 추가 (ASC 생성 + 복제 + Mixed 모드):

```cpp

	// GAS: ASC 부착 — Pawn 소유, 복제 켜고 Mixed 모드(오너 클라만 GE 복제).
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
```

파일 맨 끝(`GetLifetimeReplicatedProps` 닫는 `}` 다음)에 함수 4종 추가:

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

- [ ] **Step 3: 빌드 검증**

Run:
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
Expected: `Result: Succeeded`, 에러 0.

- [ ] **Step 4: 헤드리스 프로브 — 서버(Authority) 경로 관측**

표준 `-game`은 로컬 플레이어가 Authority(서버) → `PossessedBy` 발화. Git Bash에서:
```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_gas_probe.log &
sleep 30
grep "\[GAS\]" "Saved/Logs/RE_gas_probe.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
Expected 로그 포함:
```
[GAS] ASC ActorInfo set (role=ROLE_Authority)
```
- `role=ROLE_Authority` → 서버 `PossessedBy` 경로 + ASC 생성 + `InitAbilityActorInfo` 도달 실증.
- 크래시/`Assertion failed` 없어야 함 → ASC 부착·모듈 링크 정상.

- [ ] **Step 5: 헤드리스 네트 프로브 — 클라(AutonomousProxy) 경로 관측**

`OnRep_PlayerState`(클라 경로)는 원격 클라가 있어야 발화. 리슨서버 + 클라 2프로세스로 관측. Git Bash에서:
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
Expected:
- `--- server ---`: `role=ROLE_Authority` (로컬 폰 + 접속 클라 폰 둘 다 서버 권위).
- `--- client ---`: `[GAS] ASC ActorInfo set (role=ROLE_AutonomousProxy)` → 클라 `OnRep_PlayerState` 경로 실증.

> 클라가 붙지 않아 client 로그가 비면: 서버 리슨 기동 지연 가능 → `sleep 12`를 늘려 재시도. 3회 내 미접속이면 중단하고 사용자에게 보고(무한 재시도 금지).

- [ ] **Step 6: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp && git commit -m "feat(M2): attach ASC to ARECharacterBase + init actor info both roles (closes #23)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 완료 후

- 브랜치 `feature/M2-gas-setup` → PR (base=dev, 이슈 #23 메타 미러링: label `setup`,`C++` + milestone `M2: 플레이어 게임루프` + assignee + project, 6개 필드 전부). PR 규칙은 메모리 `pr_creation_convention` 준수.
- 후속: #25 스페이스 대쉬 어빌리티(GAS + 쿨다운)가 이 ASC 위에 어빌리티를 부여.
