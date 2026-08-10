# 발사 몽타주 Multicast (데디 원격 클라 표시) — 설계

날짜: 2026-08-10
상태: 구현 완료 (사후 정리 — 빌드/PIE 검증 대기)
이슈: #74 (M4)

## 목적

데디케이티드 서버에서 **원격 클라가 자기 발사 모션을 보게 한다.**

기존 코드는 `UREAttackComponent::FireInDirection`이 `HasAuthority()` 가드 안에서 서버 메시에 몽타주를 직접 재생했다. 리슨서버는 서버 화면 = 내 화면이라 보였지만, 데디는 서버에 화면이 없으므로 **아무도 발사 모션을 못 본다.** 소스에 `TODO M4`로 남아 있던 항목이다.

## 비범위

- **판정·데미지·rate limit 무변경.** 히트스캔(`LineTraceSingleByChannel`), `Boss->TakeDamage`, `AttackInterval * 0.9` 재검증 전부 그대로다. 이 작업은 코스메틱 전달만 고친다.
- 보스 탄막(Mass) 클라 표시 — M5 시드 동기화 범위.
- 발사 이펙트/사운드 — 없던 것을 새로 만들지 않는다.

## 설계 결정

### 1. RPC를 어디에 두는가 — 캐릭터 (이슈 b안)

`UREAttackComponent`는 **복제 설정이 없다** (`SetIsReplicatedByDefault` 미호출). 컴포넌트에 Multicast를 두려면 컴포넌트 복제를 새로 켜야 하고, 그러면 코스메틱 한 줄 때문에 이 컴포넌트가 통째로 복제 대상이 된다.

`ARECharacterBase`는 **이미 복제 액터이고 메시도 여기 있다.** RPC를 캐릭터에 두고 컴포넌트가 오너를 호출한다.

```
UREAttackComponent::FireInDirection (서버 권위)
  └─ ARECharacterBase::Multicast_PlayFireMontage(Montage)   ← NetMulticast, Unreliable
       └─ (데디 서버 제외) AnimInstance->Montage_Play
```

### 2. 신뢰성 — Unreliable

코스메틱이라 연사 중 1발 드랍이 판정/데미지에 영향이 없다. 반대로 Reliable이면 연사가 신뢰 큐를 점유해 **실제 게임플레이 RPC를 밀어낼 수 있다.**

### 3. 이중 재생 방지 — 구조로 차단

Multicast는 서버에서도 실행된다. 컴포넌트의 직접 재생을 **삭제**했으므로 재생 경로가 Multicast 하나뿐이고, 리슨서버/싱글에서도 정확히 1회 돈다. 플래그 가드 같은 런타임 분기가 필요 없다.

### 4. 데디 서버는 재생 생략

```cpp
if (!Montage || IsNetMode(NM_DedicatedServer)) { return; }
```

데디 서버에는 화면이 없다. `Mass/REBulletRenderSubsystem.cpp:15`, `Baseline/REActorBulletSpawner.cpp:35`의 기존 `NM_DedicatedServer` 조기 반환과 같은 패턴이다.

**결과로 데디 서버 로그에는 `[Attack] fire montage len=` 줄이 안 찍히는 것이 정상이다.** 클라 로그에만 나온다 — 검증 시 이걸 파손으로 오인하지 말 것.

### 5. 몽타주 애셋은 컴포넌트가 계속 소유

`FireMontage`는 `UREAttackComponent`의 `UPROPERTY`로 남는다. RPC 인자로 `UAnimMontage*`를 넘기는데, 애셋은 모든 프로세스가 CDO에서 같은 것을 로드하므로 오브젝트 참조가 정상 해석된다.

> 대쉬(#75)는 여기서 갈린다 — 그쪽은 애셋 소유를 어빌리티에서 캐릭터로 **옮겼다**. 어빌리티 인스턴스가 클라에 없어서 참조를 보낼 주체가 없기 때문이다. 컴포넌트는 클라에도 존재하므로 이 문제가 없다.

## 데이터 흐름

```
클라 좌클릭 → OnFire (로컬 페이싱) → Server_RequestFire(Dir)
  → 서버: FireInDirection — rate limit → 히트스캔 → TakeDamage   [판정, 무변경]
                          └→ Multicast_PlayFireMontage(FireMontage)  [코스메틱, 신규]
                               ├ 데디 서버: 생략
                               ├ 리슨 호스트: 1회 재생
                               └ 전 클라: 1회 재생
```

## 천장 (ponytail)

- 몽타주 재생 시점이 **서버 판정 시점**이다. 원격 클라는 자기 입력 후 RTT만큼 늦게 모션을 본다. 예측 재생(로컬 즉시 재생 + 서버 확인)은 넣지 않았다 — 지연 실측 전에 넣을 근거가 없다. 체감 문제가 실측되면 그때 별도 이슈.
- Unreliable이라 패킷 손실 시 그 발의 모션만 조용히 사라진다. 의도된 트레이드오프다.

## 파일 변경 요약

| 파일 | 변경 |
|------|------|
| `Core/RECharacterBase.h` | `Multicast_PlayFireMontage` 선언 + 배치/신뢰성 근거 주석, `UAnimMontage` 전방선언 |
| `Core/RECharacterBase.cpp` | `Multicast_PlayFireMontage_Implementation` 추가 (데디 생략 가드 + 재생 + 로그) |
| `Core/REAttackComponent.cpp` | 직접 재생 삭제 → 오너 캐릭터 RPC 호출. 불필요해진 include 3개 제거 |
| `Core/REAttackComponent.h` | `FireMontage` 주석 갱신 |

## 검증

`docs/superpowers/plans/2026-08-10-fire-montage-multicast.md` 참조. 빌드 게이트 + 데디/리슨/싱글 3경로 로그 대조.

## 참고

- 자매 작업: 대쉬 몽타주 Multicast (#75) — 같은 RPC 배치 규칙을 따른다. **두 브랜치 모두 `RECharacterBase.h/.cpp`의 인접 구간을 수정하므로 머지 시 충돌한다.**
- 커밋: `7f632ed feat(net): 발사 몽타주 NetMulticast 전환 (#74)`
