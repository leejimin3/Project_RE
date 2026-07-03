// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RECharacterBase.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 *  탑뷰 쿼터뷰 플레이어 폰 베이스.
 *  캐릭터 부착 SpringArm + Camera를 절대회전으로 고정한다.
 *  (HP/복제는 M0 #3에서 추가)
 */
UCLASS()
class ARECharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ARECharacterBase();

protected:
	/** 탑뷰 카메라 붐 (절대 하향 고정) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** 탑뷰 카메라 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* TopDownCamera;
};
