---
tags: [combat, gameplay, actor]
type: system
---
# CombatGameplay
전투 레벨 배치 오브젝트 모음.

## Deps
[[CombatInterfaces]] [[Combat_Overview]]

## Detail
- `CombatActivationVolume` — 진입 시 이벤트 트리거 (적 스폰 등)
- `CombatCheckpointVolume` — 체크포인트
- `CombatDamageableBox` — 파괴 가능 오브젝트
- `CombatDummy` — 훈련용 더미
- `CombatLavaFloor` — 지속 데미지 구역

C++: `Source/Project_RE/Variant_Combat/Gameplay/`
BP: `Content/Variant_Combat/Blueprints/Interactables/`

## Related
[[CombatEnemySpawner]]
