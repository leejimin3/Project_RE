// Copyright Epic Games, Inc. All Rights Reserved.

#include "REPlayerHudWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Styling/CoreStyle.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/REGA_Dash.h"
#include "Mass/REBulletRenderSubsystem.h"

namespace
{
	/** 투사체 수 표시 on/off. 데모 계측이라 게임 UI 와 수명이 다르다 — 따로 끈다. */
	static TAutoConsoleVariable<int32> CVarHudBulletCount(
		TEXT("re.Debug.HudBulletCount"),
		1,
		TEXT("HUD 의 화면상 투사체 수 표시 (0=끔)."),
		ECVF_Cheat);

	/** 대쉬 쿨다운 길이. REGE_DashCooldown 의 DurationMagnitude 와 같아야 한다. */
	constexpr float DashCooldownSec = 2.0f;

	const FLinearColor ColorHealth(0.90f, 0.25f, 0.25f, 1.f);
	const FLinearColor ColorDashReady(0.35f, 0.75f, 1.00f, 1.f);
	const FLinearColor ColorDashCharging(0.30f, 0.35f, 0.45f, 1.f);
	const FLinearColor ColorLabel(0.85f, 0.87f, 0.92f, 1.f);

	UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Name, int32 Size)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		T->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		T->SetColorAndOpacity(FSlateColor(ColorLabel));
		// 배경이 밝은 씬에서도 읽히도록 그림자. 배경 패널을 따로 두지 않는 대신이다.
		T->SetShadowOffset(FVector2D(1.f, 1.f));
		T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
		return T;
	}

	UProgressBar* MakeBar(UWidgetTree* Tree, const TCHAR* Name, FLinearColor Fill)
	{
		UProgressBar* B = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), Name);
		B->SetFillColorAndOpacity(Fill);
		B->SetPercent(1.f);

		FProgressBarStyle Style = B->GetWidgetStyle();
		Style.BackgroundImage.TintColor = FSlateColor(FLinearColor(0.04f, 0.04f, 0.06f, 0.75f));
		B->SetWidgetStyle(Style);
		return B;
	}

	/** 바 높이 고정. VerticalBox 안의 ProgressBar 는 기본 desired height 가 얇다. */
	USizeBox* WrapHeight(UWidgetTree* Tree, UWidget* Inner, const TCHAR* Name, float Height)
	{
		USizeBox* Box = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), Name);
		Box->SetHeightOverride(Height);
		Box->AddChild(Inner);
		return Box;
	}
}

bool UREPlayerHudWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// CDO 는 WidgetTree 구성 대상이 아니다 — 인스턴스에서만 만든다 (#29/#40 동일).
	if (!WidgetTree || HealthBar)
	{
		return true;
	}

	// 뷰포트 슬롯은 루트를 화면 전체로 늘린다 — VerticalBox 를 그대로 루트로 두면 바가
	// 화면 전폭이 된다(실측 스크린샷에서 확인). Overlay 로 좌상단에 고정하고,
	// SizeBox 로 폭을 묶는다.
	UOverlay* Screen = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Screen"));
	USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Frame"));
	Frame->SetWidthOverride(320.f);
	if (UOverlaySlot* FrameSlot = Cast<UOverlaySlot>(Screen->AddChild(Frame)))
	{
		FrameSlot->SetHorizontalAlignment(HAlign_Left);
		FrameSlot->SetVerticalAlignment(VAlign_Top);
		FrameSlot->SetPadding(FMargin(28.f, 24.f, 0.f, 0.f));
	}

	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Root"));
	Frame->AddChild(Root);

	auto AddRow = [Root](UWidget* W, float BottomPad)
	{
		if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Root->AddChild(W)))
		{
			S->SetPadding(FMargin(0.f, 0.f, 0.f, BottomPad));
			S->SetHorizontalAlignment(HAlign_Fill);
		}
	};

	HealthText = MakeLabel(WidgetTree, TEXT("HealthText"), 16);
	AddRow(HealthText, 2.f);

	HealthBar = MakeBar(WidgetTree, TEXT("HealthBar"), ColorHealth);
	AddRow(WrapHeight(WidgetTree, HealthBar, TEXT("HealthBarBox"), 16.f), 10.f);

	DashText = MakeLabel(WidgetTree, TEXT("DashText"), 14);
	AddRow(DashText, 2.f);

	DashBar = MakeBar(WidgetTree, TEXT("DashBar"), ColorDashReady);
	AddRow(WrapHeight(WidgetTree, DashBar, TEXT("DashBarBox"), 10.f), 12.f);

	// 투사체 수만 따로 컨테이너에 담는다 — CVar 로 이 줄만 접기 위해서다.
	BulletBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BulletBox"));
	BulletText = MakeLabel(WidgetTree, TEXT("BulletText"), 20);
	BulletBox->AddChild(BulletText);
	AddRow(BulletBox, 0.f);

	WidgetTree->RootWidget = Screen;
	return true;
}

void UREPlayerHudWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	RefreshHealth();
	RefreshDash();
	RefreshBulletCount();
}

void UREPlayerHudWidget::RefreshHealth()
{
	if (!HealthBar || !HealthText)
	{
		return;
	}

	const APlayerController* PC = GetOwningPlayer();
	const ARECharacterBase* Char = PC ? Cast<ARECharacterBase>(PC->GetPawn()) : nullptr;
	if (!Char)
	{
		// 사망 후 관전 중 — 폰이 없다. 0 으로 두고 표시는 유지한다(사라지면 무슨 일인지 안 보인다).
		HealthBar->SetPercent(0.f);
		HealthText->SetText(FText::FromString(TEXT("HP  0")));
		return;
	}

	const float Max = FMath::Max(Char->GetMaxHealth(), 1.f);
	const float Cur = FMath::Clamp(Char->GetHealth(), 0.f, Max);
	HealthBar->SetPercent(Cur / Max);
	HealthText->SetText(FText::FromString(
		FString::Printf(TEXT("HP  %d / %d"), FMath::RoundToInt(Cur), FMath::RoundToInt(Max))));
}

void UREPlayerHudWidget::RefreshDash()
{
	if (!DashBar || !DashText)
	{
		return;
	}

	const APlayerController* PC = GetOwningPlayer();
	const ARECharacterBase* Char = PC ? Cast<ARECharacterBase>(PC->GetPawn()) : nullptr;
	UAbilitySystemComponent* ASC = Char ? Char->GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		DashBar->SetPercent(0.f);
		DashBar->SetFillColorAndOpacity(ColorDashCharging);
		DashText->SetText(FText::FromString(TEXT("DASH  -")));
		return;
	}

	// 어빌리티에 직접 묻는다 — GAS 정본 API.
	//
	// 처음엔 Cooldown.Dash 태그로 GE 잔여시간을 질의했는데 항상 0 이 나왔다. REGE_DashCooldown 이
	// 태그를 InheritableOwnedTagsContainer 로 다는데 5.8 에서 폐기돼서다
	// ("CooldownGameplayEffectClass 'REGE_DashCooldown' grants no tags" 경고). 재활성 차단은
	// 다른 경로로 동작하고 있어 겉으론 멀쩡해 보인다 — 태그 질의만 조용히 빈다.
	float Remaining = 0.f;
	if (const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(UREGA_Dash::StaticClass()))
	{
		if (const UGameplayAbility* Ability = Spec->Ability)
		{
			Remaining = Ability->GetCooldownTimeRemaining(ASC->AbilityActorInfo.Get());
		}
	}

	if (Remaining <= 0.f)
	{
		DashBar->SetPercent(1.f);
		DashBar->SetFillColorAndOpacity(ColorDashReady);
		DashText->SetText(FText::FromString(TEXT("DASH  READY")));
		return;
	}

	DashBar->SetPercent(1.f - FMath::Clamp(Remaining / DashCooldownSec, 0.f, 1.f));
	DashBar->SetFillColorAndOpacity(ColorDashCharging);
	DashText->SetText(FText::FromString(FString::Printf(TEXT("DASH  %.1fs"), Remaining)));
}

void UREPlayerHudWidget::RefreshBulletCount()
{
	if (!BulletText || !BulletBox)
	{
		return;
	}

	if (CVarHudBulletCount.GetValueOnGameThread() == 0)
	{
		BulletBox->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	BulletBox->SetVisibility(ESlateVisibility::HitTestInvisible);

	int32 Count = 0;
	if (const UWorld* World = GetWorld())
	{
		if (const UREBulletRenderSubsystem* Sub = World->GetSubsystem<UREBulletRenderSubsystem>())
		{
			// 그려지는 인스턴스 수를 센다 — Mass 엔티티 수가 아니라 화면에 실제로 있는 수다.
			// 착지 마커(MarkerISM)는 투사체가 아니므로 빼고, 직선탄 + 곡사탄만 더한다.
			if (const UInstancedStaticMeshComponent* ISM = Sub->GetISM())
			{
				Count += ISM->GetInstanceCount();
			}
			if (const UInstancedStaticMeshComponent* ArcISM = Sub->GetArcISM())
			{
				Count += ArcISM->GetInstanceCount();
			}
		}
	}

	BulletText->SetText(FText::FromString(
		FString::Printf(TEXT("PROJECTILES  %s"), *FString::FormatAsNumber(Count))));
}
