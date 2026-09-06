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
		ResultText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 96));
		ResultText->SetJustification(ETextJustify::Center);
		// 스타일 (#100). 탄막이 화면을 덮은 상태에서 글자만으로는 안 읽힌다 — 그림자로 분리한다.
		ResultText->SetShadowOffset(FVector2D(4.f, 4.f));
		ResultText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.9f));

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
		// 원색보다 살짝 밝고 덜 쨍한 톤 — 원색 초록/빨강은 화면에서 뭉개진다 (#100).
		ResultText->SetColorAndOpacity(FSlateColor(bVictory
			? FLinearColor(0.35f, 0.95f, 0.45f, 1.f)
			: FLinearColor(0.95f, 0.30f, 0.30f, 1.f)));
	}
}
