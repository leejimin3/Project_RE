# M4 #72 데디 플레이어 이동 리플리케이션 보정 Design

**이슈:** #72 (M4: 데디케이티드 서버 전환 — 플레이어 위치 리플리케이션)
**날짜:** 2026-08-10
**선행:** 데디 서버 타겟 추가(49d3808) + 빌드 가이드(9fa3846) 완료. 클릭 이동 서버권위(#10, `Server_RequestMove` → `SimpleMoveToLocation`) 완료.

## 목표

리슨서버에서는 서버=내 화면이라 그냥 보였던 클릭 이동을, **데디에서 원격 클라 화면 기준으로도** 끊김/워프 없이 보이게 한다.

이 프로젝트의 이동은 **입력 예측형이 아니다.** 클라는 클릭 지점만 서버로 보내고 실제 이동은 서버 패스팔로잉이 돌린다. 즉 오너 클라의 `CharacterMovementComponent`에는 **재생할 입력이 없다** — 매 프레임 "입력 0"으로 자기 예측을 올리고 서버가 "아니다, 너는 여기 있다"로 되돌린다. 이 구조에서 실제로 무엇이 깨지는지를 엔진 코드로 확정하고, 최소 보정을 넣는다.

## 배경 (현재 상태 — 소스 대조)

- 클릭 → `Server_RequestMove(Hit.ImpactPoint)` (`Core/REPlayerController.cpp:105`)
- 서버: NavMesh 투영 검증 후 거부 or 패스팔로잉 (`Core/REPlayerController.cpp:202-215`)
- 발사: 서버가 `StopMovement()` + `StopMovementImmediately()` + `SetActorRotation(커서방향)` (`Core/REPlayerController.cpp:246-251`)
- 캐릭터: `bOrientRotationToMovement = true`, `RotationRate = (0,640,0)` (`Core/RECharacterBase.cpp:55-56`), `MaxWalkSpeed` 미오버라이드 = 엔진 기본 600
- 네트워크 스무딩/보정 파라미터 커스터마이즈 **없음** (엔진 기본값)

## 엔진 코드 근거

엔진 5.8 소스(`E:\UnrealEngine-5.8\UnrealEngine-5.8`) 실측. `CMC` = `Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`.

### 1. 데디 서버는 폰을 스스로 움직이지 않는다

`UCharacterMovementComponent::TickComponent` (`CMC:1751`):

```cpp
const bool bShouldPerformControlledCharMove = CharacterOwner->IsLocallyControlled()
                                              || (!GetController() && bRunPhysicsWithNoController)
                                              || (!GetController() && IsPlayingRootMotion());
if (bShouldPerformControlledCharMove) { ControlledCharacterMove(...); }   // → PerformMovement
else if (CharacterOwner->GetRemoteRole() == ROLE_AutonomousProxy)
{
    MaybeUpdateBasedMovement(DeltaTime);   // 이동 없음
    MaybeSaveBaseLocation();
    ServerAutonomousProxyTick(DeltaTime);
}
```

데디에서 원격 클라의 폰은 `IsLocallyControlled() == false`(컨트롤러가 원격 PC) + `RemoteRole == ROLE_AutonomousProxy` → **두 번째 분기**. 서버는 틱에서 `PerformMovement`를 부르지 않는다. 리슨서버는 같은 폰이 `IsLocallyControlled() == true`라 첫 분기를 탔다 — **그래서 지금까지 그냥 보였다.**

데디에서 폰이 전진하는 경로는 하나뿐이다:

```
클라 CMC 틱 → ReplicateMoveToServer → ServerMove RPC
  → 서버: MoveAutonomous(Accel=클라값=0) → PerformMovement → CalcVelocity
```

### 2. 패스팔로잉은 그 PerformMovement에 얹혀 동작한다

| 단계 | 위치 |
|------|------|
| `FollowPathSegment` → `RequestDirectMove(MoveVelocity, ...)` | `AIModule/.../PathFollowingComponent.cpp:1151-1160` |
| `RequestedVelocity` 저장 + `bHasRequestedVelocity = true` | `CMC:4024-4039` |
| `CalcVelocity` → `ApplyRequestedMove(...)`로 소비, 소비 후 플래그 해제 | `CMC:3879`, `CMC:6368` |

CMC는 `UseAccelerationForPathFollowing() == false`라 가속이 아니라 **속도 직접 지정** 경로다. 소비 지점이 `PerformMovement` 안이므로 서버는 클라 ServerMove가 올 때만 전진한다. `TickDispatch`(RPC 처리)가 액터 틱보다 앞이라 한 프레임 묵은 `RequestedVelocity`를 쓰지만 실사용상 무해.

**결론: 이동 자체는 데디에서도 동작한다.** 문제는 클라 화면이다.

### 3. 클라 예측은 매 프레임 서버와 반대로 간다

오너 클라 CMC의 `Acceleration`은 항상 0 → 자기 예측은 "브레이크 걸고 멈춤", 서버는 600uu/s 전진.

오차 허용치는 `MAXPOSITIONERRORSQUARED = 3.0`(≈1.73uu, `GameNetworkManager.cpp:29`)로 움직이는 동안 매번 초과한다. 그런데 보정 전송이 스로틀된다 (`CMC:11090`):

```cpp
const float AdjustmentTimeThreshold = bNetworkLargeClientCorrection ?
    FMath::Min(NetworkMinTimeBetweenClientAdjustmentsLargeCorrection, NetworkMinTimeBetweenClientAdjustments) :
    FMath::Max(...);   // = max(0.05, 0.10) = 0.10
```

→ **보정 최대 10Hz.** 그 100ms 동안 벌어지는 거리(`MaxWalkSpeed=600`, `GroundFriction=8`, `BrakingFrictionFactor=2`):

| | 100ms 이동량 |
|---|---|
| 서버 (패스팔로잉 600uu/s) | 60uu |
| 클라 (v0=600, 마찰 16/s 감쇠) | ≈30uu |
| **차이 = 보정 점프량** | **≈30uu, 10Hz** |

### 4. 그 보정은 스무딩이 없다 — 하드 텔레포트다

`ClientAdjustPosition_Implementation` (`CMC:11328`):

```cpp
UpdatedComponent->SetWorldLocation(WorldShiftedNewLocation, false, nullptr, ETeleportType::TeleportPhysics);
```

`SmoothCorrection`은 오너 클라에 적용되지 않는다 (`CMC:8198-8203`):

```cpp
const bool bIsSimulatedProxy = (GetLocalRole() == ROLE_SimulatedProxy);
const bool bIsRemoteAutoProxy = (GetRemoteRole() == ROLE_AutonomousProxy);
ensure(bIsSimulatedProxy || bIsRemoteAutoProxy);
```

오너 클라에서 자기 폰은 `LocalRole=AutonomousProxy`, `RemoteRole=Authority` → 둘 다 아니다. `NetworkSmoothingMode=Exponential`은 **시뮬레이티드 프록시(남의 폰)와 리슨서버 뷰 전용**이다. 오너 클라의 보정을 원래 가려주는 건 "예측 재생(replay)"인데, 여기선 예측이 매번 틀리므로 가려질 게 없다.

카메라(`CameraBoom`)는 캡슐(RootComponent)에 붙어 있다. 캡슐이 텔레포트하면 **카메라가 같이 튄다.** 메시 오프셋 스무딩조차 개입하지 않는다.

예상 증상: 이동 중 10Hz로 ~30uu 전방 툭툭 + 카메라 저더. 좌클릭 발사 순간엔 서버가 `StopMovement`하고 클라는 최대 100ms 더 미끄러지므로 보정이 **뒤로** 당긴다.

### 5. 회전은 이동과 다른 경로다 — 오너 클라엔 아예 안 온다

| 경로 | 조건 | 오너 클라 수신? |
|---|---|---|
| `AActor::ReplicatedMovement` (회전 포함) | `COND_SimulatedOrPhysics` (`ActorReplication.cpp:580`) | ✗ |
| 보정의 `PendingAdjustment.NewRot` | `bHasRotation = ShouldCorrectRotation()`, 기본 `false` (`CharacterMovementComponent.h:1137`) | ✗ |

오너 클라의 자기 폰 회전은 **100% 로컬 계산**이다. 이동 방향 회전은 `bOrientRotationToMovement`가 로컬 `Velocity`(보정으로 주입된 값)를 보고 돌리므로 대충 맞는다. 하지만 `Server_RequestFire`의 `SetActorRotation`(`REPlayerController.cpp:251`)은 전달되지 않는다 → **데디에서 "좌클릭 시 커서 방향으로 몸 돌림"이 남의 화면에만 보이고 내 화면에선 사라진다.**

### 6. off-navmesh 거부는 데디에서도 동일

`Server_RequestMove_Implementation`(`:205-211`)은 서버 권위 `ProjectPointToNavigation` 하나에만 의존한다. 롤/복제와 무관하고 클라 navmesh를 요구하지 않는다 → `[Move] rejected: off-navmesh` 경로 그대로 유효. 거부 시 서버는 아무것도 안 하고 클라도 (예측이 없으므로) 안 움직인다 → 일관됨.

## 설계 결정 (사전 확정)

1. **보정 스로틀 해제.** `NetworkMinTimeBetweenClientAdjustments = 0`, `NetworkMinTimeBetweenClientAdjustmentsLargeCorrection = 0`(둘 다 0이어야 함 — `max()`를 쓰므로 한쪽만 0이면 무효). 보정을 **없애는** 게 아니라 **잘게 쪼갠다**: 점프량이 10Hz×30uu에서 클라 무브 주기(≈60Hz)당 잔차로 줄어든다. 3절 계산을 16.6ms 창에 다시 대입하면 서버 9.96uu vs 클라 8.75uu → 잔차 ≈1.2uu로 오차 허용치(1.73uu) 근처라, 실제로는 몇 프레임에 한 번 2~4uu씩 보정된다.
   비용: 보정 RPC가 무브당 최대 1개(약 40B, 60Hz면 ~2.4KB/s per player). **플레이어 1명 전제.** 다인전이면 되돌리고 결정 3번으로 승격.
2. **발사 회전은 클라가 로컬로 같이 돈다.** 5절 때문에 서버는 오너 클라에 회전을 못 보낸다. `OnFire`는 이미 로컬에서 `Dir`을 계산하므로, 서버에 RPC를 보내면서 자기 폰도 같은 Yaw로 돌린다. 서버 rate limit에 걸려 실제 발사가 안 된 경우 클라만 돌 수 있으나 `bOrientRotationToMovement`가 다음 프레임에 진행 방향으로 되돌린다(자기수복). 리슨/싱글은 서버가 같은 값을 재적용하므로 무해.
3. **이동목표 복제 + 클라 예측은 보류.** 목표를 오너 클라에 복제해 클라도 직선 `AddMovementInput`으로 예측하면 보정이 거의 소멸하고 즉응성까지 얻는다. 하지만 약 40줄 + 발사정지/거부 시 취소 처리가 필요하고 곡선 nav 경로에서 발산한다. 코디네이터 게이트 답: **"곡선 nav 발산 위험과 40줄 비용이 지금 근거로는 정당화되지 않는다. 1·2번 측정 후 부족하면 그때 승격."**
4. **폰을 시뮬레이티드 프록시로 내리는 안은 불가.** `SetAutonomousProxy(false)`로 클라 예측을 없애면 1절의 두 분기 어디에도 안 걸려 **서버가 폰을 아예 못 움직인다.** 커스텀 CMC로 서버 틱을 직접 돌리는 것까지 가야 하므로 후보에서 제외.
5. **네트워크 스무딩 파라미터는 건드리지 않는다.** 4절대로 `NetworkSmoothingMode`/`NetworkMaxSmoothUpdateDistance`는 오너 클라 보정에 개입하지 않는다 — 이슈에 후보로 적혀 있었지만 코드상 무효다.

## 변경 범위

**수정 2개, 신규 0:**

| 파일 | 변경 |
|------|------|
| `Source/Project_RE/Core/RECharacterBase.cpp` | 생성자에 보정 스로틀 2줄 (결정 1) |
| `Source/Project_RE/Core/REPlayerController.cpp` | `OnFire`에 클라 로컬 `SetActorRotation` 1줄 (결정 2) |

헤더 변경 없음(둘 다 기존 public UPROPERTY / 기존 함수 내부). Build.cs / .uproject / Settings / navmesh 설정 변경 없음. `Server_RequestMove` 경로 무변경.

## 컴포넌트

### 1. `ARECharacterBase` 생성자 — 보정 스로틀

기존 회전 설정(`:55-56`) 바로 아래:

```cpp
	// 데디 보정 스로틀 해제 (#72) — 클릭 이동은 입력 예측형이 아니라 클라 예측(Accel=0=브레이크)이
	// 서버 패스팔로잉과 매 프레임 어긋난다. 기본 0.10s 스로틀이면 100ms마다 ~30uu가 밀린 뒤
	// 하드 텔레포트로 보정된다(오너 클라는 SmoothCorrection 대상이 아님) — 캡슐에 붙은 카메라까지 튄다.
	// 매 무브 보정하면 점프량이 1프레임 이동량(~10uu)으로 줄어든다.
	// ponytail: 플레이어 1명 전제(보정 RPC 1개/무브). 다인전이면 되돌리고 이동목표 복제+클라 예측으로 가라.
	GetCharacterMovement()->NetworkMinTimeBetweenClientAdjustments = 0.f;
	GetCharacterMovement()->NetworkMinTimeBetweenClientAdjustmentsLargeCorrection = 0.f;
```

### 2. `AREPlayerController::OnFire` — 클라 로컬 회전

`LastFireRequestTime = Now;` 와 `Server_RequestFire(Dir);` 사이:

```cpp
	// 데디 회전 보정 (#72) — 폰 회전은 오너 클라에 복제되지 않는다
	// (ReplicatedMovement=COND_SimulatedOrPhysics, CMC::ShouldCorrectRotation()=false).
	// 서버가 Server_RequestFire에서 하는 커서 방향 회전을 내 화면에서도 보이게 로컬로 같이 돈다.
	// 리슨/싱글에서는 서버가 같은 값을 다시 넣으므로 무해.
	P->SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
```

`Dir`은 이미 `GetSafeNormal2D()` 결과라 서버의 `Dir2D`와 Yaw가 같다.

## 검증 (완료 기준)

빌드/실행은 코디네이터 직렬 수행. 절차 전문은 `docs/superpowers/plans/2026-08-10-dedi-move-replication.md`.

1. **빌드 게이트** — `Build.bat Project_REEditor` → `Result: Succeeded`.
2. **보정 크기 측정** — 데디(서버+클라1)에서 `p.NetShowCorrections 1`. 서버 로그 `*** Server: Error for ... is N`의 N이 **한 자리 uu**(수 uu 수준). 3절의 스로틀 상태(~30uu)가 계속 보이면 결정 1이 안 먹은 것.
3. **클라 화면 관측** — 우클릭 이동 10회에 워프/러버밴딩/카메라 저더 없음. 이동 중 좌클릭 시 뒤로 당김 없음.
4. **회전** — 이동 중 좌클릭에서 **내 폰이 커서 쪽으로 도는 것이 내 화면에서** 보임(결정 2 전에는 안 보임).
5. **off-navmesh** — 맵 밖 우클릭 → 서버 로그 `[Move] rejected: off-navmesh`, 폰 무이동.
6. **리슨/싱글 회귀 없음** — `Play Standalone`에서 이동/발사/대쉬 정상.
7. **측정치 기록** — 2번의 실측 N값을 이슈 #72 코멘트와 본 문서에 남긴다(이슈 완료조건 "무엇을 왜 골랐는지 측정치 포함").

## 스코프 밖 (YAGNI)

- **이동목표 복제 + 클라 직선 예측** — 결정 3. 측정으로 부족이 증명되면 후속.
- **클라 navmesh(`bAllowClientSideNavigation`) + 클라 패스팔로잉 미러링** — 위보다 더 비싸다. 후속의 후속.
- **대쉬/GAS 루트모션의 데디 동작** — RootMotionSource 복제는 이동 복제와 별 경로다. 이슈 #72는 클릭 이동 전용, 대쉬는 별 이슈.
- **다인전 대역폭 튜닝** — 결정 1의 천장. M5에서 다룬다.
- **`ShouldCorrectRotation()` 오버라이드(커스텀 CMC 서브클래스)** — 회전을 서버 권위로 되돌리는 정공법이지만 새 클래스가 필요하다. 결정 2의 2줄로 충분한지 먼저 본다.
