// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "REBulletPattern.h"
#include "REBossCharacter.generated.h"

class UREHealthBarComponent;

/**
 *  보스 폰. 탄막 패턴 발사 진입점을 가진다.
 *  ACharacter 직접 상속 — ARECharacterBase는 카메라 붐 달린 플레이어 폰이라 부적합.
 *  HP는 서버 권위(Replicated) — 데미지 적용은 TakeDamage HasAuthority 가드 경유 (RECharacterBase 동일 패턴).
 */
UCLASS()
class AREBossCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AREBossCharacter();

	/**
	 *  직선탄(Spiral/Fan) 1회 발사 (#84). 서버가 결정한 생성기 입력을 브로드캐스트하고
	 *  서버·클라가 이 같은 구현체에서 같은 탄을 만든다 — 서버도 로컬 실행되므로 직접 스폰 경로가 없다.
	 *  Reliable: 유실되면 그 발사분이 클라에 영영 안 보인다(회피 게임에서 치명적).
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_FireDirect(EBulletPattern Pattern, FVector_NetQuantize Origin,
	                          float AngleDeg, int32 Count, float ServerTime);

	/**
	 *  곡사탄(Artillery) 1회 일제사 (#84). Line/PlayerAimed가 먹는 조준점과
	 *  Random이 먹는 시드를 서버가 정해 보낸다 — 클라는 PhaseRng를 돌리지 않는다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_FireArtillery(EArtilleryShape Shape, FVector_NetQuantize Origin,
	                             FVector_NetQuantize AimLoc, int32 CallSeed, float ServerTime);

	/** 페이즈 로테이션 발사 시작. Seed는 서버 전용 PhaseRng 초기화용 — 네트워크 미전송 (#84). */
	void StartFiring(int32 Seed);
	/** 발사 정지. 이미 뜬 탄은 수명까지 유지(일괄 소멸 안 함). */
	void StopFiring();

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 현재 체력. 서버 권위, 클라 복제. 변경 시 OnRep_Health로 HP바 갱신. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. 비복제 — 서버/클라 모두 생성자에서 같은 ini(UREStatsSettings)를 읽고 런타임 변경 코드가 없다 (#73). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 100.f;

	/** 머리 위 HP바 (#29). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "UI", meta = (AllowPrivateAccess = "true"))
	UREHealthBarComponent* HealthBar;

	/** Health 복제 도착(클라) / 서버 직접 호출 공용 — HP바 갱신. */
	UFUNCTION()
	void OnRep_Health();

private:
	/** Spiral 호출마다 누적되는 시작각. 연속 트리거 시 링이 회전한다. */
	float SpiralBaseAngleDeg = 0.f;
	/** Spiral 호출당 BaseAngle 증가량(deg). 링 간격(22.5°)과 비정합 → 나선 팔이 휜다(#64). */
	static constexpr float SpiralRotationStepDeg = 137.5f;

	/**
	 *  클로즈드루프 스폰율(발사당 탄 수). 적분 제어 — 라이브 카운트가 목표에 못 미치면 램프업.
	 *  Mass는 히트 프로세서가 원점 근처 탄을 소멸시켜 피드포워드로는 목표 미달(#51).
	 *  -1 = 미초기화(첫 발사에 피드포워드 값으로 시딩).
	 */
	float SpiralSpawnRate = -1.f;
	/** 소수부 누산 — 발사당 정수 탄 수로 내림하되 소수부를 이월해 소형 타깃 양자화 오버슛 방지. */
	float SpiralSpawnAccum = 0.f;
	/** 누적 발사 횟수 — 첫 1수명(≈수명/발사주기 발) 동안은 적분 정지(피드포워드로 채우기)해 와인드업 방지. */
	int32 SpiralShotCount = 0;

	/** 서버 기준 현재 시각. GameState 미준비면 0. */
	float GetServerNow() const;
	/**
	 *  ServerTime 이후 경과초. 서버에서는 ≈0이라 보정이 자연히 무효화된다.
	 *  [0, 1] 로 양쪽 클램프한다 — 하한은 GameState 미복제 시 음수 폭주 방지,
	 *  상한은 클라 시각 추정치가 오래됐거나(EMA 수렴 중) 서버 재시작으로 어긋난 경우
	 *  볼리 전체가 조용히 스킵되어 N=0으로 렌더되는 것(원래 버그의 재현)을 막는다 (#84).
	 */
	float GetElapsedSince(float ServerTime) const;
	/** Spiral 발사당 탄 수. 클로즈드루프는 서버 ISM 상태에 의존 — 서버 전용 결정. */
	int32 ResolveSpiralCount();

	/** 사망 여부. 서버 전용 — 클라 시각처리는 스코프 밖이라 비복제. */
	bool bIsDead = false;

	//~ 패턴 로테이션 페이즈 스케줄러 (#64). 발사 주체 = Boss (M5 RPC 확장 대비).
	void BeginPhase();          // 다음 패턴 선택 + 발사 타이머 세팅 + 페이즈 종료 예약
	void FireCurrentPattern();  // 현재 페이즈 패턴 1회 발사 (FireTimer 콜백)
	void EndPhase();            // 발사 정지 + RestSec 뒤 BeginPhase 예약

	FRandomStream PhaseRng;
	EBulletPattern CurrentPhasePattern = EBulletPattern::Spiral;
	bool bFirstPhase = true;    // 첫 페이즈만 no-repeat 제약 예외 (무제약 랜덤 시작)
	FTimerHandle FireTimer;     // 페이즈 내 발사 반복
	FTimerHandle PhaseTimer;    // 페이즈 종료/대기 전환

	static constexpr float SpiralPhaseSec     = 5.f;
	static constexpr float FanPhaseSec        = 3.f;
	static constexpr float RestSec            = 1.f;
	static constexpr float FanFireIntervalSec = 0.5f;

	//~ 곡사(Artillery) 페이즈 파라미터. 헤더 상수 — 플레이 후 튜닝.
	static constexpr float ArtilleryPhaseSec     = 4.f;    // 페이즈 길이
	static constexpr float ArtilleryFireInterval = 1.8f;   // 일제사 간격(비행시간보다 길게 → 겹침 억제)
	static constexpr float ArtilleryFlightTime   = 1.5f;   // 회피 시간
	static constexpr float ArtilleryMaxHeight    = 400.f;  // 포물선 최대 고도
	static constexpr float ArtilleryRadius       = 120.f;  // 폭발/마커 반경
	static constexpr float ArtilleryDamage       = 15.f;
	static constexpr int32 ArtilleryCount        = 12;     // 일제사 착지점 수(모양별 기준)
	static constexpr float MarkerGroundOffset     = -88.f; // 캡슐 중심→바닥(착지 평면). 판정은 XY라 시각용.

	EArtilleryShape CurrentArtilleryShape = EArtilleryShape::Ring;

	/**
	 *  최근접 생존 플레이어 폰 (#85). 없으면 nullptr.
	 *  서버 결정 경로에서만 부른다 — 결과는 RPC 페이로드로 나가므로 RPC 계약은 안 바뀐다.
	 */
	const APawn* FindNearestLivingPlayerPawn() const;

	/** 현재 페이즈 Artillery 1회 일제사(FireCurrentPattern에서 분기). */
	void FireArtillery();
};
