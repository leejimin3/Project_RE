// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "REBulletPattern.h"
#include "REBossCharacter.generated.h"

class UREHealthBarComponent;
class UAnimSequence;
class UMaterialInstanceDynamic;

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
	 *  곡사탄(Artillery/ArtilleryStorm) 1회 일제사 (#84). Line/PlayerAimed가 먹는 조준점과
	 *  Random이 먹는 시드를 서버가 정해 보낸다 — 클라는 PhaseRng를 돌리지 않는다.
	 *  Pattern 은 어느 페이즈가 쐈는지다 — 체공/고도/발수가 여기서 갈린다. Shape 는 착지 모양만 쥔다.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_FireArtillery(EBulletPattern Pattern, EArtilleryShape Shape, FVector_NetQuantize Origin,
	                             FVector_NetQuantize AimLoc, int32 CallSeed, int32 SweepIdx, float ServerTime);

	/**
	 *  페이즈 시작 알림 (#130). 외관 램프와 예고 애님을 건다 — 코스메틱 전용이라
	 *  탄 생성에는 관여하지 않는다. 유실되면 그 페이즈 내내 이전 색으로 남으므로 Reliable.
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_BeginPhaseLook(EBulletPattern Pattern);

	/** 페이즈 로테이션 발사 시작. Seed는 서버 전용 PhaseRng 초기화용 — 네트워크 미전송 (#84). */
	void StartFiring(int32 Seed);
	/** 발사 정지. 이미 뜬 탄은 수명까지 유지(일괄 소멸 안 함). */
	void StopFiring();

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	//~ 코스메틱 초기화(MID 생성 + idle 재생). 데디 서버에서는 통째로 생략한다.
	virtual void BeginPlay() override;
	//~ 외관 램프 전용. 데디 서버에서는 BeginPlay가 틱을 켜지 않는다.
	virtual void Tick(float DeltaSeconds) override;

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

	//~ 장미 포락선(RoseEnvelope) 파라미터. 헤더 상수 — 플레이 후 튜닝.
	//  각은 균등 링이고 **속력만** 각도의 함수다: s(θ) = BulletSpeed·(1 + Amp·cos(k·θ + ω·t)).
	//  발사 T초 뒤 그 볼리의 파면이 r(θ) = BulletSpeed·T·(1 + Amp·cos(kθ + ω·t)) 인 k로브
	//  극좌표 곡선이 되어 자기닮음으로 확대된다 — 탄은 전부 직선이고 곡선인 것은 파면뿐이다.
	//  발사당 탄 수와 발사 간격은 Spiral과 공유한다(ResolveSpiralCount / FireIntervalSec):
	//  링 지오메트리가 같아 새 손잡이를 만들 이유가 없다. 동시 체공 = 48/0.15 × 15 ≈ 4,800발로
	//  Spiral과 같은 예산이고, 곡사와 달리 **착지가 없어** 프레임 비용이 무시할 수준이다.
	static constexpr float RosePhaseSec      = 8.f;    // 파면이 여러 겹 쌓여야 무늬가 성립한다
	/** 로브 수 k. 홀수라 로브가 정반대로 겹치지 않는다 — 무늬가 비대칭이 된다. 로브 각폭 72°. */
	static constexpr int32 RoseLobes         = 5;
	/**
	 *  속력 변조 깊이. 0.5면 속력이 기준의 0.5~1.5배(ini BulletSpeed=200 기준 100~300uu/s)라
	 *  플레이어 거리(600) 도달 시점 T≈3s 에 파면 반경이 300~900 — 로브 진폭 600uu 로 읽힌다.
	 *  1.0 이면 골의 속력이 0 이 되어 그쪽 탄이 보스 발밑에 눌어붙는다.
	 */
	static constexpr float RoseAmp           = 0.5f;
	/**
	 *  로브 위상 회전(deg/s). 로브 1주기(360/k = 72°)를 도는 데 1.8초.
	 *  링 회전(SpiralRotationStepDeg — 볼리마다 137.5°)과는 별개 축이다. 링 회전은 탄이 놓이는
	 *  각만 바꾸고 로브는 절대 각의 함수라, 두 회전이 맞물려 무늬가 정지하는 일이 없다.
	 */
	static constexpr float RoseSpinDegPerSec = 40.f;

	//~ 곡사(Artillery) 페이즈 파라미터. 헤더 상수 — 플레이 후 튜닝.
	static constexpr float ArtilleryPhaseSec     = 4.f;    // 페이즈 길이
	static constexpr float ArtilleryFireInterval = 1.8f;   // 일제사 간격(비행시간보다 길게 → 겹침 억제)
	static constexpr float ArtilleryFlightTime   = 1.5f;   // 회피 시간
	static constexpr float ArtilleryMaxHeight    = 400.f;  // 포물선 최대 고도
	static constexpr float ArtilleryRadius       = 120.f;  // 폭발/마커 반경
	static constexpr float ArtilleryDamage       = 15.f;
	static constexpr int32 ArtilleryCount        = 12;     // 일제사 착지점 수(모양별 기준)
	static constexpr float MarkerGroundOffset     = -88.f; // 캡슐 중심→바닥(착지 평면). 판정은 XY라 시각용.

	//~ 곡사 폭풍(ArtilleryStorm) 파라미터. 헤더 상수 — 플레이 후 튜닝.
	//  탄은 **한 발씩** 나가며 단일 나선을 안에서 밖으로 **한 번** 훑는다 — 페이즈 = 스윕 1회다.
	//  RPC 는 발사 주기마다 1회지만 그 안의 StormCount 발은 지난 주기 동안 한 발씩 나간 것으로
	//  취급한다(각자 다른 나선 위치 + 어긋난 비행 경과). 화면상 초당 80발 단발 사격이면서
	//  네트워크는 20 RPC/s 로 남는다 — 초당 80회 Reliable 멀티캐스트는 낼 수 없다.
	//  Count/Interval 은 '한 프레임에 몇 발이 생기느냐'를 정한다. 탄은 어긋난 Elapsed 덕에
	//  뭉침이 안 보이지만 **마커는 위치가 Target 이라 어긋내기가 안 먹어** 생성 단위가 그대로
	//  드러난다 — 한 번에 뜨는 마커 수가 곧 Count 다. 그래서 초당 발수를 유지한 채 Count 를
	//  낮추고 Interval 을 같은 비율로 줄였다(12/0.15 → 4/0.05).
	//  동시 체공 탄 = Count × Arms / FireInterval × FlightTime = 4×2/0.05 × 6 ≈ 960발.
	//  비용은 체공수가 아니라 착지율(Count/FireInterval = 80/s)이 쥔다 — 착지 프레임마다
	//  폭발 Niagara 1개 + TakeDamage 가 돈다. 체공시간을 늘리는 쪽이 밀도를 싸게 산다.
	//  Radius/Damage 는 Artillery 와 공유한다(별도 상수 안 둔다).
	static constexpr float StormPhaseSec      = 10.f;   // 스윕 1회 길이
	static constexpr float StormFireInterval  = 0.05f;  // 볼리 간격 = 생성 이벤트 주기(RPC 20/s)
	static constexpr float StormFlightTime    = 6.f;    // 체공. 동시 체공 탄에 그대로 비례한다
	/**
	 *  포물선 최대 고도. 궤적이 물리가 아니라 정규화 보간이라(REArcSimProcessor) 높이와
	 *  체공 시간은 완전히 독립이다 — 이 값을 바꿔도 착지 타이밍은 안 변한다.
	 *  800 은 탑다운 카메라(높이 1500) 위로 탄이 솟아 화면 밖으로 나갔다 — 절반으로 낮췄다.
	 */
	static constexpr float StormMaxHeight     = 400.f;
	static constexpr int32 StormCount         = 4;      // 볼리당 슬롯 수 = 단발 사격의 시간 해상도
	/**
	 *  나선 팔 수. 팔마다 360/Arms 도씩 각이 어긋난 같은 나선이 겹쳐 돈다.
	 *  체공 탄이 정확히 이 배수로 늘어나고, 착지율(=비용)도 같은 배수로 는다 —
	 *  착지마다 폭발 Niagara 1개 + TakeDamage 가 돌기 때문이다.
	 *  같은 슬롯의 팔들은 **동시** 발사다(비행 경과 어긋내기는 슬롯 단위).
	 */
	static constexpr int32 StormArms          = 2;
	/** 스윕 시작 반경. 0이면 첫 볼리의 발들이 보스 발밑 한 점에 겹친다. */
	static constexpr float StormMinRadius     = 150.f;
	/**
	 *  스윕 끝 반경. 바닥은 원점 중심 ±2000(윗면 Z=40, 실측)이고 보스도 원점이라
	 *  2000까지가 바닥 안이다 — 이 값은 그 안쪽이므로 모든 착지점이 바닥 위에 있다.
	 */
	static constexpr float StormMaxRadius     = 1500.f;
	/**
	 *  나선 간격 — 한 바퀴 돌 때 반경이 커지는 양(uu). 바퀴 수가 아니라 **간격**이 상수다:
	 *  바퀴 수를 고정하면 반경을 키울 때 간격이 같이 벌어져 나선이 통째로 성겨진다.
	 *  이 값이 곧 인접한 나선 띠 사이 거리다. 마커 지름(2×120=240)보다 작으면 인접 띠가
	 *  겹쳐 회피 통로가 사라진다 — 지금 값이 그렇다(의도된 선택: 밀도 우선).
	 */
	static constexpr float StormRadiusPerTurn = 200.f;
	/** 스윕 1회의 회전 바퀴 수. 간격에서 역산한다 — 반경을 바꿔도 나선의 촘촘함이 안 변한다. */
	static constexpr float StormSweepTurns    = (StormMaxRadius - StormMinRadius) / StormRadiusPerTurn;
	/**
	 *  스윕 1회의 볼리 수 = 페이즈당 발사 횟수. 첫 발사가 인트로 종료 시점이라 +1 이다
	 *  (BeginPhase 의 InFirstDelay 참조). 스윕 진행도 분모다.
	 */
	static constexpr int32 StormVolleyCount   = (int32)(StormPhaseSec / StormFireInterval) + 1;
	/** 스윕 1회의 총 발수. 나선 진행도의 분모다 — 탄 하나가 이 중 한 칸을 차지한다. */
	static constexpr int32 StormShotsPerSweep = StormVolleyCount * StormCount;

	EArtilleryShape CurrentArtilleryShape = EArtilleryShape::Ring;
	/** 폭풍 스윕 진행도의 분자. 서버 전용 — 클라는 페이즈를 안 돌리므로 페이로드로 받는다 (#84). */
	int32 StormVolleyIdx = 0;

	/**
	 *  최근접 생존 플레이어 폰 (#85). 없으면 nullptr.
	 *  서버 결정 경로에서만 부른다 — 결과는 RPC 페이로드로 나가므로 RPC 계약은 안 바뀐다.
	 */
	const APawn* FindNearestLivingPlayerPawn() const;

	/** 현재 페이즈 Artillery/ArtilleryStorm 1회 일제사(FireCurrentPattern에서 분기). */
	void FireArtillery();

	//~ 페이즈별 외관 (#118). 보스는 고정형이라 애님BP 없이 single-node로 재생한다 —
	//  Stone Golem은 자체 스켈레톤이라 ABP_Unarmed(플레이어 공유)가 붙지 않고,
	//  idle 하나면 충분해 리타겟할 이유가 없다.
	UPROPERTY() TObjectPtr<UAnimSequence> IdleAnim = nullptr;
	UPROPERTY() TObjectPtr<UAnimSequence> LeapAnim = nullptr;
	/** 바디 머티리얼 MID. 데디 서버에서는 생성하지 않으므로 nullptr — 외관 함수들이 자연 no-op. */
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BodyMID = nullptr;
	FTimerHandle LeapTimer;   // 도약 원샷 종료 → idle 복귀

	/** 바디 머티리얼의 한 상태. 팩 마스터가 노출한 파라미터가 그대로 필드다. */
	struct FBossLook
	{
		float Snow = 0.f;
		float Lava = 0.f;
		FLinearColor Emis = FLinearColor(1.f, 0.f, 0.f, 1.f);   // 팩 기본 MI 값
	};

	/** 패턴별 목표 외관. */
	static FBossLook LookForPattern(EBulletPattern Pattern);
	/** MID에 즉시 기록. */
	void ApplyLook(const FBossLook& Look);
	/**
	 *  패턴 외관으로 가는 램프를 시작한다. 같은 패턴이면 no-op이라
	 *  발사마다 불러도 진행 중인 램프를 되감지 않는다.
	 */
	void StartPatternLook(EBulletPattern Pattern);
	/** Artillery 페이즈 예고 — 도약 1회 재생 후 타이머로 idle 복귀. */
	void PlayLeap();
	/** idle 루프 재생. PlayAnimation이 single-node 모드 전환까지 겸한다. */
	void PlayIdle();

	FBossLook LookFrom;
	FBossLook LookTo;
	float LookAlpha = 1.f;                                  // 1 = 램프 종료(틱 조기 반환)
	EBulletPattern LookPattern = EBulletPattern::Spiral;    // 현재 목표 패턴

	//~ 팩 MI 프리셋에서 그대로 가져온 값 — MI_Stone_Golem_Inst1(Snow) / Inst2(Lava).
	static constexpr float FanSnowAmount       = 1.34f;
	static constexpr float ArtilleryLavaAmount = 2.47f;
	/** 폭풍 이미시브 — 용암은 Artillery 와 공유하므로 색으로 가른다(주황 vs 금색). */
	static constexpr float StormLavaAmount     = 2.47f;
	/**
	 *  페이즈 인트로 길이(s). 외관 램프 시간이자 첫 발사 지연이다.
	 *  도약 애님(0.47s)보다 길어 애님도 이 안에서 끝난다.
	 */
	static constexpr float LookIntroSec = 0.8f;
};
