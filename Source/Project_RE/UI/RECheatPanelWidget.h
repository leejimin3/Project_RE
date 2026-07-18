// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RECheatPanelWidget.generated.h"

class UCheckBox;

/**
 *  인게임 치트 패널 (SRDebugger-lite). 순수 C++ UUserWidget — Initialize()에서 트리 구성.
 *  치트 상태는 CVar 백엔드. 지금은 무적 토글 1개. 치트 추가 = 행 빌드 블록 복사.
 */
UCLASS()
class URECheatPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;

private:
	/** 무적 체크박스 상태 변경 → re.Cheat.PlayerInvincible CVar 세팅. */
	UFUNCTION()
	void OnInvincibleChanged(bool bIsChecked);

	UPROPERTY()
	UCheckBox* InvincibleCheck = nullptr;
};
