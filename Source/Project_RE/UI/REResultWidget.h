// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "REResultWidget.generated.h"

class UTextBlock;

/**
 *  순수 C++ 결과 화면 위젯 (#40). Initialize()에서 WidgetTree로 Overlay+TextBlock 생성 — BP 자산 불필요.
 *  HP바(#29)와 달리 월드스페이스가 아니라 화면공간 — AddToViewport()로 뷰포트에 직접 올린다.
 *  Overlay 루트: TextBlock 단독 루트는 뷰포트 슬롯에서 Fill 되어 세로 중앙 정렬이 보장되지 않는다.
 */
UCLASS()
class UREResultWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 승/패 텍스트·색 적용. VICTORY=초록 / DEFEAT=빨강. */
	void SetResult(bool bVictory);

protected:
	virtual bool Initialize() override;

private:
	/** WidgetTree 소유 결과 텍스트. */
	UPROPERTY()
	TObjectPtr<UTextBlock> ResultText;
};
