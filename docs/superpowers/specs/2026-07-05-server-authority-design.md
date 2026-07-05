# M0 #3 서버 권위 코드 패턴 확립 Design Spec

**이슈:** #3 [M0] 서버 권위 코드 패턴 확립
**마일스톤:** M0 (셋업 + 아키텍처 결정)
**날짜:** 2026-07-05
**브랜치(예정):** `feature/M0-server-authority`

## 목표

데디케이티드 서버 전환(M4)을 '재작성'이 아닌 '켜기'로 만드는 토대를 베이스 클래스 3종에 박는다. 지금부터 모든 게임상태 변경 코드가 이 패턴(HasAuthority 가드 + Server RPC + Replicated)을 따르도록 뼈대를 심는다.

## 스코프 결정 (YAGNI)

M0 #3은 **패턴 뼈대**를 심는 작업이다. 넷 실동작은 M5에서 실증하며, 지금은 싱글로만 돈다. 최소셋만 배선한다.

- **포함:** Character HP `Replicated` + `TakeDamage` 권위 가드 + `GetLifetimeReplicatedProps` / PlayerController `Server_RequestMove` RPC 선언 뼈대 / GameMode 서버권위 확인.
- **제외:** 실제 이동 RPC 배선·NavMesh(M2), OnRep/체력 UI(M6), 피격 이펙트·서버권위 피격판정 실증(M5), 사망 처리.

이유: 안 쓸 배선을 미리 하면 M2/M5에서 실제 요구가 드러날 때 재작업만 는다 (CLAUDE.md Simplicity First). 완료기준은 "3종 클래스에 권위 패턴이 박혀 있음 + 빌드 통과"이므로 뼈대만으로 충족된다.

## 설계 결정 (사용자 확정)

| 갈림길 | 결정 | 근거 |
|---|---|---|
| `Server_RequestMove` 범위 | **선언 뼈대만** (빈 `_Implementation` + `// TODO M2`) | #3 완료기준은 '패턴 박힘'. 기존 `OnClickMove`의 `// TODO M2` 주석과 정합. 실배선은 M2. |
| 데미지 진입점 | **엔진 `TakeDamage` 오버라이드** | UE 표준 데미지 파이프라인과 자연 연결 (M5 피격 실증 시 재연결 불필요). |
| Health 복제 | **plain `UPROPERTY(Replicated)`** | M0 뼈대엔 충분. OnRep(UI 갱신)은 필요해지는 M6에 추가. |

## 기존 코드와의 충돌 정리

- `AREPlayerController::OnClickMove`에 이미 `// TODO M2: 로컬 이동을 Server RPC ...로 교체` 주석 존재. #3이 요구하는 것은 그 **RPC 선언 뼈대**뿐이므로 로컬 이동은 유지하고 마킹만 명확히 한다. 실동작 교체는 M2로 남는다.
- `AREGameMode`는 이미 `AGameModeBase` 상속 + 주석에 "서버 권위라 HasAuthority 불필요" 명시. 코드 변경 없이 **확인**만으로 완료.

## 검증 가능한 완료 조건 (Acceptance)

1. `Project_REEditor` 빌드 성공, 에러 0.
2. `ARECharacterBase`: `Health` `UPROPERTY(Replicated)`, `TakeDamage` 오버라이드 내부 `HasAuthority()` 가드, `GetLifetimeReplicatedProps`에 `DOREPLIFETIME(ARECharacterBase, Health)` 존재.
3. `AREPlayerController`: `UFUNCTION(Server, Reliable) Server_RequestMove(FVector)` 선언 + 빈 구현 + `// TODO M2` 마커 존재.
4. `AREGameMode`: 서버 전용 로직 배치 확인 (코드/주석 정합).

넷 런타임 실증은 M5. 지금은 빌드 + 정적 존재 확인이 게이트.

## 설계

### 1. `ARECharacterBase` — HP + 복제

`.h`:

```cpp
public:
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                             AController* EventInstigator, AActor* DamageCauser) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    /** 현재 체력. 서버 권위, 클라 복제. */
    UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
    float Health = 100.f;

    /** 최대 체력. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
    float MaxHealth = 100.f;
```

`.cpp`:

```cpp
#include "Net/UnrealNetwork.h"

float ARECharacterBase::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                   AController* EventInstigator, AActor* DamageCauser)
{
    // 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
    if (!HasAuthority())
    {
        return 0.f;
    }

    const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
    // TODO M5: 서버권위 피격 판정/이펙트, 사망 처리
    return Applied;
}

void ARECharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARECharacterBase, Health);
}
```

- `ACharacter`는 기본 `bReplicates = true` → 별도 세팅 불필요.
- 생성자에서 `Health = MaxHealth` 정합 세팅(둘 다 100 기본이라 현재는 no-op이지만 향후 MaxHealth 조정 대비 명시).

### 2. `AREPlayerController` — Server RPC 뼈대

`.h`:

```cpp
protected:
    /** 이동 요청 서버 RPC 뼈대. 실배선은 M2(NavMesh). */
    UFUNCTION(Server, Reliable)
    void Server_RequestMove(FVector Target);
```

`.cpp`:

```cpp
void AREPlayerController::Server_RequestMove_Implementation(FVector Target)
{
    // TODO M2: 서버권위 이동 — NavMesh 패스파인딩 목표 설정.
    // 현재는 뼈대만. 클라 로컬 이동(OnClickMove)이 싱글 경로를 담당.
}
```

- `OnClickMove`는 변경 없음. 기존 `// TODO M2` 주석을 "→ Server_RequestMove 배선"으로 문구만 명확화.
- `Server` RPC는 `WithValidation` 없이 `Reliable`만으로 컴파일된다 (UE5 허용).

### 3. `AREGameMode` — 서버 전용 확인

- 코드 변경 없음. `AGameModeBase`는 서버에만 존재(클라 복제 안 됨) → 모든 로직이 곧 서버 권위.
- 헤더 주석의 "(서버 전용 로직은 M0 #3에서 추가)" 마커를 "서버 권위 확인 완료" 취지로 갱신.
- `RECharacterBase.h`의 "(HP/복제는 M0 #3에서 추가)" 마커도 완료 반영해 갱신.

## 테스트 전략

자동화 테스트 인프라 없음. 게이트는 **에디터 빌드 성공** + **정적 존재 확인**(위 Acceptance 2~4). 넷 런타임 검증(서버만 Health 차감, 클라 복제 수신)은 M5 협동 단계에서 실증한다.

## 파일 요약

- Modify: `Source/Project_RE/Core/RECharacterBase.h` (Health/MaxHealth + TakeDamage/GetLifetimeReplicatedProps 선언, 마커 주석 갱신)
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp` (`Net/UnrealNetwork.h` include + 두 함수 구현 + 생성자 Health 세팅)
- Modify: `Source/Project_RE/Core/REPlayerController.h` (`Server_RequestMove` 선언)
- Modify: `Source/Project_RE/Core/REPlayerController.cpp` (`Server_RequestMove_Implementation` + OnClickMove 주석 문구 갱신)
- Modify: `Source/Project_RE/Core/REGameMode.h` (마커 주석 갱신, 코드 변경 없음)
