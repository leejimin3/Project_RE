// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "REPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
class URECheatPanelWidget;

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

	/** 오너 클라 회전 구동 (#79). 서버에서는 아무 것도 하지 않는다 — CMC가 담당. */
	virtual void PlayerTick(float DeltaTime) override;

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

	/** 좌클릭 핸들러: 홀드 연사 — 커서 방향을 로컬 페이싱 후 서버로 발사 요청 */
	void OnFire(const FInputActionValue& Value);

	/** 발사 요청 서버 RPC. 서버가 rate limit 재검증 후 정지·회전·히트스캔. */
	UFUNCTION(Server, Reliable)
	void Server_RequestFire(FVector Dir);

	/**
	 *  로컬 클라 준비 통지 (#84). 서버는 이 신호를 받고 보스 발사를 시작한다.
	 *  PostLogin이 아니라 클라발인 이유: PostLogin은 서버측 PC 생성 시점이라
	 *  클라 월드가 아직 Multicast를 받을 준비가 안 됐을 수 있다.
	 */
	UFUNCTION(Server, Reliable)
	void Server_NotifyReady();

	UPROPERTY()
	UInputAction* ClickMoveAction;

	UPROPERTY()
	UInputAction* DashAction;

	UPROPERTY()
	UInputAction* FireAction;

	UPROPERTY()
	UInputMappingContext* TopDownMappingContext;

	UPROPERTY()
	UInputAction* CheatPanelAction;

private:
	/** 헤드리스(-unattended) 자기이동 프로브. 서버 권위에서만 발동. */
	void RunHeadlessMoveProbe();

	/** 헤드리스(-unattended) 대쉬 프로브. 서버 권위에서만 발동. */
	void RunHeadlessDashProbe();

	/** 클라 발사 페이싱 — 마지막 발사 요청 시각(월드초). 홀드 시 Triggered가 매 프레임 오는 것 억제. */
	double LastFireRequestTime = -1.0;

	/**
	 *  발사 직후 커서 회전을 유지하는 구간의 종료 시각(월드초). -1 = 락 없음.
	 *  서버 StopMovement가 도달하기 전 남은 속도가 커서 회전을 이동 방향으로 덮는 것을 막는다 (#79).
	 */
	double FacingLockUntil = -1.0;

	/** 락 구간에 매 틱 다시 세울 커서 방향 yaw (#79). */
	float FacingLockYaw = 0.f;

	/** 회전 구동 설정을 1회 적용하기 위한 폰 추적. 폰이 바뀌면 다시 적용한다 (#79). */
	TWeakObjectPtr<APawn> FacingPawn;

	/** 헤드리스(-unattended) 발사 프로브. 서버 권위에서만 발동. */
	void RunHeadlessFireProbe();

	FTimerHandle ProbeFireTimer;

	FTimerHandle ProbeDashTimer;
	FVector ProbeDashStart = FVector::ZeroVector;

	FTimerHandle ProbeMoveTimer;
	FTimerHandle ProbeLogTimer;
	FVector ProbeTarget = FVector::ZeroVector;

	/** 치트 패널 토글 (F1). 위젯 1회 생성 후 표시/숨김. */
	void OnToggleCheatPanel();

	UPROPERTY()
	URECheatPanelWidget* CheatPanel = nullptr;
};
