// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHealthBarComponent.h"
#include "REHealthBarWidget.h"

UREHealthBarComponent::UREHealthBarComponent()
{
	SetWidgetSpace(EWidgetSpace::World);
	SetWidgetClass(UREHealthBarWidget::StaticClass());
	SetDrawSize(FVector2D(100.f, 10.f));

	// (피치 +50, 요 180) = 카메라 붐(피치 -50, 요 0)의 정반대 방향 — 항상 카메라 정면.
	// 절대회전 필수: 캐릭터 요 회전에 끌려가면 바가 옆면으로 사라진다.
	SetUsingAbsoluteRotation(true);
	SetRelativeRotation(FRotator(50.f, 180.f, 0.f));
	SetRelativeLocation(FVector(0.f, 0.f, 120.f));

	// 자동사격 ECC_Pawn 라인트레이스 차단 방지.
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void UREHealthBarComponent::InitWidget()
{
	Super::InitWidget();

	// 데디 서버는 엔진이 위젯 생성 스킵 → Cast 실패로 자연 no-op.
	if (UREHealthBarWidget* Bar = Cast<UREHealthBarWidget>(GetUserWidgetObject()))
	{
		Bar->SetBarColor(BarColor);
		Bar->SetPercent(CachedPercent);
	}
}

void UREHealthBarComponent::SetHealthPercent(float Percent)
{
	CachedPercent = FMath::Clamp(Percent, 0.f, 1.f);
	if (UREHealthBarWidget* Bar = Cast<UREHealthBarWidget>(GetUserWidgetObject()))
	{
		Bar->SetPercent(CachedPercent);
	}
}
