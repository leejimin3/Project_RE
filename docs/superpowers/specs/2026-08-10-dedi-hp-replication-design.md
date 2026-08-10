# HP 리플리케이션 / 체력바 클라 갱신 (데디) — 설계

날짜: 2026-08-10
상태: 검토 완료 · **기능 코드 무변경** (빌드/PIE 검증 대기)
이슈: #73 (M4)

## 목적

데디케이티드 서버에서 **원격 클라의 체력바가 서버 HP 변화를 따라가는지** 확정한다.

코드는 M0부터 서버 권위로 짜여 있었지만, 리슨서버에서 화면이 갱신되던 경로는 `TakeDamage`가 `OnRep_Health()`를 **직접 호출**하는 경로였다. 그건 원격 클라가 타는 경로(복제 → `OnRep`)와 다르다. 그래서 "된다"가 검증된 적이 없다.

## 결론

**기능 코드를 한 줄도 고치지 않는다.** 서버→클라 경로에 끊긴 고리가 없고, 이슈가 지목한 잠재 문제 3건은 전부 실제 문제가 아니었다. 변경분은 헤더 주석 2줄(근거 보존) + 문서뿐이다.

## 코드 경로 (소스 대조)

| 단계 | 위치 |
|---|---|
| 데미지 판정(서버 전용) | `Mass/REBulletHitProcessor.cpp:30`, `Mass/REArcHitProcessor.cpp:17` — `ExecutionFlags = Standalone\|Server` |
| Health 차감(서버 권위) | `Core/RECharacterBase.cpp:98-111`, `Core/REBossCharacter.cpp:350-358` — `HasAuthority()` 가드 후 차감 |
| 복제 등록 | `RECharacterBase.cpp:130`, `REBossCharacter.cpp:376` — `DOREPLIFETIME(..., Health)`. 액터 복제는 `APawn` 기본값(`Pawn.cpp:86` `bReplicates = true`) |
| 클라 수신 | `ReplicatedUsing = OnRep_Health` → `HealthBar->SetHealthPercent()` (`RECharacterBase.cpp:177-183`, `REBossCharacter.cpp:379-385`) |
| 서버/싱글 | `TakeDamage`가 `OnRep_Health()`를 직접 호출 — 리슨/싱글도 같은 갱신 로직을 타므로 회귀 위험 없음 |
| 위젯 생성 타이밍 | `UI/REHealthBarComponent.cpp:34-41` — 위젯 생성 전 도착한 percent는 `CachedPercent`에 저장 후 `InitWidget`에서 반영 |

`Health`를 읽는 곳은 체력바가 유일하다. 다른 소비자가 없으므로 복제 값의 소비 지점이 하나로 좁다.

## 판정 — 이슈 지목 3건 전부 무변경

### 1. `MaxHealth` 비복제 — 문제 아님

`DOREPLIFETIME`에 `Health`만 있고 `MaxHealth`는 없다. 클라의 `MaxHealth`는 생성자가 읽은 값이다.

쓰기는 **두 생성자뿐**(`RECharacterBase.cpp:38`, `REBossCharacter.cpp:45`)이고 출처는 `UREStatsSettings`(`Config/DefaultGame.ini`) 단일. 런타임에 바꾸는 코드가 전무하므로 서버/클라 값이 갈릴 경로가 없다. 복제 추가는 YAGNI.

**재검토 트리거:** 런타임에 `MaxHealth`를 바꾸는 기능(버프/난이도/레벨업)이 생기는 순간 이 판정은 무효다.

### 2. `bIsDead` 비복제 — 문제 아님

소비자가 전부 서버 전용이다:

- `RECharacterBase.cpp:114` — `EndGame` 재진입 차단 (`HasAuthority` 가드 안)
- `REBossCharacter.cpp:150` — `FireArtillery` 가드
- `REBossCharacter.cpp:228` — `TriggerBulletPattern` 가드

보스 발사 루프의 진입점은 `REGameMode.cpp:80`의 `Boss->StartFiring()` 하나다. **GameMode는 서버에만 존재하므로** `BeginPhase` → `PhaseTimer` 자기재진입 체인이 클라에서 아예 시작되지 않는다. 클라가 `bIsDead`를 읽는 지점이 0개.

**재검토 트리거:** 사망 시각 처리(모델 숨김/래그돌/사망 UI)를 넣으면 클라가 사망 여부를 알아야 한다.

### 3. 데디 서버 `HealthBar` 위젯컴포넌트 — 문제 아님

엔진이 이미 데디를 스킵한다. `UWidgetComponent`의 `IsRunningDedicatedServer()` 가드 지점 (`Engine/Source/Runtime/UMG/Private/Components/WidgetComponent.cpp`):

- `InitWidget` 1748 — 첫 줄에서 `SetTickMode(Disabled)` 후 `return`
- `OnRegister` 960, `TickComponent` 1245/1253, `RemoveWidgetFromScreen` 1553

위젯이 생성되지 않으므로 `REHealthBarComponent::InitWidget` / `SetHealthPercent`의 `Cast<UREHealthBarWidget>(GetUserWidgetObject())`가 null → **자연 no-op**(경고·크래시 없음). `REBulletRenderSubsystem`식 `NM_DedicatedServer` 조기 반환을 덧붙일 이유가 없다.

## 천장 (ponytail)

- **무적 치트는 데디에서 안 먹는다.** `re.Cheat.PlayerInvincible`은 클라 로컬 CVar이고 `TakeDamage`는 서버에서 돈다 (`RECharacterBase.cpp:103` 주석에 이미 명시). 데브 치트라 허용 — 이번 스코프 아님.
- 이 판정 3건은 전부 "지금 코드에 그런 경로가 없다"에 근거한다. 위 **재검토 트리거**가 발생하면 근거가 사라진다.

## ⚠ 검증 함정 — PIE로는 3번을 확인할 수 없다

`IsRunningDedicatedServer()`는 커맨드라인 `-server` 로 판정한다(`Core/Public/Misc/CoreMisc.h:152`). 에디터 프로세스에선 **항상 false**다 → PIE의 "Run Dedicated Server" 월드는 위젯을 실제로 생성한다.

위젯 스킵을 실측하려면 스테이징된 `Project_REServer.exe`로 띄워야 한다. 1·2번(복제 경로)은 PIE로 충분하다.

## 파일 변경 요약

| 파일 | 변경 |
|------|------|
| `Core/RECharacterBase.h` | `MaxHealth` 주석에 비복제 근거 1줄 |
| `Core/REBossCharacter.h` | 동일 |
| `docs/guides/dedicated-server.md` | HP 검증 절차 섹션 추가 (운영 절차) |

기능 코드 변경 없음.

## 검증

`docs/superpowers/plans/2026-08-10-dedi-hp-replication.md` 참조. 절차 본문은 `docs/guides/dedicated-server.md`의 "HP 리플리케이션 검증 절차".

## 참고

- 커밋: `d29c83e docs(net): HP 복제 경로 검증 결론 + MaxHealth 비복제 근거 (#73)`
