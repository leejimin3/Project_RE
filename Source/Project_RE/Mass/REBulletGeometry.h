// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  탄환 지오메트리 단일 출처.
 *
 *  전에는 BulletScale(렌더) / HitRadius(판정) / ActorBulletScale(Actor 비교군)이
 *  세 파일에 흩어져 **주석으로만** 묶여 있었다. 주석은 컴파일러가 검사하지 않는다 —
 *  실제로 REBulletActor.cpp 가 "REBulletRenderProcessor.cpp:14 의 BulletScale" 이라고
 *  가리키던 줄번호가 이미 27번으로 밀려 있었다. 값이 아직 맞았을 뿐 참조는 썩어 있었다.
 *
 *  판정 반경이 시각 반경과 어긋나면 "눈에 안 닿았는데 맞거나, 닿았는데 안 맞는" 상태가 된다.
 *  회피 게임에서 이건 공정성 버그다 (#97). 그래서 주석이 아니라 **유도식**으로 묶는다.
 *
 *  **네임스페이스를 쓰는 이유:** 익명 네임스페이스로 두면 유니티 빌드에서 동명 변수가
 *  C4459 로 충돌한다 — 이 프로젝트에 전례가 있어 Baseline 쪽 상수 이름을
 *  ActorBulletScale 로 비틀어 두어야 했다. 이름 하나로 합치면 원인부터 사라진다.
 */
namespace REBulletGeometry
{
	/** /Engine/BasicShapes/Sphere 원본 반경(cm). 지름 100cm 구. */
	inline constexpr float EngineSphereRadius = 50.f;

	/**
	 *  탄환 인스턴스 스케일 — 엔진 Sphere 를 지름 50cm 로. #17: 0.2 는 카메라 거리서
	 *  sub-pixel 이라 0.5 로 상향.
	 *
	 *  탄이 서로 겹친다: 간격 = BulletSpeed(200) × BossFireInterval(0.15) = 30uu < 지름 50uu.
	 *  겹침 자체는 의도적으로 허용한다 — 축소해서 틈을 만들면(0.2 시도) 탄이 너무 작아
	 *  탄막의 압도적인 인상이 사라진다. 대신 인접 탄을 **다른 색으로 교차**시켜 가른다 (#97).
	 */
	inline constexpr float BulletScale = 0.5f;

	/** 탄환 시각 반경(cm) — 유도값. 25. */
	inline constexpr float BulletVisualRadius = EngineSphereRadius * BulletScale;

	/**
	 *  플레이어 쪽 판정 여유(cm).
	 *
	 *  ARECharacterBase 는 캡슐 크기를 지정하지 않아 ACharacter 기본값을 그대로 쓴다 —
	 *  반경 34 / 반높이 88 (Engine/Private/Character.cpp: InitCapsuleSize(34.f, 88.f)).
	 *  이 값은 그 34 를 35 로 올림한 것이다. **34 로 내리지 마라** — HitRadius 가 59 가 되어
	 *  판정이 1uu 좁아지고, 그건 게임플레이 변경이다. 캡슐을 실제로 바꾸는 날 같이 바꾼다.
	 */
	inline constexpr float PlayerCapsuleAllowance = 35.f;

	/**
	 *  탄환 히트 반경(cm) — 유도값. 60.
	 *  BulletScale 을 바꾸면 여기가 **자동으로** 따라간다. 이게 이 헤더의 목적 전부다.
	 */
	inline constexpr float HitRadius = BulletVisualRadius + PlayerCapsuleAllowance;
}
