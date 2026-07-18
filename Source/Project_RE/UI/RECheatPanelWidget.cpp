// Copyright Epic Games, Inc. All Rights Reserved.

#include "RECheatPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "HAL/IConsoleManager.h"

bool URECheatPanelWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// CDO는 트리 구성 대상 아님 — 인스턴스에서만 (REResultWidget 동일).
	if (!WidgetTree || InvincibleCheck)
	{
		return true;
	}

	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
	Root->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	Root->SetPadding(FMargin(12.f));

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box"));
	Root->AddChild(Box);

	// 타이틀
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("CHEATS")));
	Title->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 24));
	Box->AddChild(Title);

	// 무적 행: [체크박스] Player Invincible
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("InvincibleRow"));
	Box->AddChild(Row);

	InvincibleCheck = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("InvincibleCheck"));
	// C++ 생성 UCheckBox는 스타일이 비어 안 보임 → 엔진 기본 체크박스 스타일 주입.
	InvincibleCheck->WidgetStyle = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
	// 초기 상태 = CVar 현재값.
	static IConsoleVariable* Inv = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Cheat.PlayerInvincible"));
	InvincibleCheck->SetIsChecked(Inv && Inv->GetInt() != 0);
	InvincibleCheck->OnCheckStateChanged.AddDynamic(this, &URECheatPanelWidget::OnInvincibleChanged);
	Row->AddChild(InvincibleCheck);

	UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InvincibleLabel"));
	Label->SetText(FText::FromString(TEXT("Player Invincible")));
	Label->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 18));
	if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(Row->AddChild(Label)))
	{
		LabelSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	WidgetTree->RootWidget = Root;
	return true;
}

void URECheatPanelWidget::OnInvincibleChanged(bool bIsChecked)
{
	static IConsoleVariable* Inv = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Cheat.PlayerInvincible"));
	if (Inv)
	{
		Inv->Set(bIsChecked ? 1 : 0);
	}
}
