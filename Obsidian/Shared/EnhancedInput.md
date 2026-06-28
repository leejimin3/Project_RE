---
tags: [shared, input]
type: system
---
# EnhancedInput
UE Enhanced Input System. 모든 변형 + 공용 입력 처리.

## Deps
[[Project_RE]]

## Detail
공용 액션:
- `IA_Move`, `IA_Look`, `IA_Jump`, `IA_MouseLook`
- IMC: `IMC_Default`, `IMC_MouseLook`
- Touch UI: `UI_Thumbstick`, `UI_TouchSimple`

변형별 추가 액션:
- Combat: 공격 액션 (`Content/Variant_Combat/Input/`)
- Platforming: 대시 액션 (`Content/Variant_Platforming/Input/`)
- SideScrolling: Interact, Move, Drop (`Content/Variant_SideScrolling/Input/`)

## Users
[[Variant_Combat/CombatCharacter]] [[Variant_Platforming/PlatformingCharacter]] [[Variant_SideScrolling/SideScrollingCharacter]]
