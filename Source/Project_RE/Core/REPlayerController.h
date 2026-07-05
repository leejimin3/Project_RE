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
	virtual void PlayerTick(float DeltaTime) override;

	/** 우클릭 핸들러: 커서 아래 지점을 이동 목표로 설정 */
	void OnClickMove(const FInputActionValue& Value);

	UPROPERTY()
	UInputAction* ClickMoveAction;

	UPROPERTY()
	UInputMappingContext* TopDownMappingContext;

	/** 이동 목표 지점 (월드) */
	FVector MoveTarget = FVector::ZeroVector;

	/** 목표를 향해 이동 중인지 */
	bool bMoveToTarget = false;

	/** 목표 도달로 간주하는 반경 */
	float AcceptanceRadius = 120.f;
};
