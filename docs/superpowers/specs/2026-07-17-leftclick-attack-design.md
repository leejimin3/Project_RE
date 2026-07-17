# 좌클릭 방향 공격 설계 (M3.5 중간점검 ①)

날짜: 2026-07-17. 상태: 사용자 승인 완료.

## 목적

자동사격(`UREAutoFireComponent` — 서버 타이머, 최근접 보스 자동조준) 제거.
로스트아크 평타 스타일로 교체: 유저가 좌클릭한 커서 방향으로 발사.

## 합의된 동작

- **홀드 연사**: 좌클릭 누르고 있으면 AttackInterval 주기로 연사.
- **공격 중 정지**: 발사 순간 이동 중단 (`StopMovementImmediately`).
- **커서 방향 회전**: 발사 순간 폰 yaw가 커서 방향을 봄.
- **판정**: 히트스캔 유지 (기존 라인트레이스 재사용). 투사체 없음.

## 아키텍처: plain Server RPC (우클릭 이동 패턴 재사용)

GAS 어빌리티 아님 — 평타에 코스트/예측/차단 태그 필요 없음. 대쉬(GAS)와 다른 선택인 이유:
대쉬는 쿨다운 GE가 필요했고, 평타는 rate limit 하나면 됨.

### 흐름

1. **클라** (`AREPlayerController`): `FireAction`(LMB) transient IMC 매핑.
   홀드 중 AttackInterval 주기 페이싱 → 커서 아래 지면 히트 →
   `Dir = (커서지점 - 폰위치).GetSafeNormal2D()` → `Server_RequestFire(Dir)`.
2. **서버 RPC**: rate limit (서버 측 마지막 발사시각 + 간격 검사 — 클라 스팸/치팅 방어).
   폰 yaw를 Dir로 회전, `StopMovementImmediately()`, `AttackComponent->FireInDirection(Dir)`.
3. **`FireInDirection(Dir)`**: 트레이스 Start=폰위치+Z50, End=Start+Dir×AttackRange(기본 2000uu).
   첫 블로킹 히트가 `AREBossCharacter`면 `TakeDamage`. 기존 트레이스/데미지/디버그라인 코드 재사용.

### 정리되는 것

- `UREAutoFireComponent` → `UREAttackComponent` 리네임. 자동 타이머·최근접 보스 탐색 삭제.
- `AREGameMode::EndGame`의 `StopFiring()` 호출 삭제 → 게임오버 후 `Server_RequestFire` 무시 가드로 교체.

### 스탯 (이번 이슈에선 UPROPERTY 하드코딩, ③에서 Settings 이관)

| 스탯 | 기본값 |
|---|---|
| AttackDamage | 10 |
| AttackInterval | 0.25s |
| AttackRange | 2000uu |

## M4 대비

클라 입력 → 서버 RPC → 서버 판정 구조 그대로 데디 동작. 무수정.

## 검증

- 빌드 게이트 통과.
- 헤드리스 프로브: 서버 권위에서 `Server_RequestFire` 경로 강제 실행 → 발사/히트 로그 관측
  (기존 이동/대쉬 프로브 패턴).
- 프로파일링 하네스 영향 없음 확인 — scripts는 AutoFire 미참조, KeepFiring 경로 무관.
