---
tags: [combat, character]
type: class
---
# CombatCharacter
`CombatCharacter` — 콤보/차지 공격 가능한 플레이어 캐릭터.

## Deps
[[Core/BaseCharacter]] [[Shared/EnhancedInput]] [[CombatAnimSystem]] [[CombatInterfaces]]

## Detail
- 콤보 공격: `AnimNotify_CheckCombo` 통해 체인
- 차지 공격: `AnimNotify_CheckChargedAttack` 통해 판정
- C++: `Source/Project_RE/Variant_Combat/CombatCharacter.cpp`
- BP: `Content/Variant_Combat/Blueprints/BP_CombatCharacter.uasset`

## Related
[[Combat_Overview]] [[CombatUI]]
