# 보스 탄막 서버→클라 동기화 설계 (#84)

- **이슈**: #84 [M5] 보스 탄막 시드 동기화 — 서버→클라 브로드캐스트 + 클라 로컬 재현
- **마일스톤**: M5 — 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격
- **날짜**: 2026-08-11
- **라벨**: enhancement, networking, mass-entity, C++

## 목표

데디 서버에서 클라 화면에 보스 탄막이 보이게 한다. 서버와 클라의 탄 궤도가 **수 cm 이내로 일치**해야 한다 — 회피 게임에서 화면에 없는 탄이나 어긋난 탄에 맞으면 안 된다.

총알 자체는 네트워크에 태우지 않는다. 서버가 **발사 1회의 생성기 입력**만 브로드캐스트하고, 서버와 클라가 같은 제너레이터로 같은 탄을 만든다.

## 현재 상태

발사는 서버에서만 시작되고 스폰은 로컬 직접 호출이다. 클라는 발사가 일어난 사실 자체를 모른다.

- `Core/REGameMode.cpp:81` — `Boss->StartFiring(FMath::Rand())`. GameMode는 서버 전용.
- `Core/REBossCharacter.cpp:233` — `// TODO M5: Multicast_TriggerPattern RPC로 교체`. 현재는 로컬 직접 스폰.
- `Core/REBossCharacter.cpp:145` — `TriggerBulletPattern(Pattern, /*Seed=*/12345, /*StartTime=*/0.f)`. 시드 리터럴 하드코딩, `StartTime` 미사용.

#70(데디 파손 목록화) 5번 항목이 이 상태를 "M5 범위"로 명시하고 넘겼다.

## 이미 갖춰진 것 — 새로 만들 것은 RPC 2개뿐이다

프로세서 구성이 이 설계를 전제로 이미 깔려 있다:

| 프로세서 | ExecutionFlags | 근거 |
|---|---|---|
| `REBulletSimProcessor` / `REArcSimProcessor` | `AllNetModes` | `REBulletSimProcessor.cpp:16`, `REArcSimProcessor.cpp:11` — 서버·클라 둘 다 시뮬 |
| `REBulletRenderProcessor` / `REArcRenderProcessor` | `Standalone \| Client` | `REBulletRenderProcessor.cpp:24`, `REArcRenderProcessor.cpp:30` — 클라만 그림 |
| `REBulletHitProcessor` / `REArcHitProcessor` | `Standalone \| Server` | `REBulletHitProcessor.cpp:30`, `REArcHitProcessor.cpp:17` — 서버만 판정 |

`UREBulletSpawnSubsystem`은 권한 가드 없는 `UWorldSubsystem`이라 클라에서 그대로 호출된다. `UREBulletRenderSubsystem::OnWorldBeginPlay`(`:15`)는 데디에서 ISM 생성을 건너뛴다.

**즉 클라는 스폰만 시켜주면 나머지가 이미 돈다.**

### 시뮬이 결정적이라는 근거

두 Sim 프로세서 모두 RNG 없이 초기 상태와 누적 경과시간의 순수 함수다:

- `REBulletSimProcessor.cpp:41-42` — `Translation += Velocity * Dt`, `Lifetime -= Dt`. 위치 = `Start + Velocity × ΣDt`. 프레임레이트가 달라도 총 경과가 같으면 같은 위치.
- `REArcSimProcessor.cpp:33-42` — `Elapsed += Dt` 후 위치는 `Elapsed`의 순수 함수(Lerp + 포물선). 누적 오차가 위치에 쌓이지 않는다.

따라서 **초기 상태와 경과시간만 맞추면 궤도가 일치한다.**

## 왜 시드가 아니라 생성기 입력인가

M0 설계(`docs/superpowers/specs/2026-07-05-bullet-pattern-seed-rpc-design.md`)는 `Multicast_TriggerPattern(Pattern, Seed, StartTime)`을 계약으로 정했다. 그 계약은 **패턴 수학이 존재하지 않던 시점**의 것이고, 이후 생긴 세 가지가 시드만으로는 재현 불가능하게 만들었다.

1. **조준 패턴은 시드에서 유도할 수 없다.**
   - `REBossCharacter.cpp:314` — Fan의 `CenterAngleDeg`가 첫 플레이어 위치로 결정된다.
   - `REBossCharacter.cpp:165-172` — Artillery의 `PlayerLoc`. `Line`/`PlayerAimed` 모양이 이 값을 직접 먹는다.
   - 클라의 복제된 플레이어 위치는 보간·지연으로 서버와 다르다 → 같은 시드로도 궤적이 갈라진다.

2. **누적 상태가 있다.**
   - `REBossCharacter.h:61` `SpiralBaseAngleDeg` — 발사마다 `SpiralRotationStepDeg`만큼 누적(`:307`). 클라가 알려면 모든 발사를 빠짐없이 재생해야 한다.

3. **발사 수가 서버 런타임 상태에 의존한다.**
   - `REBossCharacter.cpp:262-271` — Spiral `Count`가 ISM 인스턴스 수 기반 클로즈드루프(M3 하네스). 데디 서버엔 ISM이 없어 폴백을 타고, 클라의 ISM 수는 서버와 다르다.

추가로 `PhaseRng`는 페이즈 선택(`:108`)·Artillery 모양 선택(`:114`)·`GenRandom`(`:194`)이 **같은 스트림을 공유**한다. 클라가 스트림을 맞추려면 페이즈 전환 전체를 동일하게 돌려야 하는데, 이는 타이머 정밀도 차이로 어긋난다.

**결론: 서버가 이미 결정한 값을 보낸다.** 클라는 로테이션도 RNG도 돌리지 않는다. 어려운 문제 세 개(타이밍·조준·RNG 스트림)가 전부 사라진다.

이 이슈의 제목은 "시드 동기화"지만 설계 결론은 **시드를 동기화하지 않는 것**이다. `PhaseRng`는 서버 전용 상태로 남고 `StartFiring(Seed)`의 시드는 네트워크에 나가지 않는다.

## 아키텍처

```
서버                                     클라
──────────────────────────────────      ──────────────────────────────
BeginPhase (패턴·모양·각도 결정)
FireCurrentPattern / FireArtillery
   │
   └─ Multicast_FireDirect(...)  ──────► Multicast_FireDirect_Implementation
        (서버에서도 로컬 실행)                │
             │                               │
             ▼                               ▼
      제너레이터 → SpawnBulletBatch     제너레이터 → SpawnBulletBatch
        (Elapsed ≈ 0)                     (Elapsed = 지연분)
             │                               │
      SimProcessor (AllNetModes)       SimProcessor (AllNetModes)
      HitProcessor (Server)            RenderProcessor (Client)
```

**서버도 자기 Multicast 구현체를 통해 스폰한다.** 언리얼 Multicast는 서버에서도 로컬 실행되므로, 기존 로컬 직접 스폰 경로를 이 구현체로 **대체**한다(추가가 아니다).

- 서버와 클라가 문자 그대로 같은 코드를 탄다 → 궤도 불일치의 여지가 구조적으로 없다
- 직접 호출을 없애므로 이중 스폰 위험이 없다
- 싱글/리슨도 같은 경로를 탄다(넷드라이버가 없으면 로컬 실행만)

## RPC 계약

기존 코드가 직선탄(`TriggerBulletPattern`)과 포물선탄(`FireArtillery`)으로 갈라져 있으므로 RPC도 둘로 맞춘다. 한 구조체에 합치면 절반이 항상 미사용 필드가 된다.

```cpp
UFUNCTION(NetMulticast, Reliable)
void Multicast_FireDirect(EBulletPattern Pattern, FVector_NetQuantize Origin,
                          float AngleDeg, int32 Count, float ServerTime);

UFUNCTION(NetMulticast, Reliable)
void Multicast_FireArtillery(EArtilleryShape Shape, FVector_NetQuantize Origin,
                             FVector_NetQuantize AimLoc, int32 CallSeed, float ServerTime);
```

| 필드 | 보내는 이유 |
|---|---|
| `Pattern` / `Shape` | 서버가 `PhaseRng`로 이미 뽑았다. 클라는 로테이션을 돌리지 않는다 |
| `AngleDeg` | Spiral=누적 `SpiralBaseAngleDeg`, Fan=조준 `CenterAngleDeg`. 둘 다 유도 불가 |
| `Count` | Spiral 클로즈드루프가 서버 ISM 상태에 의존 |
| `AimLoc` | Artillery `Line`/`PlayerAimed`가 먹는 플레이어 위치 |
| `CallSeed` | Artillery `Random`의 `GenRandom` 전용. 그 호출만 로컬 `FRandomStream`으로 처리 |
| `ServerTime` | 시간 보정 기준 (`GetServerWorldTimeSeconds()`) |

**보내지 않는 것:**

- `Speed` / `Lifetime` / `ArtilleryCount` / `SpreadDeg` — 쿡된 `Config/DefaultGame.ini`와 `EditDefaultsOnly` 값이라 양쪽이 이미 동일하다. 보내면 대역폭을 쓰면서 진실 원천을 둘로 쪼갠다.
- `AngleStepDeg` — `MakeSpiralRing`이 `360 / Count`로 유도한다(`REBulletPatternGenerator.cpp:73`). `Count`를 보내면 따라온다.

**Reliable을 쓴다.** 유실되면 그 발사분 16발이 클라에 영영 안 보이고, 회피 게임에서 보이지 않는 탄은 치명적이다.

**대역폭:** 페이로드 약 28B, `BossFireInterval=0.15`에서 초당 6.7회 → 약 190 B/s.

## 시간 보정

RPC 도착 지연만큼 클라가 뒤처진다. `BulletSpeed=200`에서 50ms 지연은 10cm 어긋남이므로 "수 cm 이내" 요구를 못 지킨다. 수신 시 경과분을 앞당겨 스폰한다.

```cpp
const float RawElapsed = FMath::Max(0.f, ServerWorldTime - ServerTime);
```

상한은 탄 종류마다 다르다 — 상한을 넘긴 탄은 이미 수명이 다했으므로 스폰 자체를 건너뛴다.

- **직선탄** — `RawElapsed >= Lifetime`이면 스킵. 아니면 `Location += Velocity * RawElapsed;  Lifetime -= RawElapsed;`
- **포물선탄** — `RawElapsed >= FlightTime`이면 스킵(이미 착지). 아니면 `FArcBulletFragment.Elapsed = RawElapsed`

서버에서는 발사 시각이 곧 현재이므로 `Elapsed ≈ 0`이 자연히 나온다. **같은 코드가 서버에선 무보정, 클라에선 보정으로 동작한다** — 분기가 필요 없다.

기준 시각은 `AGameStateBase::GetServerWorldTimeSeconds()`. 기본 GameState가 제공하므로 별도 GameState 클래스를 만들지 않는다.

**필요한 자료구조 변경:**

- 프래그먼트 변경 **없음**. `FArcBulletFragment.Elapsed`는 이미 존재한다(`REBulletFragments.h:43`).
- `REBulletPattern::FArcBulletSpawnParams`에 `float Elapsed = 0.f` 1개 추가 + `SpawnArcBullet`으로 전달. 기본값 0이라 기존 호출부는 무변경.
- 직선탄은 변경 없음 — `FBulletSpawnParams`의 `Location`/`Velocity`/`Lifetime`을 스폰 전에 조정하면 된다.

## 시작 게이트

서버는 클라보다 먼저 뜬다. 데디에서 클라 접속까지 실측 15~90초가 걸리고(pak 마운트), 그동안 정상상태로 약 1600발이 쌓인다(`BulletLifetime=15`, 초당 107탄). 그 탄들은 클라에 영영 보이지 않고, 플레이어는 보이지 않는 탄에 맞는다.

발사 시작을 클라 준비 신호로 옮긴다.

```cpp
// AREPlayerController — UFUNCTION(Server, Reliable)
// BeginPlay에서 로컬 PC가 준비되면 서버에 통지
if (IsLocalPlayerController()) { Server_NotifyReady(); }

// AREGameMode
void NotifyPlayerReady() { bPlayerReady = true; TryStartBossFiring(); }

void TryStartBossFiring()
{
    if (bPlayerReady && DemoBoss && !bFiringStarted)
    {
        bFiringStarted = true;
        DemoBoss->StartFiring(FMath::Rand());
    }
}
```

`AREGameMode::BeginPlay`의 `StartFiring` 호출을 제거하고, 대신 `BeginPlay` 끝에서도 `TryStartBossFiring()`을 부른다. **PC BeginPlay와 GameMode BeginPlay의 순서가 보장되지 않기 때문이다** — 스탠드얼론에서 PC가 먼저 오면 그 시점의 `DemoBoss`는 null이다. 둘 중 나중에 오는 쪽이 발사를 켠다.

`PostLogin` 대신 클라발 통지를 쓰는 이유: `PostLogin`은 서버측 PC 생성 시점이라 클라 월드가 아직 멀티캐스트를 받을 준비가 안 됐을 수 있다.

#85(N인 접속)가 가져갈 자리는 `TryStartBossFiring`의 조건 한 줄이다(`bPlayerReady` → `ReadyCount == ExpectedCount`).

## 엣지케이스

| 상황 | 처리 |
|---|---|
| **Standalone에서 Multicast 로컬 실행** | 넷드라이버 없는 월드에서 NetMulticast 구현체가 로컬 호출되는 것이 이 설계의 전제다. 성립하지 않으면 싱글이 통째로 깨진다 → 빌드 직후 첫 게이트로 확인 |
| **`GetServerWorldTimeSeconds()` 미준비** | 접속 직후 GameState 복제 전이면 0을 반환할 수 있고 `Elapsed`가 폭주한다. GameState가 null이면 `Elapsed=0` 폴백 + `Clamp(0, Lifetime)` 이중 방어 |
| **보스 사망 후 잔여 발사** | `TriggerBulletPattern:227` / `FireArtillery:150`의 `bIsDead` 가드가 서버에서 막으므로 RPC가 나가지 않는다. 무변경 |
| **Reliable 큐 부담** | 초당 6.7 × 28B는 안전하다. 다만 Artillery는 `ArtilleryFireInterval`로 주기가 다르고 `re.Profiling.KeepFiring`은 로테이션을 우회해 연발한다. 프로파일링은 standalone 전용이라 실위험은 낮으나 측정해 기록 |
| **데디 서버 ISM 부재** | Spiral 클로즈드루프가 `CurrentLive=-1` 폴백을 탄다. 이미 현재 동작이며 변화 없음 |

## 검증

자동화 테스트 인프라가 없다. 게이트는 빌드 + 로그 프로브 + 실RHI 스크린샷 + 데디 2프로세스다.

1. **빌드 게이트** — `Project_REEditor`와 `Project_REServer` 양쪽 `Result: Succeeded`
2. **싱글 회귀 (최우선)** — standalone 헤드리스에서 탄막 스폰 카운트가 종전과 동일. Multicast 로컬 실행 전제가 깨지면 여기서 즉시 드러난다
3. **궤도 일치 정량 측정** — 핵심 게이트. 서버와 클라가 각각 고정 시각(예: 발사 시작 후 10초)에 생존 엔티티 위치를 로그로 덤프하고 좌표 차이를 계산한다. 육안이 아니라 숫자로 "수 cm 이내"를 판정한다. 임시 프로브 코드가 필요하며 커밋하지 않는다
4. **클라 화면 실측** — 실RHI `-windowed` 스크린샷에 탄막이 보일 것. #70이 "안 보이는 게 정상"으로 남긴 항목의 해소 증거
5. **`scripts/dedi-verify.ps1` 판정 추가** — 클라측 스폰 로그 존재 + 3번 좌표 차이 임계
6. **M3 프로파일 회귀** — `scripts/profile.ps1` Mass 경로. 시작 게이트가 발사 시점을 PC BeginPlay로 옮기므로 측정 창 오염 여부 확인

## 범위 밖

- **중간 합류자 처리** → #85. 전원 입장 후 시작하는 입장 게이트가 구현되면 중간 합류가 구조적으로 존재하지 않는다. 만약 재접속 등으로 필요해지면, 생존 탄 1600개(약 45KB)를 보내는 대신 **최근 15초 발사 기록 약 100건(약 2.8KB)을 재생**하면 된다 — 이 설계의 시간 보정 경로를 그대로 재사용하므로 새 코드가 거의 없다
- **전원 입장 판정·대기 UI** → #85
- **N인 조준 타깃 정책** → #85. 지금은 `AimLoc`을 서버가 정해 보내므로 정책이 바뀌어도 RPC 계약은 그대로다
- **Homing 패턴** → #67
- **Niagara 렌더 전환** → #50

## 참고

- 유예 근거: #70 (5번 항목)
- M0 시드 RPC 계약(본 설계에서 변경): `docs/superpowers/specs/2026-07-05-bullet-pattern-seed-rpc-design.md`
- 패턴 로테이션 설계: `docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md`
- 데디 검증 절차: `docs/guides/dedicated-server.md`, `scripts/dedi-verify.ps1` (#82)
