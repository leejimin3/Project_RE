// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHealthBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"

bool UREHealthBarWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// CDO는 WidgetTree 트리 구성 대상 아님 — 인스턴스에서만 생성.
	if (WidgetTree && !Bar)
	{
		Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("Bar"));
		Bar->SetPercent(1.f);

		// 스타일 (#100). 기본 ProgressBar 는 배경이 밝은 회색이라 밝은 씬에서 채움과 구분이 안 된다.
		// 어두운 배경 + 채도 있는 채움으로 대비를 준다. 위젯 크기는 REHealthBarComponent 가 정한다.
		FProgressBarStyle Style = Bar->GetWidgetStyle();
		Style.BackgroundImage.TintColor = FSlateColor(FLinearColor(0.04f, 0.04f, 0.06f, 0.85f));
		Style.FillImage.TintColor       = FSlateColor(FLinearColor(0.90f, 0.20f, 0.20f, 1.f));
		Bar->SetWidgetStyle(Style);

		WidgetTree->RootWidget = Bar;
	}
	return true;
}

void UREHealthBarWidget::SetPercent(float InPercent)
{
	if (Bar)
	{
		Bar->SetPercent(FMath::Clamp(InPercent, 0.f, 1.f));
	}
}

void UREHealthBarWidget::SetBarColor(FLinearColor InColor)
{
	if (Bar)
	{
		Bar->SetFillColorAndOpacity(InColor);
	}
}
