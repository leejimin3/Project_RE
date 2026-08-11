# 오너 클라 캐릭터 회전 Implementation Plan

> 구현 + PIE 검증 완료. 완료 스텝은 `[x]`, 남은 것은 `[ ]`.

**Goal:** 데디에서 오너 클라의 캐릭터 회전이 화면에 반영되게 한다(#79). 겸사겸사 대쉬 중 발사를 차단한다(#80).

**Architecture:** 오너 클라에서 `bOrientRotationToMovement`를 끄고 `AREPlayerController::PlayerTick`이 회전을 직접 구동한다. 끄는 순간 서버 보정의 회전 복원 분기 조건도 불성립해진다. 발사 시에는 커서 yaw를 `AttackInterval` 동안 **매 틱 재적용**한다. 서버 경로 무변경.

**Tech Stack:** UE 5.8, C++ (CharacterMovementComponent, GAS 태그 조회).

## Global Constraints

- 브랜치: `feature/M4-owner-rotation`
- 빌드 게이트:
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat" `
    Project_REEditor Win64 Development `
    -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
- **에디터가 켜져 있으면 빌드가 실패한다** — `Unable to build while Live Coding is active`. 헤더를 건드리는 변경은 Live Coding 패치로 안 붙으므로 에디터를 닫고 빌드해야 한다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

---

### Task 1: 오너 클라 회전 구동

**Files:** `Source/Project_RE/Core/REPlayerController.h`, `.cpp`

- [x] **Step 1:** `PlayerTick` 오버라이드 선언 + `FacingLockUntil`(double) / `FacingLockYaw`(float) / `FacingPawn`(TWeakObjectPtr) 멤버 추가.
- [x] **Step 2:** `PlayerTick` 구현 — `HasAuthority()`면 즉시 반환(서버 무변경).
- [x] **Step 3:** 폰이 새로 잡히면 1회 `bOrientRotationToMovement = false`.
- [x] **Step 4:** 속도가 있으면 `Velocity` 방향으로 `RotationRate.Yaw` 보간 회전. 정지 시 현재 회전 유지.
- [x] **검증:** 빌드 통과.

---

### Task 2: 발사 페이싱 락

**Files:** `Source/Project_RE/Core/REPlayerController.cpp`

- [x] **Step 1:** `OnFire`에서 커서 회전 적용 후 `FacingLockYaw` / `FacingLockUntil = Now + AttackInterval` 세팅.
- [x] **Step 2:** ⚠ **락 구간에 매 틱 커서 yaw를 다시 세운다.** 초안은 조기 반환만 해서 보정이 되돌린 회전을 복구하지 못했고, 그게 간헐 증상의 원인이었다.
- [x] **Step 3:** 2° 이상 어긋나면 `[Facing] reverted` 진단 로그. 검증 후 `Verbose`로 낮춰 기본 출력에서 제외.
- [x] **검증:** PIE 4케이스 통과 (아래 Task 4).

---

### Task 3: 대쉬 중 발사 차단 (#80)

**Files:** `Source/Project_RE/Core/REPlayerController.cpp`

- [x] **Step 1:** `Server_RequestFire_Implementation`에서 ASC가 `State.Dashing`을 보유하면 조기 반환 + `[Attack] blocked: dashing` 로그.
- [x] **Step 2:** 클라 조기 반환은 **넣지 않는다** — `ServerOnly` 어빌리티의 `ActivationOwnedTags`는 loose 태그라 클라 복제 보장이 없다.
- [x] **검증:** PIE 로그에 차단/해제 시퀀스 확인 (아래 Task 4).

---

### Task 4: 검증

- [x] **헤드리스 회귀:** `[Dash] dist=679.4`(기존 678.3), `[Move] probe dist=737.9`(기존 736.9), 판정 로그 동일.
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
    "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -nullrhi -unattended -log
  ```
  단 헤드리스는 `HasAuthority()`라 회전 코드가 아예 실행되지 않고, 대쉬·발사가 겹치지 않아 차단도 타지 않는다. **"안 깨졌다"만 말해준다.**

- [x] **PIE 회전 4케이스** (`Play As Client` + 데디 서버): 이동 방향 회전 / 우클릭 홀드+좌클릭 / 단발 이동 중 좌클릭 / 정지 단발 — 전부 통과.
- [x] **PIE 대쉬 차단:** `blocked: dashing` 13회 / 대쉬 17회, `hit boss` 12회, 대쉬 종료 후 발사 정상(과차단 없음).
  - **재현 조작: 좌클릭을 홀드한 채 스페이스.** 대쉬 0.2s < 클라 페이싱 0.25s라 새로 클릭하면 서버까지 도달하지 못한다 — 첫 시도에서 `blocked` 0회가 나온 원인이다.

- [x] **리슨/싱글 회귀:** 싱글은 헤드리스 프로브로, 리슨은 PIE `Play As Listen Server`로 확인 — 회전·발사·대쉬 모두 종전대로. `PlayerTick`이 `HasAuthority()`에서 즉시 반환하므로 서버 경로에 개입하지 않는다는 설계가 실측으로 확인됐다.

---

## 남은 것

- 근본 해법은 아니다 — 보정은 여전히 회전을 덮고 매 틱 덮어쓰기로 이긴다. 회전이 게임플레이 판정에 쓰이면 회전 복제 또는 `ShouldCorrectRotation` 오버라이드가 필요하다.
