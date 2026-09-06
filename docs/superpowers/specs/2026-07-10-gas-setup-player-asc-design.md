# M2 #23 GAS 플러그인 활성 + 플레이어 ASC 셋업 Design Spec

**이슈:** #23 [M2] GAS 플러그인 활성 + 플레이어 ASC 셋업
**마일스톤:** M2 (플레이어 게임루프)
**날짜:** 2026-07-10
**브랜치:** `feature/M2-gas-setup`

## 목표

`GameplayAbilities`(GAS) 플러그인을 활성하고, 플레이어 폰(`ARECharacterBase`)에 `UAbilitySystemComponent`(ASC)를 부착한다. 스페이스 대쉬 어빌리티(#25)와 서버권위 데미지(#28)의 **선행 셋업**. 이 이슈는 어빌리티 자체를 구현하지 않는다 — ASC가 서버·클라 양쪽에서 올바르게 초기화되는 토대만 심는다.

## 스코프 결정 (YAGNI)

- **포함:** `.uproject` 플러그인 활성 / `Build.cs` 모듈 의존 / `ARECharacterBase`에 ASC 부착 + `IAbilitySystemInterface` 구현 + 서버·클라 `InitAbilityActorInfo` 배선 + 초기화 로그.
- **제외:** 실제 대쉬 어빌리티(#25), `AttributeSet`(HP는 기존 `Replicated float` 유지), `GameplayEffect`/`GameplayCue`, 어빌리티 입력 바인딩, GAS로의 HP 전환.

이유: 어빌리티 없이 ASC만 붙는 게 #23 완료기준. 안 쓸 AttributeSet·GE를 미리 배선하면 #25/#28에서 실제 요구가 드러날 때 재작업만 는다 (CLAUDE.md Simplicity First).

## 설계 결정 (사용자 확정)

| 갈림길 | 결정 | 근거 |
|---|---|---|
| ASC 소유 위치 | **Pawn(`ARECharacterBase`) 소유** | M2 범위는 대쉬 하나. 새 PlayerState 클래스·GameMode 배선 불필요. 이슈 "할 일"이 명시적으로 "플레이어 폰에 ASC 부착". 리스폰 어빌리티 지속(폰 사망 시 ASC 파괴)은 M2에 미존재 — 필요해지면 M5에서 PlayerState로 이관 (YAGNI). |
| 리플리케이션 모드 | **Mixed** | 플레이어 조종 ASC 표준. GE는 오너 클라만, Cue/Tag는 모두에게 복제 → 대쉬 쿨다운 UI가 로컬 플레이어에게 정확. M5 협동에서도 유효. |
| HP 처리 | **기존 `Replicated float` 유지** | `AttributeSet` 전환은 #23 범위 밖. 기존 `Health`/`TakeDamage` 가드 그대로. |

## 기존 코드와의 충돌 정리

- `ARECharacterBase`는 이미 `ACharacter` 상속. `IAbilitySystemInterface`를 **추가 상속**한다 (다중 상속 — UE 인터페이스는 순수 가상, 충돌 없음).
- 기존 `Health`(Replicated) / `TakeDamage`(HasAuthority 가드) / `GetLifetimeReplicatedProps` / 카메라·메시 로드는 **변경 없음**.
- `ACharacter`는 기본 `bReplicates = true` → ASC 복제 위한 별도 액터 세팅 불필요. ASC 자체는 `SetIsReplicated(true)` 필요.
- Pawn 소유 ASC의 owner를 `PlayerController`로 잡아야 Mixed 모드 오너 판정이 성립 → `InitAbilityActorInfo(this, this)`에서 OwnerActor=this(pawn), 실제 net owner는 pawn의 Controller가 담당(엔진이 pawn→controller 소유 체인으로 오너 클라 판정).

## 검증 가능한 완료 조건 (Acceptance)

1. `Project_REEditor` 빌드 성공, 에러 0.
2. `ARECharacterBase`가 `IAbilitySystemInterface` 구현, `GetAbilitySystemComponent()`가 부착된 ASC 반환.
3. 헤드리스 프로브(`-game -nullrhi`)에서 **서버·클라 양쪽** `InitAbilityActorInfo` 호출 로그 관측:
   - `[GAS] ASC ActorInfo set (role=Authority)` — 서버(PossessedBy)
   - `[GAS] ASC ActorInfo set (role=AutonomousProxy)` — 클라(OnRep_PlayerState)
4. ASC 리플리케이션 모드가 `Mixed`.

넷 런타임 어빌리티 실증은 #25. 지금 게이트는 **빌드 + ASC 초기화 로그(양쪽 role)**.

## 설계

### 1. `.uproject` — 플러그인 활성

`Plugins` 배열에 추가 (StateTree/MassGameplay 옆):

```json
{
    "Name": "GameplayAbilities",
    "Enabled": true
}
```

### 2. `Build.cs` — 모듈 의존

`PublicDependencyModuleNames`에 3종 추가:

```csharp
"GameplayAbilities",
"GameplayTags",
"GameplayTasks"
```

- `GameplayAbilities`는 `GameplayTags`/`GameplayTasks`에 의존 → 셋 다 명시.

### 3. `ARECharacterBase` — ASC 부착 + 인터페이스

`.h`:

```cpp
#include "AbilitySystemInterface.h"

class UAbilitySystemComponent;

UCLASS()
class ARECharacterBase : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    ARECharacterBase();

    //~ IAbilitySystemInterface
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

    //~ 서버: Possess 시점 ActorInfo 세팅 진입점
    virtual void PossessedBy(AController* NewController) override;
    //~ 클라: PlayerState 복제 도착 시 ActorInfo 세팅 진입점
    virtual void OnRep_PlayerState() override;

    // (기존 TakeDamage / GetLifetimeReplicatedProps 선언 유지)

protected:
    /** 게임플레이 어빌리티 시스템 컴포넌트. Pawn 소유, Mixed 복제. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities", meta = (AllowPrivateAccess = "true"))
    UAbilitySystemComponent* AbilitySystemComponent;

    // (기존 Health / MaxHealth / 카메라 멤버 유지)
};
```

`.cpp`:

```cpp
#include "AbilitySystemComponent.h"

// 생성자 내부 (기존 카메라/메시 세팅 이후)
AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
AbilitySystemComponent->SetIsReplicated(true);
AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

UAbilitySystemComponent* ARECharacterBase::GetAbilitySystemComponent() const
{
    return AbilitySystemComponent;
}

void ARECharacterBase::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    // 서버 권위 경로 — Possess 즉시 ActorInfo 세팅
    AbilitySystemComponent->InitAbilityActorInfo(this, this);
    UE_LOG(LogTemp, Log, TEXT("[GAS] ASC ActorInfo set (role=Authority)"));
}

void ARECharacterBase::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();
    // 클라 경로 — PlayerState 복제 도착 후 ActorInfo 세팅
    AbilitySystemComponent->InitAbilityActorInfo(this, this);
    UE_LOG(LogTemp, Log, TEXT("[GAS] ASC ActorInfo set (role=AutonomousProxy)"));
}
```

- OwnerActor=AvatarActor=`this`(pawn 소유). 실제 오너 클라 판정은 pawn→Controller 소유 체인으로 엔진이 처리.
- `InitAbilityActorInfo`는 서버·클라 양쪽에서 호출되어야 정상 (양쪽 ActorInfo 필요). 서버=`PossessedBy`, 클라=`OnRep_PlayerState`.

## 테스트 전략

자동화 테스트 인프라 없음. 게이트:
1. **에디터 빌드 성공** (모듈 링크 확인).
2. **헤드리스 프로브** — 기존 M0/M1 프로브 패턴(`-game -nullrhi`, `MSYS_NO_PATHCONV`) 재사용. 서버 로그에 `role=Authority`, 클라 로그에 `role=AutonomousProxy` 양쪽 관측. 싱글 PIE 리슨서버(`?listen`)로 서버+오토노머스 프록시 동시 확인하거나, 로그에 `HasAuthority()`/`GetLocalRole()` 문자열을 실제 role로 찍어 검증.

> 로그 문자열은 실제 `GetLocalRole()` 값을 찍도록 구현 시 `UEnum::GetValueAsString(GetLocalRole())`로 하드코딩 대신 실 role 출력 권장 (프로브 신뢰도↑). 위 예시의 고정 문자열은 경로 구분용 표시.

## 파일 요약

- Modify: `Project_RE.uproject` (`GameplayAbilities` 플러그인 추가)
- Modify: `Source/Project_RE/Project_RE.Build.cs` (`GameplayAbilities`/`GameplayTags`/`GameplayTasks` 의존 추가)
- Modify: `Source/Project_RE/Core/RECharacterBase.h` (`IAbilitySystemInterface` 상속, ASC 멤버, `GetAbilitySystemComponent`/`PossessedBy`/`OnRep_PlayerState` 선언)
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp` (`AbilitySystemComponent.h` include, 생성자 ASC 생성/복제/모드, 3개 함수 구현 + 초기화 로그)
