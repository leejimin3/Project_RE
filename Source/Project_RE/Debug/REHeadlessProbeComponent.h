// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "REHeadlessProbeComponent.generated.h"

/**
 *  헤드리스 검증 프로브 (#77 / #82 / #112).
 *
 *  PIE 없이 `-game -nullrhi -unattended` 로 게임루프를 관측한다.
 *  이동 → 발사 → 대쉬를 순차 실행하고 각 단계 결과를 로그로 남기면,
 *  scripts/dedi-verify.ps1 이 그 로그를 판정해 PASS/FAIL 을 낸다.
 *
 *  **로그 문자열이 곧 판정 계약이다.** 메시지를 바꾸면 스크립트가 실패가 아니라
 *  **무판정**이 된다 — 조용히 초록불이 뜬다.
 *
 *  프로덕션 컨트롤러에서 분리한 이유 (#141):
 *  전에는 AREPlayerController 안에 135줄(그 파일의 19%)로 들어 있었고 게이트가
 *  런타임 하나(FApp::IsUnattended)뿐이었다. `-unattended` 는 쉬핑에서도 커맨드라인으로
 *  넘길 수 있으므로 **출시 빌드에서 프로브가 켜진다.** 이제 본문이 쉬핑에서 컴파일
 *  아웃되어 그 경로 자체가 없다.
 *
 *  **UHT 주의:** UCLASS 는 항상 컴파일한다. `#if !UE_BUILD_SHIPPING` 으로 감싸는 것은
 *  멤버와 함수 본문뿐이다 — GENERATED_BODY() 를 조건부 블록에 넣으면 UHT 가 깨진다.
 *  쉬핑에서는 컴포넌트가 존재하되 StartProbes() 가 빈 함수가 된다.
 */
UCLASS(ClassGroup = (RE), meta = (BlueprintSpawnableComponent))
class UREHeadlessProbeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 프로브 3종 순차 시작. 소유 액터는 AREPlayerController 여야 한다. */
	void StartProbes();

private:
#if !UE_BUILD_SHIPPING
	/** 헤드리스 자기이동 프로브. 서버 권위에서만 발동. */
	void RunMoveProbe();
	/** 헤드리스 발사 프로브. 서버 권위에서만 발동. */
	void RunFireProbe();
	/** 헤드리스 대쉬 프로브. 서버 권위에서만 발동. */
	void RunDashProbe();

	FTimerHandle ProbeMoveTimer;
	FTimerHandle ProbeLogTimer;
	FTimerHandle ProbeFireTimer;
	FTimerHandle ProbeDashTimer;
	FVector      ProbeTarget    = FVector::ZeroVector;
	FVector      ProbeDashStart = FVector::ZeroVector;
#endif
};
