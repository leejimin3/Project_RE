---
tags: [root, overview]
type: project
---
# Project_RE
UE5.8 third-person multi-variant gameplay demo. 3개 독립 변형 + 공용 기반.

## Variants
[[Variant_Combat/Combat_Overview]] [[Variant_Platforming/Platforming_Overview]] [[Variant_SideScrolling/SideScrolling_Overview]]

## Core
[[Core/BaseCharacter]] [[Core/BaseGameMode]] [[Core/BasePlayerController]]

## Shared Systems
[[Shared/EnhancedInput]] [[Shared/StateTree]] [[Shared/LevelPrototyping]]

## Detail
- Engine: UE 5.8
- Plugins: StateTree, GameplayStateTree, ModelingToolsEditorMode
- AI: [[Shared/StateTree]] (StateTree 기반, BT 미사용)
- Input: [[Shared/EnhancedInput]] (Enhanced Input System)
- UI: UMG
- Rendering: Substrate, Ray Tracing 활성화
