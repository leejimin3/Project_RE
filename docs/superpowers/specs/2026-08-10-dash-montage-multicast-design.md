# 대쉬 몽타주 Multicast (데디 원격 클라 표시) — 설계

날짜: 2026-08-10
상태: 구현 완료 (사후 정리 — 빌드/PIE 검증 대기)
이슈: #75 (M4)

## 목적

데디케이티드 서버에서 **원격 클라가 대쉬 모션을 보게 한다.**

`UREGA_Dash`는 `NetExecutionPolicy = ServerOnly`라 어빌리티가 서버에서만 실행된다. 모션을 서버 메시에 직접 재생하던 기존 코드는 리슨서버(서버 화면 = 내 화면)에서만 보였다. 소스의 `TODO M4: 데디 원격 클라 표시용 Multicast 검토.`가 이 작업이다.

동시에 이슈가 요구한 두 번째 항목 — **입력→대쉬 지연 측정 근거 확보** — 를 위한 로그 기준점을 남긴다.

## 비범위

- **`LocalPredicted` 전환은 하지 않는다.** 헤더의 두 번째 TODO다. 예측 전환은 쿨다운 커밋·예측키 처리가 얽혀 스코프가 커진다. 이번엔 지연을 **잴 수 있게만** 만들고, 실측 후 필요하면 별도 이슈로 올린다.
- 대쉬 이동 로직(`ApplyRootMotionConstantForce`, `DashStrength=3390`, `DashDuration=0.2`) 무변경.
- 쿨다운(`UREGE_DashCooldown`), `State.Dashing` 태그 계약 무변경.

## 설계 결정

### 1. RPC 배치 — 캐릭터 (#74와 동일 규칙)

발사 몽타주(#74)와 같은 자리에 둔다. 어빌리티에 두지 않는 이유가 하나 더 있다: **`ServerOnly` 어빌리티는 클라에 인스턴스가 없다.** RPC를 보낼 주체 자체가 없다.

```
UREGA_Dash::ActivateAbility (서버 전용)
  └─ ARECharacterBase::Multicast_PlayDashMontage()   ← NetMulticast, Unreliable
       └─ IgnoreRootMotion 전환 → PlaySlotAnimationAsDynamicMontage → 타이머로 복귀
```

### 2. 애셋 소유를 어빌리티 → 캐릭터로 이동

`DashAnim`(`MM_Dash`)의 `ConstructorHelpers` 로드를 `UREGA_Dash` 생성자에서 `ARECharacterBase` 생성자로 옮겼다.

이유: **RPC 인자로 오브젝트 참조를 보내지 않기 위해서다.** 애셋이 캐릭터 CDO에 있으면 모든 프로세스가 각자 같은 애셋을 로드하므로, RPC는 인자 없는 신호 하나면 된다.

> #74(발사)는 이 이동이 필요 없었다 — `UREAttackComponent`는 클라에도 존재하는 컴포넌트라 애셋을 그대로 들고 있어도 참조가 해석된다. 어빌리티만 클라에 없다.

### 3. ⚠ PR #59 루트모션 간섭 가드를 RPC 본체로 함께 이동

**이 작업의 핵심 위험이다.** `MM_Dash`는 `bEnableRootMotion` AnimSequence라, 다이나믹 몽타주로 재생하면 그 루트모션이 `CharacterMovement`에 먹혀 `ApplyRootMotionConstantForce`와 충돌한다. 과거 이걸로 **대쉬 거리가 680→405로 줄어드는 회귀**가 났고(PR #59), 재생 구간만 `IgnoreRootMotion`으로 전환해 해결했다.

가드를 서버 쪽에 남겨두고 재생만 클라로 보냈다면 **클라에서 충돌이 되살아나** 클라 화면의 대쉬 거리가 서버와 어긋난다. 그래서 가드 전체(`SetRootMotionMode(IgnoreRootMotion)` → 재생 → 타이머 복귀)를 RPC 본체로 옮겼다. 이 함수는 서버·클라 공통 경로이므로 가드가 양쪽에 동일하게 걸린다.

추가로 **재생 실패 경로를 보강**했다: `PlaySlotAnimationAsDynamicMontage`가 null을 반환하면 억제할 루트모션도 없으므로 즉시 기본 모드로 복귀한다. 기존 코드는 실패 시 `Len=0` 타이머로 복귀했는데, 실패 경로를 명시적으로 끊는 편이 안전하다.

### 4. 데디 서버 재생 생략 — 명시 가드 (초안 가정 반증됨)

초안은 "서버의 `GetMesh()->GetAnimInstance()`가 데디에서 null이라 자연히 no-op"이라 보고 `NM_DedicatedServer` 조기 반환을 넣지 않았다. **실측으로 반증됐다.** 스테이징 서버 exe + 원격 클라 검증에서 서버 로그에 그대로 찍혔다:

```
[Dash] anim len=0.97 (role=ROLE_Authority)   ← 데디 서버, 2회
```

데디 서버에도 `AnimInstance`가 존재하고 몽타주가 실제로 재생된다. 즉 서버가 `IgnoreRootMotion`으로 전환한 채 돌아 **서버 권위 이동 경로에 개입**하고 있었다(측정된 거리 자체는 정상이었지만 우연에 기댄 상태).

→ `#74`와 동일하게 명시 가드를 넣는다:

```cpp
if (!DashAnim || IsNetMode(NM_DedicatedServer)) { return; }
```

재검증 결과 서버 로그의 `[Dash] anim len=` 이 **0회**로 떨어졌고, 서버 대쉬 거리는 596.0으로 유지됐다.

### 4-1. ⚠ 타이머 람다는 약참조 — 클라 크래시 수정

루트모션 복귀 타이머가 `UAnimInstance*`를 **raw로 캡처**하고 있었다(PR #59 때부터). 서버/리슨에서만 돌던 시절에는 드러나지 않았지만, 재생이 클라로 넓어지자 데디 검증에서 바로 터졌다:

```
Assertion failed: Index >= 0 [UObjectArray.h:1083]
  FUObjectArray::IndexToObject()
  ...Multicast_PlayDashMontage_Implementation'::`2'::<lambda_1>::Execute()
  FTimerManager::Tick()
```

서버가 먼저 종료되자 클라 월드 정리 중 타이머가 돌았고, `IsValid()`가 **이미 파괴된** AnimInstance를 역참조해 죽었다. 실제 상황으로는 *대쉬 도중 접속 종료 / 레벨 전환 → 클라 크래시*다.

→ `TWeakObjectPtr<UAnimInstance>`로 캡처하고 `.Get()` 결과로 분기한다. 재검증에서 크래시 재현 없음.

### 5. 지연 측정 기준점

`AREPlayerController::OnDash`에 클라 로컬 로그 1줄을 넣었다:

```cpp
UE_LOG(LogTemp, Log, TEXT("[Dash] input sent (local)"));
```

이 타임스탬프와 클라의 `[Dash] anim len=` 타임스탬프 차이가 곧 **입력→대쉬 모션 지연**이다. `LocalPredicted` 전환 필요 여부를 이 수치로 판단한다.

## 데이터 흐름

```
클라 스페이스 → OnDash: [Dash] input sent (local)   ← 지연 측정 시작점
  → Server_Dash(Dir) → TryDash → TryActivateAbilityByClass
     → UREGA_Dash::ActivateAbility (서버 전용)
        ├ ApplyRootMotionConstantForce        [이동, 무변경]
        └ Multicast_PlayDashMontage()          [코스메틱, 신규]
             ├ 서버: AnimInstance null 기대 → no-op (검증 필요)
             └ 전 클라: IgnoreRootMotion → 재생 → 타이머 복귀
                        [Dash] anim len=... (role=...)  ← 지연 측정 끝점
```

## 천장 (ponytail)

- 지연은 그대로 남는다. 예측을 넣지 않았으므로 원격 클라는 RTT + 서버 처리만큼 늦게 모션을 본다. 측정 후 판단.
- `role=` 을 로그에 넣어 어느 인스턴스가 재생했는지 구분 가능하게 했다. 데디 서버 no-op 가정을 검증하려면 이 필드가 필요하다.

## 파일 변경 요약

| 파일 | 변경 |
|------|------|
| `Core/RECharacterBase.h` | `Multicast_PlayDashMontage` 선언, `DashAnim` 멤버 이동, `UAnimSequence` 전방선언 |
| `Core/RECharacterBase.cpp` | 생성자에 `MM_Dash` 로드 이동, `Multicast_PlayDashMontage_Implementation` 추가 (PR #59 가드 포함 + 실패 경로 보강) |
| `Abilities/REGA_Dash.cpp` | 직접 재생 블록 삭제 → RPC 호출 1줄. 애셋 로드 삭제, include 6개 정리 |
| `Abilities/REGA_Dash.h` | `DashAnim` 멤버 삭제, `UAnimSequence` 전방선언 삭제 |
| `Core/REPlayerController.cpp` | 지연 측정 기준점 로그 1줄 |

## 검증

`docs/superpowers/plans/2026-08-10-dash-montage-multicast.md` 참조. **대쉬 거리 회귀(PR #59 재발) 확인이 최우선 항목이다.**

## 참고

- 자매 작업: 발사 몽타주 Multicast (#74) — 같은 RPC 배치 규칙. **두 브랜치 모두 `RECharacterBase.h/.cpp` 인접 구간을 수정하므로 머지 시 충돌한다.**
- 선행 이력: PR #59 (루트모션 간섭 해소)
- 커밋: `83ac7c9 feat(net): 대쉬 몽타주 NetMulticast 전환 (#75)`
