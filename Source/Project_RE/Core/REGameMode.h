// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MassEntityTypes.h"
#include "REGameMode.generated.h"

/**
 *  Mass 스모크 테스트용 throwaway 프래그먼트.
 *  M0 #2 프로브 전용 — M1에서 실제 탄막 프래그먼트로 교체·이동한다.
 */
USTRUCT()
struct FRETestFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 Dummy = 0;
};

/**
 *  탑뷰 게임모드. 기본 폰/컨트롤러를 RE 클래스로 지정.
 *  BeginPlay에서 Mass 엔티티 1개를 생성해 서브시스템 가동을 실증한다.
 *  GameModeBase는 서버에만 존재 → 모든 로직이 곧 서버 권위(별도 가드 불필요).
 */
UCLASS()
class AREGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AREGameMode();

protected:
	virtual void BeginPlay() override;
};
