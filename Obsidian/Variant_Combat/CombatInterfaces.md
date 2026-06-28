---
tags: [combat, interface]
type: system
---
# CombatInterfaces
전투 시스템 인터페이스 3종.

## Deps
[[Combat_Overview]]

## Detail
- `CombatAttacker` — 데미지 가할 수 있는 액터 (C++: `Interfaces/CombatAttacker.cpp`)
- `CombatDamageable` — 데미지 받을 수 있는 액터 (C++: `Interfaces/CombatDamageable.cpp`)
- `CombatActivatable` — 활성화될 수 있는 액터 (C++: `Interfaces/CombatActivatable.cpp`)

## Implementors
[[CombatCharacter]] → Attacker
[[CombatEnemy]] → Damageable
[[CombatGameplay]] → Damageable, Activatable

## Triggered By
[[CombatAnimSystem]]
