---
tags: [platforming, mechanic, animation]
type: system
---
# DashMechanic
대시 이동 메커닉 + 애니메이션.

## Deps
[[PlatformingCharacter]] [[Shared/EnhancedInput]]

## Detail
- `AnimNotify_EndDash` — 대시 종료 타이밍 알림
- C++: `Source/Project_RE/Variant_Platforming/Animation/AnimNotify_EndDash.cpp`
- AnimMontage: `Content/Variant_Platforming/Anims/AM_Dash.uasset`
- ABP: `Content/Variant_Platforming/Anims/ABP_...`
- VFX: `NS_Jump_Trail.uasset`
- Input: `Content/Variant_Platforming/Input/` (대시 액션)

## Related
[[Platforming_Overview]]
