// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "REPlayerHudWidget.generated.h"

class UTextBlock;
class UProgressBar;
class UVerticalBox;

/**
 *  플레이어 상태 HUD (#100). 화면 좌상단에 HP · 대쉬 쿨다운 · 화면상 투사체 수.
 *
 *  WBP 전환 (#121). 아래 위젯 포인터는 `BindWidgetOptional` 이다 — WBP_PlayerHud 의
 *  디자이너에 **같은 이름**의 위젯이 있으면 그것이 바인딩되고, 이 클래스는 값만 채운다.
 *  하나도 없으면 예전처럼 C++ 가 WidgetTree 를 직접 구성한다(폴백).
 *  전부 아니면 전무다 — UUserWidget 의 루트는 하나뿐이라 바인딩과 C++ 구성을
 *  섞으면 루트가 둘이 된다. 디자이너에서 만들 때는 아래 이름/타입을 전부 갖춰라.
 *  매 프레임 폰과 월드에서 값을 당겨온다. 위젯이 상태를 들고 있지 않으므로
 *  폰 교체·리스폰에도 따로 재바인딩할 것이 없다.
 *
 *  투사체 수는 게임 UI 가 아니라 데모 계측이다 — 포트폴리오 문구("N개의 투사체")를
 *  영상이 스스로 증명하게 하려고 띄운다. re.Debug.HudBulletCount 0 으로 끌 수 있다.
 */
UCLASS()
class UREPlayerHudWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** 폰의 Health/MaxHealth 를 바에 반영. 폰이 없으면 0 으로 둔다(관전 중). */
	void RefreshHealth();

	/** Cooldown.Dash GE 잔여시간으로 채움 비율 갱신. 쿨다운 없으면 100%(준비됨). */
	void RefreshDash();

	/** ISM 인스턴스 수(직선탄 + 곡사탄). Mass 엔티티가 아니라 실제 그려지는 수다. */
	void RefreshBulletCount();

	//~ 디자이너 바인딩 대상 (#121). 이름이 곧 계약이다 — WBP 에서 바꾸면 바인딩이 끊긴다.
	//  Optional 인 이유: 없으면 C++ 폴백 구성이 돌아야 하고, 네가 디자인을 만드는 도중
	//  일부만 있는 상태에서도 에디터가 컴파일 에러로 막지 않아야 한다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UProgressBar> HealthBar = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> HealthText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UProgressBar> DashBar = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> DashText = nullptr;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UTextBlock> BulletText = nullptr;

	/** 투사체 표시 묶음. re.Debug.HudBulletCount 0 이 이 박스를 통째로 숨긴다. */
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"))
	TObjectPtr<UVerticalBox> BulletBox = nullptr;
};
