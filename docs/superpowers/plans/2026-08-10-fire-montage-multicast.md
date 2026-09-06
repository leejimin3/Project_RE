# 발사 몽타주 Multicast Implementation Plan

> 사후 정리 문서. 구현은 `7f632ed`에 이미 들어갔다 — 구현 스텝은 `[x]`, **남은 검증 스텝은 `[ ]`** 로 표기한다. 검증은 코디네이터(본체 worktree)가 수행한다.

**Goal:** 데디케이티드 서버에서 원격 클라가 자기 발사 모션을 보게 한다. 판정·데미지·rate limit은 건드리지 않는다.

**Architecture:** 코스메틱 재생을 `ARECharacterBase::Multicast_PlayFireMontage`(NetMulticast, Unreliable)로 옮긴다. `UREAttackComponent`는 복제 설정이 없으므로 RPC를 컴포넌트가 아니라 이미 복제 액터인 캐릭터에 둔다. 컴포넌트의 직접 재생은 삭제해 재생 경로를 하나로 만든다(이중 재생 구조적 차단). 데디 서버는 `IsNetMode(NM_DedicatedServer)`로 재생을 생략한다.

**Tech Stack:** UE 5.8, C++ (NetMulticast RPC, AnimInstance 몽타주 재생).

## Global Constraints

- 브랜치: `leejimin3/M4-fire-multicast` (worktree `C:\Users\leeji\orca\workspaces\Project_RE\M4-fire-multicast`)
- 빌드 타깃: `Project_REEditor Win64 Development`
- **빌드 게이트 — `-Project`는 반드시 이 worktree의 `.uproject`를 가리킨다.** 본체 절대경로를 넣으면 캐시로 10초 만에 "성공"하고 실제 변경은 검증되지 않는다:
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat" `
    Project_REEditor Win64 Development `
    -Project="C:\Users\leeji\orca\workspaces\Project_RE\M4-fire-multicast\Project_RE.uproject" `
    -WaitMutex -NoHotReload
  ```
- 디스크 여유가 46GB뿐이라 **worktree 병렬 빌드 금지.** 빌드는 코디네이터가 직렬로 돌린다.
- 유닛테스트 하니스 없음 — 태스크 검증 = 빌드 게이트, 통합 검증 = PIE/데디 로그 대조.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

---

### Task 1: 캐릭터에 Multicast RPC 추가

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`, `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Produces: `ARECharacterBase::Multicast_PlayFireMontage(UAnimMontage*)` — Task 2의 컴포넌트가 호출.

- [x] **Step 1: 선언 추가** — `RECharacterBase.h` public 구간, `GetPendingDashDir()` 아래. `UFUNCTION(NetMulticast, Unreliable)`. 배치 근거(컴포넌트 비복제)와 신뢰성 근거(Reliable이면 연사가 신뢰 큐 점유)를 주석으로 남긴다. `class UAnimMontage;` 전방선언 추가.

- [x] **Step 2: 구현 추가** — `RECharacterBase.cpp`, `TryDash` 아래:
  ```cpp
  void ARECharacterBase::Multicast_PlayFireMontage_Implementation(UAnimMontage* Montage)
  {
      if (!Montage || IsNetMode(NM_DedicatedServer)) { return; }
      if (UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
      {
          const float Len = AnimInst->Montage_Play(Montage, 1.0f);
          UE_LOG(LogTemp, Log, TEXT("[Attack] fire montage len=%.2f"), Len);
      }
  }
  ```
  `#include "Animation/AnimMontage.h"` 추가.

- [ ] **검증:** 빌드 게이트 통과.

---

### Task 2: 컴포넌트 직접 재생 제거

**Files:**
- Modify: `Source/Project_RE/Core/REAttackComponent.cpp`, `Source/Project_RE/Core/REAttackComponent.h`

**Interfaces:**
- Consumes: Task 1의 `Multicast_PlayFireMontage`.

- [x] **Step 1:** `FireInDirection`의 몽타주 블록을 `Cast<ARECharacterBase>(GetOwner())->Multicast_PlayFireMontage(FireMontage)` 호출로 교체. `ACharacter` 캐스트 → `ARECharacterBase` 캐스트로 변경.
- [x] **Step 2:** 불필요해진 include 제거 (`GameFramework/Character.h`, `Components/SkeletalMeshComponent.h`, `Animation/AnimInstance.h`), `RECharacterBase.h` 추가.
- [x] **Step 3:** `FireMontage` 주석을 새 재생 경로로 갱신.
- [x] **Step 4:** 판정 블록(히트스캔 / `Boss->TakeDamage` / rate limit / `DrawDebugLine`) **무변경** 확인.

- [ ] **검증:** 빌드 게이트 통과 + `git diff dev...HEAD -- Source/Project_RE/Core/REAttackComponent.cpp` 에 판정 라인 변경이 없음을 눈으로 확인.

---

### Task 3: 3경로 런타임 검증 (코디네이터)

**Files:** 없음 (실행 검증)

- [ ] **Step 1 — 데디 PIE:** Play 드롭다운 → Net Mode `Play As Client` + `Run Dedicated Server`, 클라 1개.
  - 클라 창에서 좌클릭 발사 → **캐릭터가 발사 모션을 재생하는가** (핵심 통과 조건)
  - 클라 로그에 `[Attack] fire montage len=` 1회/발
  - **서버 로그에는 이 줄이 없어야 정상** (데디 생략 가드). 있으면 가드가 안 걸린 것
  - 서버 로그에 `[Attack] hit boss, applied=` / `[Attack] miss (blocked=` 는 그대로 나와야 함 (판정 회귀 없음)

- [ ] **Step 2 — 리슨서버:** Net Mode `Play As Listen Server`.
  - 호스트 화면에서 발사 모션 재생
  - `[Attack] fire montage len=` 이 **발당 정확히 1회** (2회면 이중 재생 회귀)

- [ ] **Step 3 — 싱글(Standalone):** 모션 + 판정 로그 모두 기존과 동일.

- [ ] **Step 4 — rate limit 회귀:** 좌클릭 홀드 연사 시 `[Attack] rate-limited (dt=` 가 기존과 같은 빈도로 나오는지.

**검증 통과 기준:** 데디 클라에서 모션이 보이고, 리슨에서 1회만 재생되며, 판정 로그 3종이 전부 회귀 없음.

---

## 실패 시 롤백

`7f632ed` 되돌리면 원상복구. 컴포넌트 직접 재생으로 돌아가며 데디에서만 모션이 안 보이는 기존 상태가 된다.

## 머지 주의

`#75`(대쉬 몽타주)가 `RECharacterBase.h/.cpp`의 **인접 구간**을 수정한다. 두 브랜치를 `dev`로 합칠 때 충돌한다 — 먼저 머지된 쪽 기준으로 나머지 하나를 rebase하고, 두 RPC 선언이 모두 남았는지 확인할 것.
