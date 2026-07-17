# 스탯 노출 — DeveloperSettings 설계 (M3.5 중간점검 ③)

날짜: 2026-07-17. 상태: 사용자 승인 완료. 선행: ① 좌클릭 공격 (공격 스탯 표면 확정 후 이관).

## 목적

플레이어/보스 스탯이 하드코딩 산재 — 조절하려면 리빌드. 한곳에서 조절 가능하게:

- `UREAttackComponent`: AttackDamage / AttackInterval / AttackRange (UPROPERTY)
- `RECharacterBase` / `REBossCharacter`: MaxHealth (UPROPERTY, BP 없어 사실상 고정)
- `REBulletPattern`: FireIntervalSec / BulletLifetimeSec (constexpr), Speed (구조체 기본값)

## 설계: UDeveloperSettings + DefaultGame.ini

`UREStatsSettings : UDeveloperSettings`, `Config=Game, defaultconfig` 1클래스.

- 에디터 Project Settings UI에서 편집 → `DefaultGame.ini` 저장. 리빌드 불필요.
- 데디 서버(M4)에서도 ini 그대로 동작. uasset 불필요 — 코드 온리 기조 유지.
- DataAsset 기각: BP/uasset 워크플로우 필요. CVar 전면화 기각: 영구 저장 없음.

### 스탯 표면 (기본값 = 현재 하드코딩과 동일 — 회귀 없음)

| 카테고리 | 스탯 | 기본값 |
|---|---|---|
| Player | MaxHealth | 100 |
| Player | AttackDamage | 10 |
| Player | AttackInterval | 0.25 |
| Player | AttackRange | 2000 |
| Boss | MaxHealth | 100 |
| Boss | BulletSpeed | 300 |
| Boss | BulletLifetime | 3 |
| Boss | FireInterval | 0.1 |

### 소비처 교체

- `REBulletPattern::FireIntervalSec/BulletLifetimeSec` constexpr → Settings 조회로 교체.
  클로즈드루프 역산 공식(수명/발사주기)도 Settings 값 사용 —
  **"보스 탄막 지속시간" 조절이 BulletLifetime으로 달성됨.**
- `FSpiralParams`/`FFanParams` Speed/Lifetime 기본값 → Settings 조회.
- `AREGameMode` 데모 발사 타이머 주기 → Settings FireInterval.
- MaxHealth: 생성자에서 `GetDefault<UREStatsSettings>()` 조회로 초기화.

### CVar 관계 — 역할 분리

`re.Bullets.Count` / `re.Bullets.SpawnKi` **존치**. 프로파일링 런타임 스윕용(-ExecCmds 즉석 오버라이드).
Settings = 영구 기본값, CVar = 측정용 즉석 노브. 중복 아님.

## 검증

- 빌드 게이트 통과.
- 헤드리스 프로브: ini 값 변경 → 재시작 → 로그로 스탯 반영 확인.
  주의: BulletLifetime을 줄여도 클로즈드루프가 스폰율을 올려 라이브 카운트는 목표 유지 —
  탄 수로는 검증 불가. 대신 발사 정지 후 lifetime+ε 시점에 라이브 카운트 0 관측으로 수명 검증.
- 기본값 회귀 없음: ini 미변경 상태에서 기존 동작 동일.
