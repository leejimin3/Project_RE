// Copyright Epic Games, Inc. All Rights Reserved.

#include "REResultWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Styling/CoreStyle.h"

bool UREResultWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// CDO는 WidgetTree 트리 구성 대상 아님 — 인스턴스에서만 생성 (#29 동일).
	if (WidgetTree && !ResultText)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));

		ResultText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultText"));
		ResultText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 72));
		ResultText->SetJustification(ETextJustify::Center);

		// 화면 정중앙 고정 — 뷰포트 슬롯이 Overlay를 채우고, Overlay 슬롯이 텍스트를 가운데로.
		if (UOverlaySlot* TextSlot = Cast<UOverlaySlot>(Root->AddChild(ResultText)))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}

		WidgetTree->RootWidget = Root;
	}
	return true;
}

void UREResultWidget::SetResult(bool bVictory)
{
	if (ResultText)
	{
		ResultText->SetText(FText::FromString(bVictory ? TEXT("VICTORY") : TEXT("DEFEAT")));
		ResultText->SetColorAndOpacity(FSlateColor(bVictory ? FLinearColor::Green : FLinearColor::Red));
	}
}
