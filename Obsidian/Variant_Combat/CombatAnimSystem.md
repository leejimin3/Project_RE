---
tags: [combat, animation]
type: system
---
# CombatAnimSystem
전투 애니메이션 — ABP + AnimNotify 3종.

## Deps
[[CombatCharacter]] [[CombatEnemy]]

## Detail
ABP: `Content/Variant_Combat/Anims/ABP_Manny_Combat.uasset`

AnimMontage:
- `AM_ComboAttack` — 콤보 공격
- `AM_ChargedAttack` — 차지 공격

AnimNotify (C++):
- `AnimNotify_CheckCombo` — 콤보 입력 체크 타이밍
- `AnimNotify_CheckChargedAttack` — 차지 판정 타이밍
- `AnimNotify_DoAttackTrace` — 히트박스 트레이스 실행

## Related
[[Combat_Overview]] [[CombatInterfaces]]
