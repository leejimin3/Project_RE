// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "REGameMode.generated.h"

/**
 *  탑뷰 게임모드. 기본 폰/컨트롤러를 RE 클래스로 지정.
 *  (서버 전용 로직은 M0 #3에서 추가)
 */
UCLASS()
class AREGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AREGameMode();
};
