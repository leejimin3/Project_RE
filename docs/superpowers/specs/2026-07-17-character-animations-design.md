# 플레이어/보스 애니메이션 설계 (M3.5 중간점검 ②)

날짜: 2026-07-17. 상태: 사용자 승인 완료. 선행: ① 좌클릭 공격 (발사 모션이 FireInDirection 훅에 걸림).

## 목적

- 보스가 안 보임 — `AREBossCharacter` 생성자에 메시/애님BP 로드 없음. 가시화.
- 플레이어 발사/대쉬 모션 없음 — 추가.
- 범위: **공격 + 대쉬 + 이동만**. 사망/피격 리액트는 범위 외 (합의).

## 설계

### 보스 가시화

`AREBossCharacter` 생성자에 `RECharacterBase`와 동일 패턴 추가:

- 메시: `SKM_Quinn_Simple` (플레이어 Manny와 구분)
- 애님BP: `ABP_Unarmed` — idle/이동 자동 처리 (같은 마네퀸 스켈레톤)
- 오프셋: RelativeLocation Z-90, RelativeRotation Yaw-90
- `ConstructorHelpers` 로드, 실패 시 크래시 없이 진행 (기존 패턴)

### 플레이어 발사 모션

`FireInDirection` 성공 경로에서 `MM_Pistol_Fire_Montage` 재생 (같은 스켈레톤, 호환).
싱글/리슨: 서버 재생 = 화면 표시됨. M4 데디: Multicast RPC 필요 — TODO 주석만 남김.

### 대쉬 모션

`REGA_Dash::ActivateAbility`에서 `MM_Dash` 재생.
AnimSequence(몽타주 아님) → `PlaySlotAnimationAsDynamicMontage`로 슬롯 재생.

### 이동

플레이어는 이미 `ABP_Unarmed` 로코모션 동작 중 — 변경 없음.
보스는 고정형 — ABP idle이면 충분.

## 검증

- 빌드 게이트 통과.
- 시각 검증: `-windowed` 실RHI + `FScreenshotRequest` PNG — 보스 메시 렌더 확인
  (`-nullrhi`는 메시 가시성 오진 함정 — 메모리 참조).
- 몽타주 재생: 발사/대쉬 시 `Montage_Play` 반환값(재생 길이 > 0) 로그 프로브.
