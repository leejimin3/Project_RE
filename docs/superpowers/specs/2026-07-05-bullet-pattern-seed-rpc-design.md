# 탄막 패턴 시드 RPC 인터페이스 설계 (M0 스켈레톤)

- **이슈**: #5 [M0] 탄막 패턴 시드 RPC 인터페이스 설계
- **마일스톤**: M0 — 셋업 + 아키텍처 결정
- **날짜**: 2026-07-05
- **라벨**: architecture, C++, networking

## 목표

M5 데디 협동 시 '총알을 네트워크에 태우지 않는' 아키텍처의 토대를 M0에서 확립한다.
서버가 `(패턴, 시드, 시작시각)`만 전송하고 서버/클라가 각자 동일 시드로 시뮬하는 구조.
싱글(M1~M3)에서는 로컬 직접 호출로 동작한다.

M0 범위는 **인터페이스 계약 + 실제 스폰 스켈레톤**까지. 패턴별 나선/부채꼴 수학은 M1.

## 네이밍 규칙

프로젝트 컨벤션은 `RE` 접두어 (`ARECharacterBase`, `AREGameMode`, `AREPlayerController`).
이슈 본문의 `ABossCharacter`는 컨벤션에 맞춰 **`AREBossCharacter`**로 구현한다.

## 컴포넌트

### 1. EBulletPattern enum

```cpp
UENUM(BlueprintType)
enum class EBulletPattern : uint8
{
    Spiral,
    Fan,
    Homing
};
```

- 위치: `Source/Project_RE/Mass/REBulletPattern.h` (새 파일)
- **슬롯만 정의.** 각 패턴의 실제 발사 수학은 M1에서 채운다.

### 2. AREBossCharacter : ACharacter

- 위치: `Source/Project_RE/Core/REBossCharacter.h` / `.cpp` (새 파일)
- **`ACharacter` 직접 상속.** `ARECharacterBase`는 카메라 붐이 달린 플레이어 폰이라 보스에 부적합.
- HP / TakeDamage 서버 권위 로직은 M2 범위 — M0에서는 TODO 주석만 남긴다.
- 공개 진입점:

```cpp
// 보스가 패턴 발사 시 호출. M0 싱글: 직접 엔티티 스폰.
void TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime);
```

### 3. TriggerBulletPattern 싱글 경로 (실제 스폰)

검증된 스폰 패턴(`AREGameMode::BeginPlay` 스모크 테스트)을 재사용한다:

```
UMassEntitySubsystem
  → GetMutableEntityManager()
  → CreateArchetype({ FBulletSimFragment, FBulletRenderFragment })
  → CreateEntity() × N
```

- `N` = 상수 placeholder (16개).
- 스폰 직후 각 엔티티의 `FBulletSimFragment.Velocity = 0`, `Lifetime = placeholder`.
- **패턴별 방향/속도 계산은 M1.** `switch(Pattern)` 각 case에 `// TODO M1` 마킹.
- 함수 진입점에 M5 전환 지점 마킹:

```cpp
// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트)
```

## 데이터 흐름

```
[M0 싱글]
  AREBossCharacter::TriggerBulletPattern(Pattern, Seed, StartTime)
    → EntityManager.CreateEntity × N (placeholder fragment)
    → UREBulletSimProcessor 가 매 틱 시뮬 (실제 이동은 M1)

[M5 데디]
  서버: Multicast_TriggerPattern(Pattern, Seed, StartTime) RPC 발사
    → 서버 + 클라 각자 동일 시드로 로컬 시뮬
    → 서버만 피격 판정 권위 / 클라는 비주얼만
    → 총알 자체는 네트워크 미전송
```

## 완료 기준

`AREBossCharacter::TriggerBulletPattern()` 호출 시 싱글에서 엔티티 N개가 스폰됨을 로그로 확인.
M1의 이동/수명/패턴 제너레이터와 연계된다.

## 범위 밖 (명시적 제외)

- 패턴별 나선/부채꼴/호밍 수학 → M1
- 실제 Multicast RPC 구현 → M5
- 보스 HP / 피격 판정 → M2
- ISM/Niagara 렌더 → M1 / M6
