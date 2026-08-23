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

	/**
	 *  화면에 표시할 현재 패턴. 외관 상태(LookPattern)를 그대로 쓴다 — 그 값이 페이즈 시작
	 *  방송과 발사 RPC 양쪽에서 갱신되므로 **클라도 새 복제 없이 이미 알고 있다** (#84).
	 *  데디 서버에서는 BodyMID 가 없어 갱신되지 않지만 HUD 는 클라 전용이라 무관하다.
	 *  페이즈 사이 Rest 구간에는 직전 패턴이 남는다 — 보스가 Rest 를 방송하지 않는다.
	 */
	EBulletPattern GetDisplayPattern() const { return LookPattern; }

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

	/**
	 *  패턴별 발사 간격(s). **발사 간격의 단일 출처다.**
	 *  페이즈 타이머(BeginPhase)와 볼리 내부 어긋내기(Multicast_FireDirect_Implementation)가
	 *  같은 값을 봐야 하는데, 후자는 클라에서도 돌아 페이즈 지역변수를 볼 수 없다.
	 *  두 곳에 각각 스위치를 두면 패턴을 늘릴 때 한쪽만 고쳐 조용히 어긋난다.
	 */
	static float FireIntervalFor(EBulletPattern P);

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

	/**
	 *  패턴 지오메트리의 바깥 한계(uu). 바닥이 원점 중심 정사각 ±2000(실측)이라 이 값이면
	 *  축 방향으로 가장자리 바로 안쪽이다. **모서리(2828)는 원형 패턴으로는 구조적으로
	 *  못 덮는다** — 덮으려면 탄이 바닥 밖으로 나가야 한다.
	 *  범위를 쥔 상수를 여기 하나로 모은다 — 패턴마다 매직넘버로 흩어져 있으면
	 *  "필드를 다 덮는가"를 한눈에 확인할 수 없다.
	 */
	static constexpr float ArenaRadius = 1900.f;

	/**
	 *  볼리 안에서 탄이 죽는 시각을 벌리는 폭(s). 0 이면 한 볼리가 **같은 프레임에 통째로
	 *  증발한다** — 속력이 제각각인 패턴(장미·심장형·블룸)에서 파면 한 줄이 한 번에 사라져
	 *  눈에 거슬린다. 인덱스에 비례해 수명을 깎아 흩어지며 사라지게 한다.
	 */
	static constexpr float VolleyDeathSpreadSec = 0.8f;

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

	//~ 심장형 조준(Cardioid) 파라미터.
	//  FireDirect 페이로드의 각은 하나뿐이고 그 자리를 **조준각**이 쓴다. 링 자체 회전은
	//  상수·ServerTime 에서 순수 유도하므로 실을 필요가 없다 (#84).
	static constexpr float CardioidPhaseSec = 7.f;
	static constexpr int32 CardioidCount    = 64;
	/** 변조 깊이. 1.0 이면 반대편 속력이 0 이라 탄이 보스 발밑에 눌어붙는다. */
	static constexpr float CardioidAmp      = 0.6f;
	/** 링 회전(deg/s). 링 간격(360/64 = 5.625°)과 비정합이라 방사 스포크가 안 생긴다. */
	static constexpr float CardioidRingSpinDegPerSec = 63.f;

	//~ 곡사 리사주(LissajousStorm) 파라미터.
	//  볼리 하나가 매듭 전체를 그린다 — 이 패턴의 정체성은 밀도가 아니라 **바닥에 그려지는
	//  수학 곡선**이고, 그러려면 동시에 떠 있는 매듭 겹수가 적어야 한다.
	//  겹수 = FlightTime / FireInterval 이다. 0.2/4.0 은 20겹이라 회전한 매듭들이 상자를
	//  통째로 메워 둥근 사각형 덩어리가 됐다 — 실제로 그랬다. 5겹으로 낮춘다.
	//  비용도 같이 내려간다: 착지율 60/0.5 = 120/s 로 폭풍(160/s, 9.23ms)보다 싸다.
	//  착지 프레임마다 도는 폭발 Niagara 가 이 패턴 비용의 대부분이었다(Draws 1723).
	static constexpr float LissaPhaseSec     = 9.f;
	/**
	 *  아레나 전체(Extent 1900)를 덮으려고 곡선이 길어져 표본이 60→150 으로 늘었고,
	 *  착지율이 120→300/s 로 뛰어 15.17ms(게이트 16.6)가 됐다. 간격으로 되돌린다:
	 *  150/1.0 = 150/s. 겹수는 2.5 라 곡선 가독성도 유지된다.
	 */
	static constexpr float LissaFireInterval = 1.0f;
	static constexpr float LissaFlightTime   = 2.5f;
	static constexpr float LissaMaxHeight    = 350.f;
	/**
	 *  곡선 위 표본 수. 이 값이 곧 매듭의 해상도다 — 점 간격이 마커 지름(2×120=240)보다
	 *  커지면 곡선이 끊긴 점 무더기로 보인다. 30 으로는 실제로 그랬다.
	 */
	static constexpr int32 LissaCount        = 150;
	/**
	 *  매듭의 반폭(uu). 아레나 반경 2000 안쪽이면 착지점이 바닥 위이긴 하나 그것만으론 부족하다 —
	 *  1100 은 화면(가로 약 3000uu)에 다 안 들어와 무늬가 잘렸다. 750 이면 통째로 보이고
	 *  같은 표본 수로 점 간격도 좁아진다.
	 */
	static constexpr float LissaExtent       = ArenaRadius;
	/**
	 *  폭발·마커 반경(uu). Artillery 의 120 을 그대로 쓰면 마커 지름(240)이 매듭의 로브
	 *  간격(≈E/2 = 375)에 육박해 안쪽이 메워지고 무늬가 둥근 사각형 덩어리로 보인다 —
	 *  실제로 그랬다. 곡선이 읽히려면 **선 굵기가 로브 간격보다 충분히 얇아야** 한다.
	 *  덤으로 마커 면적이 1/4 이 되어 Draw 오버드로도 같이 내려간다.
	 */
	static constexpr float LissaRadius       = 60.f;
	/** 서로소라야 곡선이 닫힌 매듭이 된다. 3:2 는 가장 읽기 쉬운 매듭이다. */
	static constexpr int32 LissaFreqX        = 3;
	static constexpr int32 LissaFreqY        = 2;
	/** 위상 회전(deg/s) — 매듭이 통째로 꿈틀거린다. */
	static constexpr float LissaDeltaDegPerSec = 25.f;

	//~ 곡사 소용돌이(BezierVortex) 파라미터.
	//  착지점은 보스 둘레 링이고, 2차 베지어 제어점을 Start→Target 의 **접선**으로 밀어
	//  탄이 직선 대신 옆으로 크게 휘감아 들어간다. 착지 시각·착지점·마커는 제어점과
	//  무관하므로(끝점 고정) 회피 규칙은 일반 곡사와 같고 화면만 3D 소용돌이가 된다.
	static constexpr float VortexPhaseSec     = 9.f;
	/**
	 *  발수를 링 둘레에 맞춰 52로 올리면서 착지율이 180→260/s 로 뛰어 13.72ms 가 됐다 —
	 *  게이트 16.6 에 17%%밖에 안 남는다. 간격으로 착지율을 되돌린다(52/0.28 = 186/s).
	 */
	static constexpr float VortexFireInterval = 0.28f;
	static constexpr float VortexFlightTime   = 3.5f;
	/**
	 *  볼리당 발수 = 착지 링의 점 수. 링 둘레 2π·900 을 이 값으로 나눈 간격이 마커 지름(240)
	 *  보다 작아야 링이 끊기지 않는다 — 36 이면 157uu 다.
	 */
	static constexpr int32 VortexCount        = 52;
	//~ 링 반경이 톱니로 팽창한다 — 고정 링이면 그 띠 밖은 영영 안전하다.
	//  AerialDome 과 같은 수법이고 위상을 ServerTime 에서 뽑으므로 페이로드가 필요 없다.
	static constexpr float VortexMinRadius   = 400.f;
	static constexpr float VortexMaxRadius   = ArenaRadius;
	static constexpr float VortexExpandSec   = 5.f;
	/** 접선 오프셋(uu). 반경과 비슷한 크기라야 휘감김이 화면에서 읽힌다. */
	static constexpr float VortexSwirl        = 900.f;
	/** 링 회전(deg/s) — 볼리마다 착지 링이 돌아 소용돌이가 이어진다. */
	static constexpr float VortexSpinDegPerSec = 55.f;
	//~ 고도를 탄마다 어긋내 층을 만든다. 궤적이 정규화 보간이라 높이를 바꿔도 착지 타이밍은
	//  안 변한다 — 층이 생겨도 링은 동시에 착지한다.
	//  800 은 탑다운 카메라(높이 1500) 위로 솟아 화면 밖으로 나갔다 — 폭풍이 같은 이유로
	//  800→400 을 겪었다(StormMaxHeight 주석). 곡사 기준 고도인 400 을 상한으로 맞춘다.
	static constexpr float VortexMinHeight = 150.f;
	static constexpr float VortexMaxHeight = 400.f;

	//~ 곡선 블룸 3종(StarBloom / LemniscateBloom / SuperformulaBloom) 공용 파라미터.
	//  탄을 곡선 위에 스폰하고 속도를 위치 벡터에 비례시킨다 → 도형이 자기닮음으로 부푼다.
	//  회전·변태 위상은 전부 ServerTime 에서 순수 유도하므로 페이로드에 실을 값이 없다 (#84) —
	//  FireDirect 의 각 자리는 안 쓴다(0을 보낸다). 발사 간격은 Spiral 과 공유한다.
	/**
	 *  초당 배율 증가량(1/s). T초 뒤 도형이 (1 + ScaleRate·T) 배가 된다.
	 *  수명과 짝지어 **소멸 반경**을 정한다: R₀·(1 + Rate·Life). 아래 값들은 그 곱이
	 *  아레나 반경(2000) 언저리가 되도록 잡혀 있다.
	 */
	static constexpr float BloomScaleRate = 1.7f;
	/**
	 *  블룸 전용 수명(s). ini 기본(15초)을 쓰면 도형이 4,400uu 까지 부풀어 **대부분의 시간을
	 *  맵 밖에서 보낸다** — 화면에는 이미 가장자리를 지나간 잔해만 남는다.
	 *  7초면 165·(1+1.7·7) = 2,100uu 로 아레나 가장자리에 닿으며 소멸한다.
	 */
	static constexpr float BloomLifetime = 7.f;
	/**
	 *  도형 회전(deg/s). 커버리지를 만드는 것이 이 값이다: 반경 r 의 탄은 (r/R₀−1)/Rate 초 전에
	 *  발사됐으므로 바깥일수록 더 돌아가 있고, 팔이 나선으로 감긴다(장미와 같은 원리).
	 *  수명 7초 × 55°/s = 385° 라 한 바퀴를 넘겨 가장자리에 각도 구멍이 남지 않는다.
	 */
	//~ 도형 회전(deg/s) — 도형마다 다르다. 두 요구가 반대로 당긴다:
	//    · 너무 빠르면 겹쳐 보이는 복사본이 제각기 다른 각으로 누워 도형이 뭉갠다.
	//      (55°/s 는 수명 7초 동안 385°를 훑어 실제로 통째로 뭉갰다.)
	//    · 너무 느리면 도형의 각도 구멍이 제자리에 남아 거기 선 플레이어가 영영 안전하다.
	//  **k겹 대칭 도형은 수명 동안 360/k 만 훑으면 구멍이 닫힌다** — 그 최소치 언저리를 쓴다.
	/** {7/3} 별은 7겹 대칭 → 51° 면 된다. 12×7 = 84°. */
	static constexpr float StarBloomSpinDegPerSec = 12.f;
	/** ∞ 는 **2겹** 대칭이라 180°가 필요하다. 27×7 = 189°. 여기만 유독 빠른 이유가 이것이다. */
	static constexpr float LemniSpinDegPerSec     = 27.f;
	/** 초공식은 m=4~8 이라 최소 4겹 → 90°. 14×7 = 98°. */
	static constexpr float SuperSpinDegPerSec     = 14.f;
	/**
	 *  블룸 전용 발사 간격(s). **이 패턴이 읽히느냐를 이 값이 혼자 정한다.**
	 *  동시에 보이는 복사본 사이의 반경 간격이 R₀·ScaleRate·이 값이고, 그게 탄 지름(70uu)보다
	 *  작으면 이웃 복사본이 서로 겹쳐 도형이 통째로 뭉갠다.
	 *  Spiral 과 공유하던 0.15 는 165·1.7·0.15 = 42uu 라 지름보다 작았다 — 실제로 뭉갰다.
	 *  0.4 면 112uu 로 분리된다. 수명 7초 동안 17겹이 고른 간격으로 퍼진다.
	 */
	static constexpr float BloomFireInterval = 0.4f;

	static constexpr float StarBloomPhaseSec = 8.f;
	/** 별 다각형 {N/Skip}. gcd(7,3)=1 이라 한붓그리기로 닫힌다. */
	static constexpr int32 StarBloomVerts    = 7;
	static constexpr int32 StarBloomSkip     = 3;
	/** 변당 표본 수. 꼭짓점만 찍으면 각진 변이 사라져 별이 안 읽힌다. 총 발수 = Verts × 이 값. */
	static constexpr int32 StarBloomSegPerEdge = 40;
	static constexpr float StarBloomRadius   = 165.f;

	static constexpr float LemniPhaseSec = 8.f;
	static constexpr int32 LemniCount    = 200;
	/** 렘니스케이트 반폭(uu). u=0 에서 x=A 라 이 값이 그대로 반폭이다. */
	static constexpr float LemniA        = 165.f;

	static constexpr float SuperPhaseSec = 9.f;
	static constexpr int32 SuperCount    = 240;
	static constexpr float SuperRadius   = 165.f;
	/** n1 이 작을수록 뾰족하다. 0.3 은 별처럼 각이 선다. */
	static constexpr float SuperN1       = 0.3f;
	static constexpr float SuperN2       = 1.7f;
	static constexpr float SuperN3       = 1.7f;
	//~ m 은 대칭 가지 수다. 정수가 아니어도 정의되므로 연속으로 움직여 모양을 변태시킨다.
	//  범위를 넓게 잡으면 중간에 비대칭 찌그러진 모양을 지나 난잡해 보인다 — 4~8 로 좁힌다.
	static constexpr float SuperMMin     = 4.f;
	static constexpr float SuperMMax     = 8.f;
	/** m 왕복 주기(s). 페이즈(9초)보다 짧아야 한 페이즈 안에서 변태가 보인다. */
	static constexpr float SuperMorphPeriodSec = 6.f;

	//~ 곡사 3종(RoseField / AerialDome / Spirograph).
	//  셋 다 **바닥에 그래프를 그리고** 3D 비행으로 그 위에 내려앉는다. 곡선 반경은 아레나
	//  반경 2000 안쪽 최대인 1900 으로 맞춰 맵 전체를 덮는다.
	//
	//  표본 수는 곡선 길이 / 마커 지름(2×120=240) 으로 정한다 — 간격이 지름보다 크면
	//  곡선이 끊긴 점 무더기로 보인다(LissajousStorm 에서 겪었다).
	//
	//  3차 베지어 정점은 H + 0.75·Rise 다. 곡사 기준 고도 400 을 넘지 않게 역산했다 —
	//  더 높이면 탑다운 카메라(1500) 밖으로 솟아 화면에서 사라진다.

	//~ **겹수(체공/발사간격)를 1로 유지하는 것이 이 두 패턴의 생명이다.**
	//  블룸은 복사본이 반경으로 벌어져 여러 겹이 겹쳐도 읽혔지만, 여기는 복사본이 전부
	//  같은 크기라 회전만 다른 채 직접 포개진다 — 0.4/2.5(6겹)로 뒀더니 곡선이 통째로
	//  뭉개져 마커 덩어리가 됐다(실제로 그랬다).
	//  겹이 하나면 회전 뭉갬 자체가 없으므로 회전은 오히려 크게 줘도 된다 — 볼리마다
	//  곡선이 크게 돌아앉아 시간으로 맵을 덮는다.
	static constexpr float RoseFieldPhaseSec     = 9.f;
	static constexpr float RoseFieldFireInterval = 1.2f;
	/** 회피 예고 시간. Artillery(1.5)와 같게 둔다 — 곡선을 읽고 움직일 시간이다. */
	static constexpr float RoseFieldFlightTime   = 1.5f;
	/** 곡선 위 표본 수. 장미 둘레(≈18,000uu) / 마커 지름(240) 을 넘겨야 곡선이 이어진다. */
	static constexpr int32 RoseFieldCount        = 110;
	static constexpr float RoseFieldRadius       = ArenaRadius;
	/** 꽃잎 계수. 짝수라 꽃잎이 2×K = 4장 나온다. */
	static constexpr int32 RoseFieldPetals       = 2;
	/** 볼리당 48° 회전. 꽃잎 주기(360/4 = 90°)를 두 볼리에 덮는다. */
	static constexpr float RoseFieldSpinDegPerSec = 40.f;
	static constexpr float RoseFieldMaxHeight    = 150.f;
	/** 감아 도는 하강. 정점 = 150 + 0.75·340 = 405. */
	static constexpr float RoseFieldRise         = 340.f;
	static constexpr float RoseFieldSwirl        = 600.f;

	static constexpr float DomePhaseSec     = 9.f;
	static constexpr float DomeFireInterval = 0.4f;
	static constexpr float DomeFlightTime   = 3.f;
	static constexpr int32 DomeCount        = 44;
	//~ 링 반경이 톱니로 팽창한다 — 고정 링이면 가운데와 바깥이 영영 안전하다.
	//  주기마다 안쪽에서 다시 시작해 '퍼지는 충격파'로 읽힌다.
	static constexpr float DomeMinRadius    = 350.f;
	static constexpr float DomeMaxRadius    = ArenaRadius;
	static constexpr float DomeExpandSec    = 4.f;
	static constexpr float DomeSpinDegPerSec = 15.f;
	static constexpr float DomeMaxHeight    = 100.f;
	/** 정점 = 100 + 0.75·400 = 400. */
	static constexpr float DomeRise         = 400.f;

	//~ RoseField 와 같은 이유로 겹수를 1로 맞춘다(위 주석 참조).
	static constexpr float SpiroPhaseSec     = 9.f;
	static constexpr float SpiroFireInterval = 1.2f;
	static constexpr float SpiroFlightTime   = 1.5f;
	/** 로제트는 고리가 겹쳐 곡선이 길다 — 장미보다 표본을 더 준다. */
	static constexpr int32 SpiroCount        = 150;
	static constexpr float SpiroRadius       = ArenaRadius;
	//~ 하이포트로코이드 파라미터. 서로소(5,3)라 도형이 닫히고, D 가 크면 고리가 깊어진다.
	static constexpr int32 SpiroBigR         = 5;
	static constexpr int32 SpiroSmallR       = 3;
	static constexpr float SpiroD            = 5.f;
	/** 볼리당 66° 회전 — 로제트는 대칭 차수가 높아 조금만 돌려도 새 각을 덮는다. */
	static constexpr float SpiroSpinDegPerSec = 55.f;
	static constexpr float SpiroMaxHeight    = 150.f;
	/** 정점 = 150 + 0.75·330 = 397. */
	static constexpr float SpiroRise         = 330.f;
	/** S자 진폭. 이웃 탄끼리 부호가 뒤집혀 이 값의 2배만큼 벌어졌다 만난다. */
	static constexpr float SpiroSwing        = 700.f;

	//~ 마이크로 미사일(MicroMissile) 파라미터.
	//  곡사 베지어 4발이 한 볼리다. **유도가 아니다** — 발사 순간의 플레이어 위치를 목표로
	//  잡고 궤적이 그 자리에서 완전히 결정된다. 목표점은 곡사 RPC 가 이미 싣고 다니는
	//  AimLoc 이라 새 페이로드가 필요 없다 (#84).
	//  네 발은 각각 동/서/남/북 윗대각선으로 먼저 뻗었다가 꺾여 들어간다 — 초기 방향이
	//  목표와 무관하므로 어디로 도망쳐도 네 방향에서 동시에 온다.
	//  발수가 4뿐이라 밀도 패턴이 아니라 **정밀 회피** 패턴이다. 14개 중 유일하게 성긴 쪽이다.
	static constexpr float MicroPhaseSec     = 7.f;
	static constexpr float MicroFireInterval = 0.5f;
	/** 체공(s). "빠르게 공격"이 요구라 예고 시간을 Artillery(1.5)보다 짧게 잡는다. */
	static constexpr float MicroFlightTime   = 1.1f;
	static constexpr int32 MicroCount        = 4;      // 동/서/남/북 — 축 수와 같아야 한다
	static constexpr float MicroMaxHeight    = 100.f;
	/** 꺾이기 전 나침반 방향으로 뻗는 거리(uu). 작으면 그냥 포물선처럼 보인다. */
	static constexpr float MicroOutDist      = 900.f;
	//~ 정점 = MaxHeight + 0.375·(Rise1+Rise2) = 100 + 300 = 400. 곡사 기준 고도를 안 넘는다.
	static constexpr float MicroRise1        = 550.f;  // 출발 쪽 — 윗대각선을 만드는 값
	static constexpr float MicroRise2        = 250.f;  // 착지 쪽 — 급강하 각을 쥔다
	/**
	 *  네 발의 착지점을 플레이어 기준으로 자기 발사 방향만큼 벌리는 거리(uu).
	 *  0 이면 네 마커가 한 점에 완전히 포개져 한 개로 보인다 — 4발인 게 안 읽힌다.
	 */
	static constexpr float MicroSpread       = 80.f;
	/** 폭발·마커 반경. "마이크로"라 Artillery(120)보다 작게 잡는다. */
	static constexpr float MicroRadius       = 90.f;

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
	/**
	 *  체공(s). 6 은 탄이 너무 느리게 떨어졌다 — 3 으로 줄여 낙하가 빨라진다.
	 *  동시 체공 탄이 여기에 정비례하므로 밀도가 절반이 된다(960 → 480). 착지율은 안 변해
	 *  프레임 비용도 그대로다 — 밀도를 되찾으려면 Arms 를 올려야 하고 그건 비용이 붙는다.
	 */
	static constexpr float StormFlightTime    = 3.f;
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
	static constexpr float StormMaxRadius     = ArenaRadius;
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
	//~ 패턴이 9개라 질감 축(Snow/Lava)만으로는 안 갈린다. **(질감, 이미시브 색) 쌍**이
	//  유일하도록 배분한다 — LookForPattern 의 각 case 가 그 쌍 하나씩을 집는다.
	static constexpr float CardioidSnowAmount    = 1.34f;
	//~ 패턴이 16개다. 질감 3종 × 색으로도 슬슬 빠듯하다 — 새 패턴을 더 늘리면 외관 축을
	//  하나 더 찾아야 한다(스케일·회전·발광 주기 등). 지금은 (질감, 색) 쌍 유일성으로 버틴다.
	static constexpr float RoseFieldLavaAmount   = 2.47f;
	static constexpr float DomeSnowAmount        = 1.34f;
	static constexpr float SpiroLavaAmount       = 2.47f;
	static constexpr float MicroSnowAmount       = 1.34f;
	static constexpr float LissaLavaAmount       = 2.47f;
	static constexpr float VortexSnowAmount      = 1.34f;
	/**
	 *  페이즈 인트로 길이(s). 외관 램프 시간이자 첫 발사 지연이다.
	 *  도약 애님(0.47s)보다 길어 애님도 이 안에서 끝난다.
	 */
	static constexpr float LookIntroSec = 0.8f;
};
