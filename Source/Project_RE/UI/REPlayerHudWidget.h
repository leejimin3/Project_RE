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
 *  기존 위젯들과 같이 순수 C++ 로 WidgetTree 를 구성한다 — BP 자산 없음(#29/#40 동일).
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

	/** 보스가 지금 쏘는 패턴 이름. 보스의 외관 상태에서 읽는다 — 새 복제 경로가 없다. */
	void RefreshPattern();

	/** 켜져 있는 데브 치트 나열. 하나도 없으면 줄째로 접는다. */
	void RefreshCheats();

	UPROPERTY()
	TObjectPtr<UProgressBar> HealthBar = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> HealthText = nullptr;

	UPROPERTY()
	TObjectPtr<UProgressBar> DashBar = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> DashText = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> BulletText = nullptr;

	UPROPERTY()
	TObjectPtr<UVerticalBox> BulletBox = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> PatternText = nullptr;

	UPROPERTY()
	TObjectPtr<UVerticalBox> PatternBox = nullptr;

	UPROPERTY()
	TObjectPtr<UTextBlock> CheatText = nullptr;

	UPROPERTY()
	TObjectPtr<UVerticalBox> CheatBox = nullptr;

	/** 보스 약참조 캐시. 매 틱 액터를 훑지 않기 위한 것 — 죽거나 없으면 다시 찾는다. */
	TWeakObjectPtr<class AREBossCharacter> CachedBoss;
};
