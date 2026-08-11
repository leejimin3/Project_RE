# 대쉬 몽타주 Multicast Implementation Plan

> 사후 정리 문서. 구현은 `83ac7c9`에 이미 들어갔다 — 구현 스텝은 `[x]`, **남은 검증 스텝은 `[ ]`** 로 표기한다. 검증은 코디네이터(본체 worktree)가 수행한다.

**Goal:** 데디케이티드 서버에서 원격 클라가 대쉬 모션을 보게 하고, `LocalPredicted` 전환 판단에 쓸 입력→대쉬 지연을 잴 수 있게 만든다. 대쉬 이동 거리는 건드리지 않는다.

**Architecture:** 코스메틱 재생을 `ARECharacterBase::Multicast_PlayDashMontage`(NetMulticast, Unreliable)로 옮긴다. `UREGA_Dash`는 `ServerOnly`라 클라에 인스턴스가 없으므로 RPC 주체가 될 수 없고, 애셋(`MM_Dash`) 소유도 어빌리티 → 캐릭터로 옮겨 RPC 인자를 없앤다. PR #59의 `IgnoreRootMotion` 가드는 RPC 본체로 함께 옮겨 서버·클라 양쪽에 걸리게 한다.

**Tech Stack:** UE 5.8, C++ (GAS ServerOnly 어빌리티, NetMulticast RPC, 다이나믹 몽타주, RootMotionSource).

## Global Constraints

- 브랜치: `leejimin3/M4-dash-multicast` (worktree `C:\Users\leeji\orca\workspaces\Project_RE\M4-dash-multicast`)
- 빌드 타깃: `Project_REEditor Win64 Development`
- **빌드 게이트 — `-Project`는 반드시 이 worktree의 `.uproject`.** 본체 절대경로를 넣으면 캐시로 10초 만에 "성공"하고 실제 변경은 검증되지 않는다:
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat" `
    Project_REEditor Win64 Development `
    -Project="C:\Users\leeji\orca\workspaces\Project_RE\M4-dash-multicast\Project_RE.uproject" `
    -WaitMutex -NoHotReload
  ```
- 디스크 여유 46GB — **worktree 병렬 빌드 금지.** 코디네이터가 직렬로 돌린다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

---

### Task 1: 애셋 소유 이동 + Multicast RPC 추가

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`, `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Produces: `ARECharacterBase::Multicast_PlayDashMontage()` — Task 2의 어빌리티가 호출.

- [x] **Step 1: `DashAnim` 멤버를 캐릭터로 이동** — `RECharacterBase.h` protected에 `TObjectPtr<UAnimSequence> DashAnim`, `class UAnimSequence;` 전방선언.
- [x] **Step 2: 생성자에 애셋 로드 이동** — `ConstructorHelpers::FObjectFinder<UAnimSequence>`로 `/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Dash.MM_Dash`. 실패해도 크래시 없이 진행(모션만 생략).
- [x] **Step 3: RPC 선언** — `UFUNCTION(NetMulticast, Unreliable) void Multicast_PlayDashMontage();`
- [x] **Step 4: 구현 — PR #59 가드 포함.** 순서가 중요하다:
  1. `DashAnim` / `AnimInstance` null 가드
  2. `AnimInst->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion)`
  3. `PlaySlotAnimationAsDynamicMontage(DashAnim, "DefaultSlot", 0.1f, 0.1f)`
  4. **null 반환 시 즉시 `RootMotionFromMontagesOnly` 복귀 후 return** (실패 경로 보강)
  5. `[Dash] anim len=%.2f (role=%s)` 로그 — `role`은 데디 서버 no-op 검증에 필요
  6. `GetWorldTimerManager().SetTimer(..., Len, false)` 로 기본 모드 복귀
- [x] **Step 5:** include 추가 (`Animation/AnimSequence.h`, `Animation/AnimMontage.h`, `TimerManager.h`).

- [ ] **검증:** 빌드 게이트 통과.

---

### Task 2: 어빌리티에서 직접 재생 제거

**Files:**
- Modify: `Source/Project_RE/Abilities/REGA_Dash.cpp`, `Source/Project_RE/Abilities/REGA_Dash.h`

**Interfaces:**
- Consumes: Task 1의 `Multicast_PlayDashMontage`.

- [x] **Step 1:** `ActivateAbility`의 몽타주 블록 전체를 `Char->Multicast_PlayDashMontage();` 한 줄로 교체.
- [x] **Step 2:** 생성자의 `MM_Dash` 로드 삭제, `DashAnim` 멤버 삭제, `UAnimSequence` 전방선언 삭제.
- [x] **Step 3:** 불필요해진 include 6개 제거 (`SkeletalMeshComponent`, `AnimInstance`, `AnimSequence`, `AnimMontage`, `ConstructorHelpers`, `TimerManager`).
- [x] **Step 4:** `ApplyRootMotionConstantForce` 태스크 / `DashStrength` / `DashDuration` / `ActivationOwnedTags` **무변경** 확인.

- [ ] **검증:** 빌드 게이트 통과 + `git diff dev...HEAD -- Source/Project_RE/Abilities/REGA_Dash.cpp` 에 RootMotion 태스크 구간 변경이 없음을 눈으로 확인.

---

### Task 3: 지연 측정 기준점

**Files:**
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

- [x] **Step 1:** `OnDash`의 `Server_Dash(Dir)` 직전에 `UE_LOG(LogTemp, Log, TEXT("[Dash] input sent (local)"));`

- [ ] **검증:** 빌드 게이트 통과.

---

### Task 4: 런타임 검증 (코디네이터) — 회귀 확인이 최우선

**Files:** 없음 (실행 검증)

- [ ] **Step 1 — 대쉬 거리 회귀 (PR #59 재발 방지, 최우선):**
  헤드리스 대쉬 프로브로 이동 거리를 재고 기존 값(약 600uu, `DashStrength 3390 × DashDuration 0.2`)과 대조한다. `AREPlayerController::RunHeadlessDashProbe` 경로:
  ```bash
  MSYS_NO_PATHCONV=1 "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "C:/Users/leeji/orca/workspaces/Project_RE/M4-dash-multicast/Project_RE.uproject" \
    /Game/Level/Main -game -nullrhi -unattended -log
  ```
  **405uu 근처가 나오면 PR #59 회귀다 — 즉시 중단하고 `IgnoreRootMotion` 가드 위치를 재검토한다.**

- [x] **Step 2 — 데디 실검증 (스테이징 서버 exe + 원격 클라):** PIE가 아니라 `Project_REServer.exe` + `UnrealEditor-Cmd 127.0.0.1:7777`로 검증했다. `IsRunningDedicatedServer()`가 빌드 타깃 기준이라 PIE로는 데디 분기를 재현할 수 없기 때문이다.
  - 클라 로그 `[Dash] anim len=0.97 (role=ROLE_AutonomousProxy)` — Multicast 도달 확인
  - **서버 로그 `[Dash] anim len=` — 초안 가정(데디는 AnimInstance null이라 no-op)은 반증됐다.** 서버에 `role=ROLE_Authority`로 2회 찍혔다 → `IgnoreRootMotion` 상태로 서버가 몽타주를 돌려 권위 이동에 개입 중이었다. `IsNetMode(NM_DedicatedServer)` 명시 가드를 추가(`ff3facf`)했고 재검증에서 **0회**로 떨어졌다.
  - **클라 크래시 발견 → 수정.** 복귀 타이머 람다의 raw `UAnimInstance*` 캡처가 월드 정리 중 파괴된 객체를 역참조해 `UObjectArray.h:1083` assert. `TWeakObjectPtr`로 전환(`ff3facf`), 재현 없음.
  - 서버 대쉬 거리 `[Dash] dist=596.0` — 가드 추가 후에도 유지

- [ ] **Step 3 — 리슨/싱글 회귀:** 모션이 **1회만** 재생되고 대쉬 거리가 기존과 동일.

- [ ] **Step 4 — 지연 실측:** 클라 로그의 `[Dash] input sent (local)` 과 `[Dash] anim len=` 타임스탬프 차를 5회 측정해 중앙값 기록. 이 수치로 `LocalPredicted` 전환 필요 여부를 결론내고 이슈 #75에 코멘트로 남긴다. 전환이 필요하다고 나오면 **별도 이슈로 제안** (쿨다운 커밋·예측키 처리가 얽혀 스코프가 크다).

**검증 통과 기준:** 대쉬 거리 회귀 없음 + 데디 클라에서 모션 표시 + 리슨 1회 재생 + 지연 실측치 기록.

---

## 실패 시 롤백

`83ac7c9` 되돌리면 원상복구. 애셋 소유가 어빌리티로 돌아가고 데디에서만 모션이 안 보이는 기존 상태가 된다.

## 머지 주의

`#74`(발사 몽타주)가 `RECharacterBase.h/.cpp`의 **인접 구간**을 수정한다. 두 브랜치를 `dev`로 합칠 때 충돌한다 — 먼저 머지된 쪽 기준으로 나머지를 rebase하고, 두 RPC 선언(`Multicast_PlayFireMontage` / `Multicast_PlayDashMontage`)이 **둘 다** 남았는지 확인할 것.
