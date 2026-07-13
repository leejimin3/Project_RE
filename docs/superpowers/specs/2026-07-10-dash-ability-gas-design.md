# 설계 스펙: [M2 #25] 스페이스 대쉬 어빌리티 (GAS 시간형)

## 개요
UE 5.8 C++ 탑뷰 탄막 프로젝트. 이슈 **#25** (마일스톤 **M2: 플레이어 게임루프**).
플레이어에 **스페이스 대쉬** 추가: GAS 어빌리티로 **커서 방향 고정거리 시간형 대쉬** + **쿨다운(GAS Cooldown GE)**. 서버권위.

토대(#23 완료): `ARECharacterBase`에 `UAbilitySystemComponent`(Pawn 소유, Mixed 복제) 부착됨. ActorInfo는 서버(`PossessedBy`)+클라(`OnRep_PlayerState`) 초기화됨. **어빌리티 0개, 입력 바인딩 없음** — 이 스펙이 첫 실전 어빌리티.

## 결정 요약 (브레인스토밍 확정)
| 항목 | 결정 | 근거 |
|---|---|---|
| 대쉬 종류 | 시간형 (RootMotion 고정거리) | 매번 같은 거리·마찰 무관 → 탄막 회피 신뢰도. 로스트아크 체감. 서버권위 정합 최상. |
| 방향 | 커서 방향 | 탑뷰 조준회피 직관. 로컬 계산 → 서버 전달. |
| 무적(i-frame) | **미포함 → #27로 미룸** | 피격판정(#27) 미구현. 이번엔 `State.Dashing` 태그만 부여, #27이 읽음. |
| 튜닝 | 거리 600uu / 시간 0.2s / 쿨다운 2.0s | 시작값. 플레이 후 조정 가능. |
| 활성 경로 | A — 서버 RPC 주도 (예측 미사용) | 기존 이동 RPC 패턴 대칭. M2 리슨서버 지연 0. 복잡도 최소. |
| 클라 예측 | **미사용 (M4로 문서화)** | 아래 §5. |

## 아키텍처

### 신규 파일 — `Source/Project_RE/Abilities/`
1. **`REGameplayTags.h/.cpp`** — 네이티브 게임플레이 태그 선언 (`.ini` 편집 없이 C++로).
   - `Cooldown.Dash` — 쿨다운 GE가 부여, 재활성 차단용.
   - `State.Dashing` — 대쉬 활성 동안 부여 (#27 무적판정이 읽을 계약).
2. **`REGE_DashCooldown.h/.cpp`** — `UGameplayEffect` 서브클래스. 생성자에서:
   - `DurationPolicy = EGameplayEffectDurationPolicy::HasDuration`
   - Duration = 2.0s (`FScalableFloat`)
   - `InheritableOwnedTagsContainer`에 `Cooldown.Dash` 추가.
   - (프로젝트 규약: uasset 없이 코드 정의.)
3. **`REGA_Dash.h/.cpp`** — `UGameplayAbility` 서브클래스.
   - 생성자: `NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly`, `InstancingPolicy = InstancedPerActor`, `CooldownGameplayEffectClass = UREGE_DashCooldown::StaticClass()`, `ActivationOwnedTags`에 `State.Dashing`.
   - `ActivateAbility`: `CommitAbility` (쿨다운 커밋) → 실패 시(쿨다운 중) `EndAbility`. 성공 시 아바타(`ARECharacterBase`)의 `PendingDashDir` 읽어 RootMotion 태스크 시작.
   - RootMotion: `UAbilityTask_ApplyRootMotionConstantForce` — WorldDirection=Dir, Strength=3000(uu/s), Duration=0.2 → 거리≈600. `OnFinish` 델리게이트 → `EndAbility`.
     - (대안: 정밀 고정거리 필요 시 `UAbilityTask_ApplyRootMotionMoveToForce`, 목표=loc+Dir*600. 플랜 단계에서 확정.)

### 수정 파일
4. **`RECharacterBase.h/.cpp`**
   - 멤버 `FVector PendingDashDir` + 접근자 `GetPendingDashDir()`.
   - `TryDash(FVector Dir)`: `PendingDashDir = Dir` 저장 → `AbilitySystemComponent->TryActivateAbilityByClass(UREGA_Dash::StaticClass())`.
   - `PossessedBy`: `InitASCActorInfo()` 직후, 서버 권위에서 `GiveAbility(FGameplayAbilitySpec(UREGA_Dash::StaticClass(), 1, INDEX_NONE, this))`. 핸들 멤버 저장(`DashAbilityHandle`).
5. **`REPlayerController.h/.cpp`**
   - `UInputAction* DashAction`(Boolean) 생성 + `IMC_TopDown`에 `EKeys::SpaceBar` 매핑.
   - `OnDash(const FInputActionValue&)` (로컬): `GetHitResultUnderCursor` → 커서 지점과 폰 위치의 XY 차 정규화(Z=0) → `Server_Dash(Dir)`.
   - `UFUNCTION(Server, Reliable) void Server_Dash(FVector Dir)` : `GetPawn()` → `Cast<ARECharacterBase>` → `Char->TryDash(Dir)`.

### 데이터 흐름
```
[로컬] Space → OnDash → 커서방향 계산(XY정규화) → Server_Dash(Dir) RPC
[서버] Server_Dash_Impl → Char->TryDash(Dir)
        → PendingDashDir=Dir; ASC->TryActivateAbilityByClass(REGA_Dash)
[서버] REGA_Dash::ActivateAbility
        → CommitAbility (쿨다운 GE 적용; Cooldown.Dash 2s)
          └ 쿨다운 중 → Commit 실패 → EndAbility (대쉬 취소)
        → State.Dashing 태그 활성 (ActivationOwnedTags, 자동)
        → RootMotion 태스크: Dir 방향 3000uu/s × 0.2s → 거리 600
        → OnFinish → EndAbility (State.Dashing 해제)
[클라] CharacterMovement가 루트모션 결과를 일반 이동복제로 전파
```

## §5. 클라 예측 미사용 근거 (포폴 대비 — 의도적 결정)
`REGA_Dash`는 `NetExecutionPolicy = ServerOnly`. **클라 예측(LocalPredicted) 미사용.**

이유: 클라 예측의 목적은 RTT 지연을 은폐하는 것. M2는 **리슨서버/싱글** 환경 → 로컬 플레이어 폰이 곧 서버 권위체 → 입력→반응 지연 0 → 예측으로 은폐할 지연이 없음. 예측은 예측키·롤백·misprediction 보정이라는 복잡도 비용을 수반하는데, 이득이 0인 곳에 넣는 것은 오버엔지니어링(YAGNI).

**어빌리티 구조는 예측 정책과 무관하게 설계.** M4 데디케이티드 서버 전환 시 원격 클라이언트에 실제 RTT 지연이 발생하면, 그때 원격클라 입력지연을 **측정**하고 `NetExecutionPolicy`를 `LocalPredicted`로 교체 (구조 재작성 없이 정책 플래그만). 이 "문제→측정→해결" 서사는 M3 프로파일링(왜 Mass) 서사와 동일한 측정기반 결정 패턴.

> **코드 TODO** (`REGA_Dash` 생성자 주석): `// TODO M4: 데디 원격클라 지연 측정 후 LocalPredicted 전환. 지금은 리슨서버라 지연 0 → 예측 이득 0.`

## §6. 검증 (자동 테스트 인프라 없음 → 빌드 + headless 프로브)
게이트:
1. **에디터 빌드 성공** (에러 0):
   ```
   "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
   ```
2. **headless 대쉬 프로브 로그 관측** (`-game -unattended`, PIE 불필요). 기존 이동 프로브(`RunHeadlessMoveProbe`) 옆에 `RunHeadlessDashProbe` 추가 — 서버권위·`FApp::IsUnattended()`에서만. 커서 경로 우회해 `Char->TryDash(FVector::ForwardVector)` 직접 호출, 로그 관측:
   - `[Dash] activate ok, State.Dashing on` — 활성 성공 + 태그
   - `[Dash] dist=~600` — 0.3s 후 위치 델타 (RootMotion 이동 확인)
   - `[Dash] blocked (cooldown)` — 즉시 재활성 시도 → Cooldown.Dash 차단
   - `[Dash] re-activate ok` — 2.1s 후 재활성 성공 (쿨다운 만료)

   (커서 방향 계산은 로컬/GUI 경로라 headless 미검증 — 서버 TryDash 권위 경로만 프로브. 커서방향은 실 PIE 플레이 시 육안 확인.)

## 스코프 경계 (손대지 말 것)
- 무적/i-frame 로직 → **#27**. 이번엔 `State.Dashing` 태그 부여만.
- 클라 예측 → **M4**.
- 이동(`Server_RequestMove`)/HP(`Health`,`TakeDamage`)/카메라/기존 GAS 셋업(ASC 부착·ActorInfo) → **변경 금지** (Surgical).
- HP의 GAS AttributeSet 전환 → 스코프 밖 (HP는 기존 `Replicated float` 유지).
- Cost GE(마나 등) → 없음. 쿨다운만.

## 커밋 계획 (Conventional Commits, 태스크당 1커밋)
1. `feat(M2): add native gameplay tags + dash cooldown GE (#25)` — 태그 + 쿨다운 GE
2. `feat(M2): add REGA_Dash root-motion dash ability (#25)` — 어빌리티
3. `feat(M2): grant dash ability + Space input wiring (#25)` — 캐릭터 GiveAbility + 컨트롤러 입력/RPC
4. `test(M2): headless dash probe (#25)` — 프로브

푸터: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

브랜치: `dev`에서 분기 → `feature/M2-dash-ability`.
