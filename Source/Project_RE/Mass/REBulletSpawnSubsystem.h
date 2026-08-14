// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "REBulletSpawnSubsystem.generated.h"

namespace REBulletPattern { struct FArcBulletSpawnParams; }

/** 탄환 1발 스폰 파라미터. UStruct 아님 — 함수 인자 전용 경량 구조체. */
struct FBulletSpawnParams
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float   Lifetime = 0.f;
	/** 색 선택 0/1 — 머티리얼이 두 색을 Lerp 한다. 인접 탄을 다른 색으로 갈라 겹침을 읽히게 한다 (#97). */
	float   ColorSel = 0.f;
};

/**
 *  탄환 스폰 단일 진입점. 탄환 Archetype을 lazy 캐싱하고 EntityManager로 스폰 + Fragment 초기화.
 *  Boss/패턴 제너레이터(#16)가 GetSubsystem으로 접근한다. 이동/렌더는 후속 이슈.
 */
UCLASS()
class UREBulletSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 탄환 1발 스폰 + Fragment 초기값 주입. EntityManager 없으면 무효 핸들 반환. */
	FMassEntityHandle SpawnBullet(FVector Location, FVector Velocity, float Lifetime, float ColorSel = 0.f);

	/** N발 배치 스폰. 내부는 SpawnBullet 루프(배치 최적화는 YAGNI). */
	void SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params);

	/** 곡사탄 1발 스폰 + arc Fragment 초기화. EntityManager 없으면 무효 핸들 반환. */
	FMassEntityHandle SpawnArcBullet(FVector Start, FVector Target, float FlightTime,
	                                 float MaxHeight, float Damage, float Radius, float InElapsed = 0.f);

	/** N발 배치 스폰. 내부는 SpawnArcBullet 루프. */
	void SpawnArcBulletBatch(TConstArrayView<REBulletPattern::FArcBulletSpawnParams> Params);

private:
	/** 탄환 Archetype 최초 스폰 시 1회 생성·캐싱. */
	void EnsureArchetype(FMassEntityManager& EntityManager);

	/** 같은 World의 UMassEntitySubsystem에서 EntityManager 획득. 없으면 nullptr. */
	FMassEntityManager* GetEntityManager() const;

	FMassArchetypeHandle BulletArchetype;

	/** 곡사탄 Archetype 최초 스폰 시 1회 생성·캐싱. */
	void EnsureArcArchetype(FMassEntityManager& EntityManager);

	FMassArchetypeHandle ArcArchetype;
};
