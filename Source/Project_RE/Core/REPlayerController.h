// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "REPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 *  탑뷰 PlayerController. 우클릭 입력을 받을 뼈대만 세운다.
 *  IA/IMC는 uasset 없이 코드로 생성(transient). 실제 이동은 M2.
 */
UCLASS()
class AREPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** 우클릭 핸들러 (M0에선 로그만) */
	void OnClickMove(const FInputActionValue& Value);

	UPROPERTY()
	UInputAction* ClickMoveAction;

	UPROPERTY()
	UInputMappingContext* TopDownMappingContext;
};
