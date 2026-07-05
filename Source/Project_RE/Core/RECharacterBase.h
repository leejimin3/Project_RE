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
 *  HP는 서버 권위(Replicated) — 데미지 적용은 TakeDamage HasAuthority 가드 경유.
 */
UCLASS()
class ARECharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ARECharacterBase();

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 현재 체력. 서버 권위, 클라 복제. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 100.f;

	/** 탑뷰 카메라 붐 (절대 하향 고정) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** 탑뷰 카메라 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* TopDownCamera;
};
