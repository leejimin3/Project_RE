# M2 #40 승패 상태 + 결과 화면 Design

**이슈:** #40 (M2: 플레이어 게임루프)
**날짜:** 2026-07-14
**선행:** #28 (보스 HP TakeDamage), #34 (탄막→플레이어 피격), #29 (HP바 위젯) 완료

## 목표

게임루프를 닫는다. 플레이어 HP 0 → **패배**, 보스 HP 0 → **승리**. 판정은 서버 권위, 결과는 화면에 한 줄, 그리고 모든 게 멈춘다.

리스폰/재시작/상태기계는 범위 밖.

## 배경 (현재 상태, 소스 대조 완료)

**플레이어 사망 — 아무 일도 안 일어난다.**
`Core/RECharacterBase.cpp:92-96` — `Health`를 0으로 클램프하고 `OnRep_Health()`로 HP바만 비운다. 그 자리에 `// TODO M5: ... 사망 처리` 주석만 있다. 이 라벨은 틀렸다 — 플레이어 사망은 M5(협동 멀티) 기능이 아니라 M2에서 닫았어야 할 게임루프다. HP가 0이어도 계속 플레이되고 탄막도 계속 날아온다.

**보스 사망 — 발사만 멈춘다.**
`Core/REBossCharacter.cpp:82-86` — `bIsDead = true` 세우고 로그 한 줄. `TriggerBulletPattern` 진입 가드(`REBossCharacter.cpp:24`)가 이 플래그를 읽어 탄막만 멈춘다. 승리 판정도, 화면 표시도 없다.

**멈춰야 할 것이 3개, 서로 다른 액터에 흩어져 있다.**

| 멈출 것 | 소유자 |
|---|---|
| 탄막 발사 (0.1초 루프) | `AREGameMode::DemoFireTimer` (`REGameMode.cpp:66`) |
| 자동사격 | `UREAutoFireComponent::FireTimer` (private, 정지 API 없음) |
| 플레이어 입력 (이동/대쉬) | `AREPlayerController` |

## 설계 결정 (사전 확정)

### 1. 오케스트레이션은 GameMode

멈출 대상 3개가 서로 다른 액터에 있다. 이걸 전부 아는 유일한 지점이 GameMode다. 캐릭터가 서로를 뒤지게 하면 결합만 는다.

`AREGameMode::EndGame(bool bVictory)` 신설. `AGameModeBase`는 서버에만 존재 → **새 권위 경로를 만들지 않는다**. 판정 호출부는 양쪽 `TakeDamage`의 **기존** `HasAuthority()` 가드 안.

대안으로 검토했다가 버린 것:
- **PlayerController 오케스트레이션** — 결과화면·입력차단은 PC 소유물이라 자연스럽지만, PC가 GameMode의 private `DemoFireTimer`를 못 끈다. GameMode에 어차피 stop API를 뚫어야 하니 파일만 하나 더 건드리고 이득 없음.
- **`SetGamePaused(true)`** — 정지 3개가 1줄로 끝나 제일 짧다. 하지만 네트워크 게임에서 무력화되므로 M4 데디 전환 때 통째로 버려야 하고, 이슈의 "서버 권위 판정" 요구와 결이 다르다.

### 2. 결과 화면은 Client RPC 경유

`AREPlayerController::Client_ShowResult(bool bVictory)` — `UFUNCTION(Client, Reliable)`. 싱글에선 로컬 호출로 똑같이 동작하고, M4 데디 전환 시 수정 0줄. GameMode가 직접 `AddToViewport`하면 데디에서 서버가 위젯을 띄우려다 no-op → 클라 무화면.

입력 차단(`DisableInput`)도 여기서 한다. 입력은 원래 클라 소유물이다.

### 3. 뜬 탄막은 그대로 날아간다

새 발사만 중지(`DemoFireTimer` Clear). 이미 스폰된 Mass 엔티티는 `Lifetime` 다할 때까지 이동·소멸한다. 일괄 소멸(`ClearAll`) 경로는 추가하지 않는다 — 결과 화면 뜬 뒤 몇 초 더 날아다니는 건 감수한다.

### 4. 사망 플래그는 보스 기존 패턴 재사용

`ARECharacterBase`에 `bool bIsDead = false` (서버 전용, **비복제** — 클라 시각처리는 범위 밖). 보스의 기존 플래그와 동일. 중복 발화 방지용.

## 변경 범위

**신규 2개 + 수정 5개:**

| 파일 | 변경 |
|---|---|
| `Source/Project_RE/UI/REResultWidget.h` | 신규 — 결과 위젯 선언 |
| `Source/Project_RE/UI/REResultWidget.cpp` | 신규 — `WidgetTree` 텍스트 루트 |
| `Core/REGameMode.h/.cpp` | `EndGame(bool)` + `bGameOver` 가드 |
| `Core/RECharacterBase.h/.cpp` | `bIsDead` + 패배 판정, 잘못된 `TODO M5` 주석 제거 |
| `Core/REBossCharacter.cpp` | 기존 `bIsDead` 블록에 `EndGame(true)` 한 줄 |
| `Core/REAutoFireComponent.h/.cpp` | public `StopFiring()` |
| `Core/REPlayerController.h/.cpp` | `Client_ShowResult` Client RPC |

`Build.cs` / `.uproject` 변경 없음 — `UMG`·`Slate` 모듈과 `Project_RE/UI` include 경로는 #29에서 이미 들어갔다.

## 컴포넌트

### 1. `UREResultWidget` (신규)

`UREHealthBarWidget` 패턴 그대로 — BP 자산 없이 `Initialize()`에서 `WidgetTree->ConstructWidget<UTextBlock>`으로 루트 생성. `NativeConstruct`는 슬레이트 트리 구축 후라 늦다.

HP바와 다른 점: **월드스페이스가 아니라 화면공간**. `UWidgetComponent`에 붙지 않고 `AddToViewport()`로 뷰포트에 직접 올린다.

- `SetResult(bool bVictory)` — 텍스트 `"VICTORY"`(초록) / `"DEFEAT"`(빨강), 화면 중앙 정렬, 큰 폰트

### 2. `AREGameMode::EndGame(bool bVictory)` (신설)

```
if (bGameOver) return;      // 같은 프레임 양쪽 사망 → 선착순
bGameOver = true;

DemoFireTimer Clear                              // 탄막 발사 중지
PlayerPawn->AutoFireComponent->StopFiring()      // 자동사격 중지
PC->Client_ShowResult(bVictory)                  // 결과 화면 + 입력 차단
```

### 3. `UREAutoFireComponent::StopFiring()` (신설, public)

`FireTimer` Clear. 3줄.

> 지금 자동사격은 입력 기반이 아니라 **서버 타이머**라 `DisableInput`으로 안 멈춘다. 그래서 명시적 정지가 필요하다. 향후 좌클릭 공격(별도 이슈, #26 재작업)으로 갈아끼우면 `EndGame`의 이 한 줄은 지워도 된다 — 입력 차단이 커버한다.

### 4. `AREPlayerController::Client_ShowResult(bool bVictory)` (신설)

`CreateWidget<UREResultWidget>(this)` → `SetResult(bVictory)` → `AddToViewport()` → `DisableInput(this)`.

## 데이터 흐름

```
[패배] 탄막 히트 → ARECharacterBase::TakeDamage (HasAuthority 가드 안)
                 → Health 0 클램프 → OnRep_Health() → HP바 0
                 → Health<=0 && !bIsDead → bIsDead=true → GameMode->EndGame(false)

[승리] 자동사격 → AREBossCharacter::TakeDamage (HasAuthority 가드 안)
                → Health 0 클램프 → OnRep_Health() → HP바 0
                → 기존 bIsDead 블록 → GameMode->EndGame(true)

EndGame → DemoFireTimer Clear + AutoFire StopFiring + PC->Client_ShowResult
                                                      → 위젯 AddToViewport + DisableInput
```

사망 후에도 뜬 탄환이 플레이어를 계속 때리지만, `bIsDead` 가드가 `EndGame` 재진입을 막는다.

## 검증

1. 빌드 통과
2. **headless 프로브 (`-game -nullrhi`)** — 승리 경로: 자동사격 10dmg/0.25s → 보스 100HP → ~2.5초에 사망. `[RE] Boss died` + `EndGame(bVictory=1)` + 타이머 정지 로그 관측. `-nullrhi`에선 위젯이 안 그려지므로 판정·정지만 로그로.
3. **패배 경로 프로브** — ⚠️ 현재 밸런스로는 자동사격이 보스를 먼저 녹여서 **DEFEAT가 자연 발생하지 않는다.** 프로브 시 보스 `MaxHealth`를 일시적으로 크게 올려 돌린 뒤 되돌린다. **이 수정은 커밋하지 않는다** (프로브 전용 로컬 변경).
4. **실 RHI 스크린샷 (`-windowed` + `FScreenshotRequest`)** — 위젯 렌더는 로그로 검증 불가(#29에서 배움). VICTORY / DEFEAT 화면 각 1장.
5. PIE 육안 최종 1회

## 스코프 제외 (의도적)

- 재시작 / 리스폰 — 필요해지면 별도 이슈
- `AREGameState` 상태기계 (Waiting/Playing/Won/Lost) — 상태 4개뿐이라 지금은 과함. M4 데디 전환 시 재검토
- 사망 연출 (페이드/이펙트/보스 액터 파괴) — M6 폴리싱
- 좌클릭 공격 (로스트아크식 이동중단+발사) — #26 재작업, 별도 이슈
