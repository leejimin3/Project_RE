# HP 리플리케이션 / 체력바 클라 갱신 Implementation Plan

> 사후 정리 문서. 검토는 `d29c83e`에 반영됐다 — 완료 스텝은 `[x]`, **남은 실행 검증은 `[ ]`** 로 표기한다. 검증은 코디네이터(본체 worktree)가 수행한다.

**Goal:** 데디에서 원격 클라 체력바가 서버 HP를 따라간다는 것을 근거와 함께 확정한다. 문제 없는 곳을 투기적으로 고치지 않는다.

**Architecture:** 기존 서버 권위 경로(`HasAuthority` 차감 → `DOREPLIFETIME(Health)` → `OnRep_Health` → `HealthBar`)를 그대로 둔다. 이 작업의 산출물은 **코드가 아니라 판정과 근거**다.

**Tech Stack:** UE 5.8, C++ (프로퍼티 복제, UMG WidgetComponent).

## Global Constraints

- 브랜치: `leejimin3/M4-hp-repl` (worktree `C:\Users\leeji\orca\workspaces\Project_RE\M4-hp-repl`)
- **빌드 게이트 — `-Project`는 반드시 이 worktree의 `.uproject`.** 본체 절대경로를 넣으면 캐시로 10초 만에 "성공"하고 실제 변경은 검증되지 않는다:
  ```powershell
  & "E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat" `
    Project_REEditor Win64 Development `
    -Project="C:\Users\leeji\orca\workspaces\Project_RE\M4-hp-repl\Project_RE.uproject" `
    -WaitMutex -NoHotReload
  ```
- 디스크 여유 46GB — worktree 병렬 빌드 금지.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

---

### Task 1: 서버 → 클라 경로 완주 검토

**Files:** 없음 (읽기 전용 검토)

- [x] **Step 1:** 데미지 진입점부터 체력바까지 6단계 추적 — 히트 프로세서 `ExecutionFlags`, `HasAuthority` 차감, `DOREPLIFETIME`, `OnRep_Health`, `SetHealthPercent`, `CachedPercent` 타이밍. 결과를 설계 문서 표로 기록.
- [x] **Step 2:** `Health` 소비자 전수 조사 — 체력바가 유일함을 확인.

- [x] **검증:** 끊긴 고리 없음. 기능 코드 수정 불필요 판정.

---

### Task 2: 주의 지점 3건 판정

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`, `Source/Project_RE/Core/REBossCharacter.h` (주석만)

- [x] **Step 1 — `MaxHealth` 비복제:** 쓰기 지점 전수 조사(생성자 2곳뿐, 런타임 변경 없음) → **무변경**. 헤더 주석에 근거 1줄씩.
- [x] **Step 2 — `bIsDead` 비복제:** 소비자 4곳이 전부 서버 전용임을 확인. 보스 페이즈 루프 진입점이 `REGameMode.cpp:80`(서버 전용 액터)이라 클라에서 체인이 시작되지 않음 → **무변경**.
- [x] **Step 3 — 데디 `HealthBar`:** 엔진 `UWidgetComponent`가 `IsRunningDedicatedServer()`로 이미 스킵(`InitWidget:1748` 등) → Cast 실패로 자연 no-op → **무변경**.
- [x] **Step 4:** 각 판정에 **재검토 트리거**를 명시 (판정 근거가 사라지는 조건).

- [ ] **검증:** 빌드 게이트 통과 (주석만 바뀌었으므로 형식 확인 수준).

---

### Task 3: 런타임 검증 (코디네이터)

**Files:** 없음 (실행 검증)

절차 본문은 `docs/guides/dedicated-server.md` "HP 리플리케이션 검증 절차" 참조. 체크리스트만 여기 둔다.

- [ ] **Step 1 — 데디 PIE 기동:** `Play As Client` + `Run Dedicated Server`, Players 1. 뜨는 창은 클라 하나 → 보이는 체력바가 곧 클라 복제 결과다.
- [ ] **Step 2 — 플레이어 체력바:** 보스 탄막 피격 시 초록 바 감소. 피격 전/후 스크린샷 2장.
- [ ] **Step 3 — 보스 체력바:** 좌클릭 홀드로 보스 피격 시 빨간 바 감소.
- [ ] **Step 4 — 수치 근거:** 클라 콘솔에서 `DisplayAll RECharacterBase Health` / `DisplayAll REBossCharacter Health`. 복제된 값이 실시간 감소하면 경로 확정 (육안 판정 대체).
- [ ] **Step 5 — 결과 위젯 도달:** HP 0까지 진행 후
  ```powershell
  Select-String -Path "Saved\Logs\Project_RE.log" -Pattern "\[RE\] (EndGame|Client_ShowResult|Player died|Boss died)"
  ```
  `[RE] EndGame: DEFEAT`(서버) → `[RE] Client_ShowResult: DEFEAT`(클라) 순서 확인. `Client_ShowResult`가 `UFUNCTION(Client, Reliable)`(`REPlayerController.h:29`)이라 이 로그가 곧 클라 도달 증거.
- [ ] **Step 6 — 싱글 회귀:** `Standalone`으로 재실행해 체력바/결과 위젯이 종전대로.
- [ ] **Step 7 — 위젯 스킵 실측(스테이징 필요):** PIE로는 불가(`IsRunningDedicatedServer()`가 에디터에서 항상 false). 스테이징 `Project_REServer.exe` 기동 후 서버 로그에 위젯 관련 경고가 없는지 확인. **#76(standalone 통합 회귀)에 묶어 처리해도 된다.**

**검증 통과 기준:** Step 2~5 전부 통과 + Step 6 회귀 없음. Step 7은 #76으로 이월 가능.

---

## 실패 시

경로 어딘가가 실제로 끊겨 있다면 이 계획의 판정이 틀린 것이다. 그 경우 Task 1의 6단계 표에서 **어느 단계에서 값이 멈췄는지** 부터 특정한다 — `DisplayAll`이 서버/클라 어느 쪽에서 안 움직이는지가 1차 분기점이다.
