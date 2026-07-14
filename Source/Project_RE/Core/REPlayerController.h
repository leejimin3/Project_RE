// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "REPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 *  탑뷰 PlayerController. 우클릭으로 커서 아래 지점으로 폰을 이동시킨다.
 *  클릭 검출은 로컬, 이동 권위는 서버(Server_RequestMove RPC → NavMesh 패스팔로잉).
 *  IA/IMC는 uasset 없이 코드로 생성(transient).
 */
UCLASS()
class AREPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/**
	 *  결과 화면 표시 + 입력 차단 (#40). 서버가 EndGame에서 호출, 오너 클라에서 실행.
	 *  싱글/리슨에서는 로컬 즉시 실행 — M4 데디 전환 시 수정 불필요.
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowResult(bool bVictory);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** 우클릭 핸들러: 커서 아래 지점을 서버로 이동 요청 */
	void OnClickMove(const FInputActionValue& Value);

	/** 이동 요청 서버 RPC. 서버가 nav 검증 후 SimpleMoveToLocation 구동. */
	UFUNCTION(Server, Reliable)
	void Server_RequestMove(FVector Target);

	/** 스페이스 핸들러: 커서 방향을 계산해 서버로 대쉬 요청 */
	void OnDash(const FInputActionValue& Value);

	/** 대쉬 요청 서버 RPC. 서버가 폰의 대쉬 어빌리티를 Dir 방향으로 활성. */
	UFUNCTION(Server, Reliable)
	void Server_Dash(FVector Dir);

	UPROPERTY()
	UInputAction* ClickMoveAction;

	UPROPERTY()
	UInputAction* DashAction;

	UPROPERTY()
	UInputMappingContext* TopDownMappingContext;

private:
	/** 헤드리스(-unattended) 자기이동 프로브. 서버 권위에서만 발동. */
	void RunHeadlessMoveProbe();

	/** 헤드리스(-unattended) 대쉬 프로브. 서버 권위에서만 발동. */
	void RunHeadlessDashProbe();

	FTimerHandle ProbeDashTimer;
	FVector ProbeDashStart = FVector::ZeroVector;

	FTimerHandle ProbeMoveTimer;
	FTimerHandle ProbeLogTimer;
	FVector ProbeTarget = FVector::ZeroVector;
};
