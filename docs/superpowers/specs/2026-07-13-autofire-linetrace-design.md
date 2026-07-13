# 설계 스펙: [M2 #26] 플레이어 자동사격 (라인트레이스)

## 개요
UE 5.8 C++ 탑뷰 탄막 프로젝트. 이슈 **#26** (마일스톤 **M2: 플레이어 게임루프**).
플레이어 **자동사격**: 주기 타이머로 최근접 `AREBossCharacter`를 라인트레이스 조준, 히트 시 서버 권위 `TakeDamage`.

## 결정 요약 (브레인스토밍 확정)
| 항목 | 결정 | 근거 |
|---|---|---|
| #28(보스 HP) 의존 | **독립 구현** | `AActor::TakeDamage`(엔진 기본)만 호출. 보스 HP 차감은 #28이 override로 수신 — `TakeDamage`가 이슈 간 계약. 검증은 히트+호출 로그로. |
| 로직 위치 | **별도 `UREAutoFireComponent`** | #25 세션이 `RECharacterBase`/`REPlayerController` 수정중 → 신규 파일로 충돌 표면 최소화(캐릭터엔 부착 코드만). #27/#29와도 분리 유지. |
| 구동 주체 | **서버 전용 타이머, RPC 없음** | 자동사격 = 입력 없음 → 클라가 보낼 게 없음. 이동/대쉬 RPC는 클라 입력 전달용이라 여기 부적용. 조준·트레이스·데미지 전부 서버 계산. 치팅 표면 0. |
| 튜닝 | FireInterval 0.25s / Damage 10 | 시작값. `EditDefaultsOnly`로 조정 가능. |
| 사거리 | 무제한 | 탑뷰 탄막 — 보스 상시 근거리. 사거리 제한은 필요 시 추가(YAGNI). |

## 아키텍처

### 신규 파일 — `Source/Project_RE/Core/`
**`REAutoFireComponent.h/.cpp`** — `UActorComponent` 서브클래스.
- 생성자: `PrimaryComponentTick.bCanEverTick = false` (틱 불필요, 타이머 구동).
- 튜닝 프로퍼티: `float FireInterval = 0.25f`, `float Damage = 10.f` (`EditDefaultsOnly, Category="AutoFire"`).
- `BeginPlay()`: `GetOwner()->HasAuthority()` 아니면 return. 서버면 `GetWorld()->GetTimerManager().SetTimer(FireTimer, this, &Fire, FireInterval, /*bLoop=*/true)`.
- `EndPlay()`: 타이머 해제.
- `Fire()` — 발사 1회:
  1. `TActorIterator<AREBossCharacter>`로 최근접 보스 탐색. 보스 1~2마리 전제 — 매 발사 전체 스캔, 캐싱 없음.
  2. 보스 없으면 스킵(로그 1회성 아님, Verbose).
  3. 시작점 = 오너 위치 + 총구 높이 오프셋(Z+50), 끝점 = 보스 액터 위치. `LineTraceSingleByChannel(ECC_Pawn)`, `FCollisionQueryParams`에 오너 ignore.
  4. 히트 액터가 `AREBossCharacter`면 `HitActor->TakeDamage(Damage, FDamageEvent{}, OwnerController, GetOwner())` 호출. 리턴값 로그.
  5. `#if ENABLE_DRAW_DEBUG` — `DrawDebugLine` (히트=빨강, 미스=초록, 0.2s 수명).

### 수정 파일
**`RECharacterBase.h/.cpp`** — 부착만:
- 헤더: 전방선언 + `UPROPERTY(VisibleAnywhere)` 멤버 `UREAutoFireComponent* AutoFireComponent`.
- 생성자: `CreateDefaultSubobject<UREAutoFireComponent>(TEXT("AutoFire"))` 1줄.
- (#25 세션과 같은 파일 — 생성자/멤버 추가만이라 충돌 나도 trivial merge.)

### 데이터 흐름
```
[서버] BeginPlay → HasAuthority → FireTimer(0.25s 루프)
[서버] Fire → 최근접 AREBossCharacter 탐색
        → LineTrace(오너+Z50 → 보스, ECC_Pawn, 오너 ignore)
        → 히트=보스 → HitActor->TakeDamage(10)   ← #28이 override로 HP 차감 (계약)
        → DrawDebugLine (개발 확인용)
[클라] 없음 — 입력·RPC 없음. HP 복제는 #28/#29 몫.
```

## 검증 (자동 테스트 인프라 없음 → 빌드 + headless 프로브)
게이트:
1. **에디터 빌드 성공** (에러 0):
   ```
   "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
   ```
2. **headless 프로브 로그 관측** (`-game -nullrhi -unattended`, PIE 불필요). `REGameMode`가 DemoBoss 스폰하므로 별도 셋업 없이 자동사격 자연 발동. 관측 로그:
   - `[AutoFire] hit boss, applied=10.0` — 트레이스 히트 + TakeDamage 리턴
   - `[AutoFire] miss` — 장애물 케이스 (있다면)
   - 보스 HP 감소 확인은 **#28 검증 몫** — 여기선 데미지 "전달"까지만.

   (DrawDebugLine은 headless 미검증 — 실 PIE 육안 확인.)

## 스코프 경계 (손대지 말 것)
- 보스 Health/TakeDamage override → **#28**. 이번엔 엔진 기본 `TakeDamage` 호출까지만.
- 탄막 피격 판정 → **#27**. HP바 → **#29**.
- 발사 이펙트/사운드/애니메이션 → 스코프 밖.
- `Abilities/`, `REPlayerController`(#25 세션 작업 영역) → **일절 미접촉**.
- 사거리 제한/타겟 우선순위 로직 → 없음 (최근접 단일 기준만).

## 커밋 계획 (Conventional Commits, 태스크당 1커밋)
1. `feat(M2): add REAutoFireComponent server-authoritative linetrace fire (#26)` — 컴포넌트
2. `feat(M2): attach AutoFireComponent to player character (#26)` — 캐릭터 부착
3. `test(M2): headless autofire probe log check (#26)` — 프로브/로그 확인

브랜치: `dev`에서 분기 → `feature/M2-autofire`.
주의: #25 세션이 현 워킹트리(`feature/M2-dash-ability`) 사용중 → 브랜치 전환 시점 별도 조율.
