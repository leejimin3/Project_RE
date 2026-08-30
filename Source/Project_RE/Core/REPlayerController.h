// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "REPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
class URECheatPanelWidget;
class UREHeadlessProbeComponent;

/**
 *  탑뷰 PlayerController. 우클릭으로 커서 아래 지점으로 폰을 이동시킨다.
 *  클릭 검출은 로컬, 이동 권위는 서버(Server_RequestMove RPC → NavMesh 패스팔로잉).
 *  IA/IMC는 uasset 없이 코드로 생성(transient).
 */
UCLASS()
class AREPlayerController : public APlayerController
{
	GENERATED_BODY()

	/**
	 *  헤드리스 검증 프로브가 Server_RequestMove / Server_RequestFire 를 호출한다 (#141).
	 *  **public 확대가 아니라 friend 를 쓰는 이유:** 테스트 하네스 하나 때문에 프로덕션
	 *  API 표면을 넓히면, 그 뒤로는 누구나 그 RPC 를 부를 수 있게 된다.
	 */
	friend class UREHeadlessProbeComponent;

public:
	/**
	 *  결과 화면 표시 + 입력 차단 (#40). 서버가 EndGame에서 호출, 오너 클라에서 실행.
	 *  싱글/리슨에서는 로컬 즉시 실행 — M4 데디 전환 시 수정 불필요.
	 */
	UFUNCTION(Client, Reliable)
	void Client_ShowResult(bool bVictory);

	/**
	 *  사망 통지 (#85). 입력만 차단하고 폰·카메라는 그대로 둔다 —
	 *  그 자리에서 동료 전투를 보는 것이 곧 관전 시점이다(별도 관전 카메라 없음).
	 */
	UFUNCTION(Client, Reliable)
	void Client_NotifyDeath();

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
	/** 폰이 살아있는가. 서버 RPC 가드용 — 클라 DisableInput은 지연·조작에 뚫린다 (#85). */
	bool IsPawnAlive() const;

	/** 클라 발사 페이싱 — 마지막 발사 요청 시각(월드초). 홀드 시 Triggered가 매 프레임 오는 것 억제. */
	double LastFireRequestTime = -1.0;

	/** 클라 이동 페이싱 — 마지막으로 서버에 보낸 이동 목표 (#126). 발사의 시간 페이싱에 대응하는 거리 페이싱. */
	FVector LastMoveRequest = FVector::ZeroVector;

	/**
	 *  발사 직후 커서 회전을 유지하는 구간의 종료 시각(월드초). -1 = 락 없음.
	 *  서버 StopMovement가 도달하기 전 남은 속도가 커서 회전을 이동 방향으로 덮는 것을 막는다 (#79).
	 */
	double FacingLockUntil = -1.0;

	/**
	 *  발사 후 이동이 잠기는 구간의 종료 시각(월드초). -1 = 락 없음. **서버 전용.**
	 *
	 *  사격 모션이 끝날 때까지 자리를 지키게 한다. 발사 시점의 StopMovement 만으로는
	 *  그 직후 클릭 한 번에 바로 다시 걸어나가 모션이 미끄러지듯 잘렸다.
	 *  클라에 두지 않는 이유: 이동 권위가 서버(Server_RequestMove → 패스팔로잉)에 있고
	 *  클라에는 이동 예측이 없다 — 서버가 거절하면 클라도 안 움직인다.
	 */
	double MoveLockUntil = -1.0;

	/** 락 구간에 매 틱 다시 세울 커서 방향 yaw (#79). */
	float FacingLockYaw = 0.f;

	/** 회전 구동 설정을 1회 적용하기 위한 폰 추적. 폰이 바뀌면 다시 적용한다 (#79). */
	TWeakObjectPtr<APawn> FacingPawn;

	/** 치트 패널 토글 (F1). 위젯 1회 생성 후 표시/숨김. */
	void OnToggleCheatPanel();

	UPROPERTY()
	URECheatPanelWidget* CheatPanel = nullptr;

	/** 플레이어 상태 HUD (#100). 로컬 컨트롤러에서만 만든다 — 데디서버엔 화면이 없다. */
	UPROPERTY()
	TObjectPtr<class UREPlayerHudWidget> PlayerHud = nullptr;
};
