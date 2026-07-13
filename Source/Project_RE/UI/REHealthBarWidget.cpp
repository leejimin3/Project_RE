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
