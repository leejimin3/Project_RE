// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "REHealthBarWidget.generated.h"

class UProgressBar;

/**
 *  순수 C++ HP바 위젯 (#29). Initialize()에서 WidgetTree로 UProgressBar 루트 생성 — BP 자산 불필요.
 *  NativeConstruct는 슬레이트 트리 구축 후에 불려서 늦다 — RootWidget은 Initialize()에서 세팅해야 한다.
 */
UCLASS()
class UREHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 프로그레스바 percent 설정 (0~1 클램프). */
	void SetPercent(float InPercent);

	/** 바 채움 색 설정. */
	void SetBarColor(FLinearColor InColor);

protected:
	virtual bool Initialize() override;

private:
	/** WidgetTree 소유 프로그레스바 루트. */
	UPROPERTY()
	TObjectPtr<UProgressBar> Bar;
};
