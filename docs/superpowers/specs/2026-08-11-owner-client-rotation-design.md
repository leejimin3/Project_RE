# 오너 클라 캐릭터 회전 — 설계

날짜: 2026-08-11
상태: 구현 + PIE 실측 검증 완료
이슈: #79, #80 (M4)

## 목적

데디케이티드 서버에서 **오너 클라의 캐릭터 회전이 화면에 반영되게 한다.** 서버는 정상적으로 돌고 있었고 클라 화면에서만 안 돌았다.

부수적으로 **대쉬 중 발사를 차단한다**(#80). 같은 파일(`REPlayerController`)을 건드리므로 함께 처리한다.

## 문제 — 원인 두 겹

### 1. 오너 클라는 회전을 계산하지 않는다 (구조)

`UCharacterMovementComponent::ComputeOrientToMovementRotation`:

```cpp
if (Acceleration.SizeSquared() < UE_KINDA_SMALL_NUMBER)
{
    if (bHasRequestedVelocity && RequestedVelocity.SizeSquared() > ...)
        return RequestedVelocity.GetSafeNormal().Rotation();   // 서버 패스팔로잉만 해당
    return CurrentRotation;                                     // 오너 클라: 회전 없음
}
return Acceleration.GetSafeNormal().Rotation();
```

이 프로젝트의 이동은 입력 예측형이 아니다 — 클릭 지점만 서버로 보내고 이동은 서버 `SimpleMoveToLocation`이 돌린다. 따라서 오너 클라는 `Acceleration == 0`이고 패스팔로잉도 서버 전용이라 `bHasRequestedVelocity == false` → **`CurrentRotation`을 그대로 반환한다.**

회전이 복제되지도 않는다: `ReplicatedMovement`는 `COND_SimulatedOrPhysics`, `ShouldCorrectRotation()`은 기본 `false`(`CharacterMovementComponent.h:1137`, 프로젝트에 오버라이드 없음).

### 2. 서버 보정이 회전을 덮는다

`ClientAdjustPosition_Implementation` (`CharacterMovementComponent.cpp:11318`):

```cpp
if (CharacterMovementCVars::bUseLastGoodRotationDuringCorrection
    && (bOrientRotationToMovement || bUseControllerDesiredRotation)
    && (!OptionalRotation.IsSet() && ClientData->LastAckedMove.IsValid()))
{
    OptionalRotation = ClientData->LastAckedMove->SavedRotation;
}
...
UpdatedComponent->SetWorldLocationAndRotation(WorldShiftedNewLocation, OptionalRotation.GetValue(), ...);
```

보정마다 회전이 **마지막 ack 무브의 회전**으로 되돌아간다. #72에서 넣은 로컬 커서 회전이 여기서 지워졌다.

**#72가 이 증상을 악화시켰다.** 보정 스로틀을 0으로 낮춰(`NetworkMinTimeBetweenClientAdjustments = 0`) 위치 튐을 줄인 대가로, 회전을 지우는 보정이 10Hz에서 매 무브로 늘었다. 위치는 좋아지고 회전은 나빠진 맞교환이었는데 로그 검증만 해서 놓쳤고, #70 육안 관측에서 드러났다.

증상이 간헐적이었던 이유도 여기 있다 — 보정 타이밍이 일정하지 않다. 우클릭 **홀드** 시엔 `OnClickMove`가 `ETriggerEvent::Triggered`라 매 프레임 이동 요청이 나가 보정이 상시 발생 → **항상** 실패했다.

## 설계

오너 클라에서 회전을 **엔진에 맡기지 않고 직접 소유한다.** 서버 경로는 건드리지 않는다.

### 1. 오너 클라 한정 `bOrientRotationToMovement = false`

- 클라는 어차피 이 경로로 회전을 만들지 못하므로 잃는 것이 없다
- 동시에 위 보정 복원 분기의 **조건 자체가 불성립**해진다

`AREPlayerController::PlayerTick`에서 폰이 새로 잡힐 때 1회 적용한다. 캐릭터 쪽에 `BeginPlay`/`OnRep_Controller`를 새로 만들지 않는 이유는, PC는 정의상 로컬이고 그 폰은 정의상 로컬 소유라 **판정이 공짜**이기 때문이다.

### 2. 클라가 `Velocity` 방향으로 직접 회전

`Velocity`는 서버 보정으로 갱신되므로 서버 결과와 수렴한다. 보간 속도는 상수를 새로 만들지 않고 **CMC `RotationRate.Yaw`를 그대로 읽는다**(서버와 단일 출처).

정지(속도 ≈ 0)에서는 현재 회전을 유지한다 — 서버 CMC도 같은 조건에서 회전하지 않는다.

### 3. ⚠ 발사 후 페이싱 락은 "매 틱 재적용"이어야 한다

발사 시 커서 방향으로 돌리고 `AttackInterval` 동안 속도 기준 회전을 막는다. **초안은 락 구간에서 조기 반환만 했는데 그게 틀렸다** — 그 사이 보정이 회전을 되돌리면 복구하지 못해 "돌았다가 되돌아옴"이 됐고, 보정 타이밍이 랜덤이라 간헐 증상으로 나타났다.

락 구간에는 커서 yaw를 **매 틱 다시 세운다.** 회전은 코스메틱이라 재적용 비용은 무시할 만하다.

### 4. 대쉬 중 발사 차단 (#80)

상태는 `UREGA_Dash`의 `ActivationOwnedTags`(`State.Dashing`)가 이미 표현한다 — 새 플래그를 만들지 않는다.

차단은 **서버에만** 둔다(`Server_RequestFire_Implementation`). 클라 조기 반환도 검토했으나, `ServerOnly` 어빌리티의 `ActivationOwnedTags`는 loose 태그라 클라 복제가 보장되지 않는다. **믿을 수 없는 값으로 분기하느니 서버 단일 차단이 낫다.**

입력은 큐에 넣지 않고 버린다 — 대쉬가 0.2s로 짧아 체감 손해가 작다.

## 검증 (PIE, `Play As Client` + 데디 서버)

회전 4케이스 전부 통과:

| 케이스 | 이전 | 이후 |
|---|---|---|
| 우클릭 이동 중 이동 방향 회전 | ❌ 안 됨 | ✅ |
| 우클릭 **홀드** + 좌클릭 | ❌ 100% 실패 | ✅ |
| 우클릭 단발 이동 중 좌클릭 | 간헐 | ✅ |
| 정지 상태 좌클릭 단발 | 간헐 | ✅ |

**보정이 회전을 덮는다는 것이 로그로 확정됐다** (진단 로그 `[Facing] reverted`, 1분 플레이에 112회):

```
[Facing] reverted: cur=176.7 expected=-105.7
[Facing] reverted: cur=53.0  expected=-78.6
```

조금 어긋나는 정도가 아니라 거의 반대 방향으로 통째 교체된다. 매 틱 재적용이 이를 복구한다. 이 로그는 목적을 다했으므로 `Verbose`로 낮춰 기본 출력에서 뺐다 — 재발 시 `Log LogTemp Verbose`로 켠다.

#80 차단도 실측 확정:

```
07:31:38.016  [Dash] activate ok dir=...
07:31:38.065  [Attack] blocked: dashing      ← 대쉬 49ms 지점, 차단
07:31:38.316  [Attack] miss (blocked=0)      ← 대쉬 종료 후 정상 발사
```

`blocked: dashing` 13회 / 대쉬 17회, `hit boss` 12회. **과차단 없음**(대쉬 종료 후 발사는 정상).

> 첫 시도에서는 `blocked` 0회였다. 대쉬가 0.2s인데 클라 발사 페이싱이 0.25s라 새 클릭이 서버까지 도달하지 못했기 때문이다. **좌클릭을 홀드한 채 스페이스**를 눌러야 재현된다 — 회귀 테스트 시 이 조작을 써야 한다.

## 천장 (ponytail)

- 보정은 여전히 회전을 덮고 있고 우리는 매 틱 덮어쓰기로 이긴다. 근본 해법(회전 복제 또는 `ShouldCorrectRotation` 오버라이드)이 아니라 **표시 계층에서의 우선권 확보**다. 회전이 게임플레이 판정에 쓰이게 되면(예: 피격 방향 판정) 이 구조로는 부족하다.
- `PlayerTick`은 오너 클라에서만 실질 작업을 한다(`HasAuthority()` 즉시 반환). N인 협동(M5)에서 다른 플레이어는 simulated proxy라 회전이 정상 복제되므로 이 코드와 무관하다.

## 파일 변경 요약

| 파일 | 변경 |
|------|------|
| `Core/REPlayerController.h` | `PlayerTick` 선언, `FacingLockUntil` / `FacingLockYaw` / `FacingPawn` 멤버 |
| `Core/REPlayerController.cpp` | `PlayerTick` 구현(회전 구동 + 락 재적용 + 진단 로그), `OnFire`에 락 세팅, `Server_RequestFire`에 대쉬 차단 |

## 참고

- 선행: #72 (PR #78) — 보정 스로틀 조정, 발사 시 클라 로컬 회전 추가
- 관측 이슈: #70
- 구현 계획: `docs/superpowers/plans/2026-08-11-owner-client-rotation.md`
