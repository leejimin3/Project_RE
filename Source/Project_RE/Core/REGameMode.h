// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MassEntityTypes.h"
#include "REGameMode.generated.h"

class AREBossCharacter;

/**
 *  Mass 스모크 테스트용 throwaway 프래그먼트.
 *  M0 #2 프로브 전용 — M1에서 실제 탄막 프래그먼트로 교체·이동한다.
 */
USTRUCT()
struct FRETestFragment : public FMassFragment
{
	GENERATED_BODY()

	int32 Dummy = 0;
};

/**
 *  탑뷰 게임모드. 기본 폰/컨트롤러를 RE 클래스로 지정.
 *  BeginPlay에서 Mass 엔티티 1개를 생성해 서브시스템 가동을 실증한다.
 *  GameModeBase는 서버에만 존재 → 모든 로직이 곧 서버 권위(별도 가드 불필요).
 */
UCLASS()
class AREGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AREGameMode();

	/**
	 *  승패 확정 (#40). 탄막 발사·자동사격 정지 + 결과 화면 표시.
	 *  GameModeBase는 서버에만 존재 → 이 함수 자체가 서버 권위. 중복 호출은 선착순 무시.
	 */
	void EndGame(bool bVictory);

	/** 승패 확정 여부 — 게임오버 후 잔여 발사 RPC 무시용 (REPlayerController가 조회). */
	bool IsGameOver() const { return bGameOver; }

	/** 클라 준비 통지 수신 (#84/#85). 보스 발사 시작 조건을 재평가한다. */
	void NotifyPlayerReady(APlayerController* PC);

	/** 플레이어 사망 통지 (#85). 전원 사망이면 EndGame(DEFEAT)까지 간다. */
	void NotifyPlayerDied(APlayerController* PC);

	/**
	 *  서버측 헤드리스 프로브 완주 통지 (#87). 전원 완주 시 프로세스를 종료한다.
	 *  종료 결정이 개별 PC가 아니라 여기 있는 이유: PC는 자기 프로브만 알기 때문에,
	 *  먼저 끝난 하나가 서버를 내리면 뒤 클라의 프로브는 시작조차 못 한다.
	 */
	void NotifyProbeComplete();

protected:
	virtual void BeginPlay() override;

	virtual void Logout(AController* Exiting) override;

	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer,
	                                                          const FTransform& SpawnTransform) override;

private:
	UPROPERTY()
	TObjectPtr<AREBossCharacter> DemoBoss = nullptr;

	/** 승패 확정 여부. 같은 프레임에 양쪽이 죽는 경우 선착순 처리. */
	bool bGameOver = false;

	/**
	 *  준비를 알린 PC 집합 (#85). Num()이 곧 실제 접속자 수라 승패 판정 분모로도 쓴다.
	 *  int32 카운터가 아니라 집합인 이유: 클라가 Server_NotifyReady를 두 번 보내도
	 *  수가 부풀지 않는다. 카운터였다면 연타 한 번에 게이트가 뚫린다.
	 */
	UPROPERTY()
	TSet<TObjectPtr<APlayerController>> ReadyPlayers;

	/** 사망한 PC 집합 (#85). ReadyPlayers를 채우면 전원 사망 = 패배. */
	UPROPERTY()
	TSet<TObjectPtr<APlayerController>> DeadPlayers;

	/**
	 *  스폰된 폰 수 = 다음 스폰의 오프셋 인덱스 (#85).
	 *  감소시키지 않는다 — 나갔다 들어오면 오프셋이 바깥으로 밀리지만, 감소시키면 두 플레이어가
	 *  같은 인덱스를 받아 겹칠 수 있다. 겹침이 드리프트보다 나쁘다(#54).
	 */
	int32 SpawnedPawnCount = 0;
	/** 플레이어 간 이격 거리(uu). 캡슐 반경 대비 넉넉히. */
	static constexpr float SpawnSpacing = 250.f;

	/** 발사 시작 1회성 가드. */
	bool bFiringStarted = false;

	/**
	 *  완주한 서버측 프로브 수 (#87). ReadyPlayers처럼 TSet이 아니라 카운터인 이유:
	 *  프로브 완주는 서버 자신의 타이머가 컨트롤러당 정확히 1회 발화시키므로 중복 경로가 없다.
	 */
	int32 CompletedProbes = 0;

	/** 준비 신호와 보스 스폰이 모두 끝났으면 발사 시작. 둘의 순서는 보장되지 않는다. */
	void TryStartBossFiring();
};
