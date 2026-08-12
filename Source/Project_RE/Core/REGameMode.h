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

protected:
	virtual void BeginPlay() override;

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

	/** 발사 시작 1회성 가드. */
	bool bFiringStarted = false;
	/** 준비 신호와 보스 스폰이 모두 끝났으면 발사 시작. 둘의 순서는 보장되지 않는다. */
	void TryStartBossFiring();
};
