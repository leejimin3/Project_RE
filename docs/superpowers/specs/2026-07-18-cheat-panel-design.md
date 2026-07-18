# 치트 패널 (SRDebugger-lite) — 설계

날짜: 2026-07-18
상태: 승인 대기

## 목적

인게임 치트 패널. Unity의 SRDebugger처럼 런타임 토글 UI. 지금은 토글 목록 1개 —
첫 치트는 **플레이어 무적**. 이후 치트는 패널에 줄 추가로 확장.

범위 결정(브레인스토밍):
- 패널 구조: **토글 목록만** (카테고리 탭/자동등록 프레임워크 아님 — YAGNI)
- 상태 백엔드: **CVar** (`re.Profiling.*` 기존 패턴 재사용, 콘솔 토글 무료)
- 열기 키: **F1**
- 검증: **PIE 육안만** (헤드리스 assert 없음 — 치트는 데브 도구)

## 비범위

- 보스 패턴 "바로 다음 재출현 방지"(#원요청 1번)는 **별도 태스크** — 이 스펙 밖.
- 카테고리 탭, 콘솔 뷰, 프로파일러, 자동 등록 레지스트리.
- 데디케이티드 서버 지원(아래 천장 참조).

## 컴포넌트

### 1. CVar `re.Cheat.PlayerInvincible`
- `TAutoConsoleVariable<int32>`, 기본값 `0`.
- 선언 위치: `RECharacterBase.cpp` 파일 스코프(읽는 곳과 동봉).
- 콘솔에서도 `re.Cheat.PlayerInvincible 1` 로 토글 가능.

### 2. `RECharacterBase::TakeDamage` 가드
- 기존 `HasAuthority()` 가드 **직후**:
  ```cpp
  // ponytail: CVar는 클라 로컬 — PIE/단일프로세스에서만 유효, 실 데디 서버 미지원(데브 치트)
  static IConsoleVariable* Inv =
      IConsoleManager::Get().FindConsoleVariable(TEXT("re.Cheat.PlayerInvincible"));
  if (Inv && Inv->GetInt() != 0)
  {
      return 0.f;
  }
  ```
- early return이라 데미지 적용·사망 판정 모두 스킵.

### 3. `URECheatPanelWidget` (`Source/Project_RE/UI/`)
- 순수 C++ `UUserWidget`. `Initialize()` 오버라이드에서 위젯트리 구성
  (`REResultWidget` 동일 idiom: `WidgetTree->ConstructWidget`, CDO 가드).
- 트리: `UBorder`(반투명 배경) → `UVerticalBox`
  - 타이틀 `UTextBlock` "CHEATS"
  - 치트 행 1개: 수평 박스 = `UCheckBox` + `UTextBlock` "Player Invincible"
- 체크박스 초기 상태 = CVar 현재값. `OnCheckStateChanged` → CVar `Set(체크?1:0)`.
- 향후 치트 추가 = 행 빌드 블록 복사 + 해당 CVar 배선.

### 4. 토글 입력 (`REPlayerController`)
- 코드생성 `IA_CheatPanel`(Boolean), 기존 `TopDownMappingContext`에 `EKeys::F1` 매핑.
- `BindAction(..., ETriggerEvent::Started, ...)` → `OnToggleCheatPanel()`.
- `OnToggleCheatPanel`: 패널 위젯 1회 생성(멤버 캐시), 이후 `Visible`/`Collapsed` 토글.
- 오버레이 방식 — 게임 정지 안 함. 커서는 이미 표시(탑뷰 `bShowMouseCursor=true`)라
  체크박스 클릭 가능. 입력모드 UI-only 전환 안 함(게임 입력 유지).

## 데이터 흐름

```
F1 → OnToggleCheatPanel → 패널 표시
체크박스 클릭 → OnCheckStateChanged → CVar.Set(1)
보스 탄 명중 → RECharacterBase::TakeDamage → CVar!=0 → return 0 (무피해)
```

## 천장 (ponytail)

CVar는 클라이언트 로컬, `TakeDamage`는 서버 권위. PIE/단일 프로세스(리슨 서버)에서는
클라 CVar == 서버 CVar(동일 프로세스)라 동작. 실 데디케이티드 서버에서는 클라 CVar가
서버에 도달 안 함 → 무적 미적용. 데브 치트 용도라 허용. `TakeDamage` 가드에 주석으로 명시.

## 검증

PIE 실행 → F1 → Player Invincible 체크 → 보스 탄막에 서 있기 → HP 100 유지 확인.
(자동 헤드리스 assert 없음.)

## 파일 변경 요약

| 파일 | 변경 |
|------|------|
| `UI/RECheatPanelWidget.h/.cpp` | 신규 — 패널 위젯 |
| `Core/RECharacterBase.cpp` | CVar 선언 + `TakeDamage` 무적 가드 |
| `Core/REPlayerController.h/.cpp` | `IA_CheatPanel` + F1 매핑 + 토글 핸들러 + 패널 캐시 멤버 |
