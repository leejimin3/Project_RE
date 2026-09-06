// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "REHealthBarComponent.generated.h"

/**
 *  머리 위 월드스페이스 HP바 컴포넌트 (#29). 셋업 전부 캡슐화 — 소유 캐릭터는 SetHealthPercent만 호출.
 *  절대회전 고정: 캐릭터가 bOrientRotationToMovement로 회전해도 바는 탑뷰 카메라(피치 -50)를 계속 정면으로 본다.
 */
UCLASS()
class UREHealthBarComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UREHealthBarComponent();

	/** HP 비율(0~1) 반영. 위젯 미생성 시점 호출이면 캐시 후 InitWidget에서 적용. */
	void SetHealthPercent(float Percent);

	//~ 위젯 생성 직후 색/캐시 percent 적용.
	virtual void InitWidget() override;

	/** 바 채움색. 캐릭터 생성자에서 지정 (플레이어 초록, 보스 빨강). */
	UPROPERTY(EditAnywhere, Category = "HealthBar")
	FLinearColor BarColor = FLinearColor::Green;

private:
	/** 위젯 생성 전 SetHealthPercent 호출 대비 캐시. */
	float CachedPercent = 1.f;
};
