# 설계 스펙: [M2 #28] 보스 HP + 서버권위 TakeDamage

## 개요
UE 5.8 C++ 탑뷰 탄막 프로젝트. 이슈 **#28** (마일스톤 **M2: 플레이어 게임루프**).
`REBossCharacter`에 서버권위 HP를 도입한다. `RECharacterBase`가 이미 구현한 Replicated `Health` + `TakeDamage` HasAuthority 가드 패턴을 그대로 미러링. `#26` `REAutoFireComponent`가 이미 `HitActor->TakeDamage(10)`을 호출중 — 엔진 기본 `TakeDamage`만 받던 계약을 이번에 override로 완성한다.

## 결정 요약 (브레인스토밍 확정)
| 항목 | 결정 | 근거 |
|---|---|---|
| 구현 위치 | `REBossCharacter`에 **직접 인라인** (컴포넌트 추출 안 함) | 사용처 `RECharacterBase`/`REBossCharacter` 2곳뿐, 각 15줄 안팎. 컴포넌트 추출은 과한 추상화(YAGNI). 보스가 `ACharacter` 직접 상속하는 이유(플레이어 폰 베이스 부적합)와 동일 논리로 `RECharacterBase` 상속도 부적합. |
| 크로스 오염 가드 | **불필요** | `TakeDamage` 호출 경로는 `REAutoFireComponent`(오토파이어→보스) 단일. RECharacterBase도 인스티게이터 가드 없이 동일 패턴 — 미러링. |
| 사망 시 탄막 정지 방식 | `bIsDead` 가드를 **`TriggerBulletPattern` 최상단**에 추가 | `REGameMode::DemoFireTimer`가 `DemoBoss` 포인터로 0.1s 루프 발사중 — 타이머 자체를 건드리지 않고 발사 함수를 no-op화. GameMode 미접촉으로 충돌 표면 최소화. |
| 사망 시 액터 처리 | **Destroy 안 함, 액터 유지** | `bIsDead=true`만 세팅, 로그만. 디스폰/시각처리는 스코프 밖(추후 마일스톤). |
| `bIsDead` 복제 여부 | **비복제 (서버 전용)** | 클라 시각처리(메시 숨김 등) 이번 스코프 아님 — 복제 불필요. `Health`만 복제(기존 RECharacterBase 패턴 동일). |
| 초기 HP | `Health=100`, `MaxHealth=100` (`EditDefaultsOnly`로 조정 가능) | 오토파이어 데미지 10/0.25s 기준 10초 내 사망 — 데모/프로브 검증에 적당한 시작값. |

## 아키텍처

### 수정 파일 — `Source/Project_RE/Core/REBossCharacter.h/.cpp`
**헤더 추가:**
- `UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Stats") float Health = 100.f;`
- `UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Stats") float MaxHealth = 100.f;`
- `bool bIsDead = false;` (서버 전용, 비복제, private)
- `virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;`
- `virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;`

**cpp 구현:**
- `TakeDamage`: `RECharacterBase::TakeDamage`와 동일 패턴.
  1. `!HasAuthority()` → `return 0.f`
  2. `Super::TakeDamage(...)` 호출해 `Applied` 획득
  3. `Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth)`
  4. `Health <= 0.f && !bIsDead` → `bIsDead = true; UE_LOG(LogTemp, Log, TEXT("[RE] Boss died (Health<=0)"));`
  5. `return Applied;`
- `GetLifetimeReplicatedProps`: `Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AREBossCharacter, Health);`
- `TriggerBulletPattern` 최상단에 가드 추가: `if (bIsDead) { return; }`

### 데이터 흐름
```
[서버] AutoFireComponent::Fire (0.25s 루프) → Boss->TakeDamage(10)
   → HasAuthority → Health -= 10 (clamp 0..100) → 복제
   → Health<=0 && !bIsDead → bIsDead=true, UE_LOG("[RE] Boss died (Health<=0)")
[서버] GameMode DemoFireTimer(0.1s 루프, 미변경) → Boss->TriggerBulletPattern
   → bIsDead==true → 즉시 return, 탄막 스폰 없음
[클라] Health 복제 수신. 시각처리(메시 숨김 등)는 스코프 밖.
```

## 검증 (자동 테스트 인프라 없음 → 빌드 + headless 프로브)
게이트:
1. **에디터 빌드 성공** (에러 0):
   ```
   "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
   ```
2. **headless 프로브 로그 관측** (`-game -nullrhi -unattended`, PIE 불필요). `REGameMode`가 DemoBoss 스폰 + 오토파이어(#26, 0.25s/10dmg) 자연 발동하므로 별도 셋업 불필요. 관측 로그:
   - `[AutoFire] hit boss, applied=10.0` 반복 → 10초 내 `[RE] Boss died (Health<=0)` 1회 출력
   - 사망 로그 이후 `[RE] Boss::TriggerBulletPattern` 관련 로그(Spiral/Fan 스폰 로그) 더 이상 안 찍힘 — `DemoFireTimer`는 계속 돌지만 no-op 확인
   - `Health` 복제: 서버 로그 기준 확인(클라 리슨서버 분리 관측은 이번 프로브 범위 밖, `DOREPLIFETIME` 선언 존재로 충분)

## 스코프 경계 (손대지 말 것)
- 보스 사망 후 디스폰/`Destroy()` → 스코프 밖 (액터 유지, `bIsDead` 플래그만).
- 사망 시각처리(메시 숨김/애니메이션/이펙트) → 스코프 밖.
- HP바 UI → **#29**.
- 탄막 대 플레이어 피격 판정 → **#27** (`bullet-player-hit`, 별도 스펙).
- `REAutoFireComponent`, `REGameMode` 로직 자체 → **일절 미접촉** (계약 소비만).
- 크로스 오염 방지 가드(인스티게이터 체크 등) → 없음 (RECharacterBase 동일 패턴 미러링).

## 커밋 계획 (Conventional Commits, 태스크당 1커밋)
1. `feat(M2): add boss Health/TakeDamage server-authoritative HP (#28)` — Health/TakeDamage/복제
2. `feat(M2): stop bullet pattern trigger after boss death (#28)` — `TriggerBulletPattern` bIsDead 가드
3. `test(M2): headless boss death probe log check (#28)` — 프로브/로그 확인

브랜치: `dev`에서 분기 → `feature/M2-boss-hp`.
