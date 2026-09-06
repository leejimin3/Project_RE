// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "REBulletPattern.h"

/**
 *  보스 패턴 정의의 **단일 출처**.
 *
 *  전에는 같은 지식이 일곱 곳에 병렬로 나열돼 있었다 — 계열 판별 두 함수, 로테이션 풀,
 *  페이즈 길이 switch, 발사 간격 switch, 외관 switch, 곡사 파라미터 if-else 사슬.
 *  패턴을 하나 추가하려면 일곱 곳을 고쳐야 했고, **하나를 빠뜨려도 컴파일이 통과했다**
 *  (전부 default 가 있다). 결과는 크래시가 아니라 조용한 오동작이었다: 새 패턴이 Spiral 의
 *  발사 간격으로 돌거나, 곡사인데 직선탄 경로를 타거나, 외관이 안 바뀌거나.
 *
 *  코드 자신이 이미 그 문제를 알고 주석으로 적어 두었지만 주석은 기록할 뿐 막지 못한다.
 *  이제 테이블 한 줄을 빠뜨리면 **빌드가 실패한다** — 아래 static_assert 두 개가 그 일을 한다.
 *  이게 이 파일의 목적 전부다.
 *
 *  **왜 DataAsset/ini 가 아니라 constexpr 테이블인가:** 지금 필요한 건 튜닝 편의가 아니라
 *  누락 방지다. 디자이너가 없어 에디터 튜닝의 수요가 없고, 파라미터가 백 개 넘어 ini 로
 *  올리면 각 값이 왜 그 값인지 적어 둔 주석이 통째로 사라진다 — 이 프로젝트에서 그 주석이
 *  자산이다. 튜닝하는 사람이 프로그래머가 아니게 되는 날 ini 로 올린다.
 */
namespace REBoss
{
	/** 발사 경로 분기. 세 계열이 서로 다른 스폰 진입점을 탄다. */
	enum class EFamily : uint8
	{
		Direct,   // 직선탄 — Multicast_FireDirect
		Bloom,    // 곡선 블룸(직선탄이지만 스폰 위치로 모양을 그린다) — Multicast_FireDirect
		Arc       // 곡사 — Multicast_FireArtillery
	};

	/**
	 *  바디 머티리얼의 한 상태. 팩 마스터가 노출한 파라미터가 그대로 필드다.
	 *  기본값은 Spiral 의 외관이다 — 회색 화강암(Snow/Lava 둘 다 0) + 팩 기본 MI 의 빨강.
	 */
	struct FBossLook
	{
		float        Snow = 0.f;
		float        Lava = 0.f;
		FLinearColor Emis = FLinearColor(1.f, 0.f, 0.f, 1.f);
	};

	/** 곡사 계열 전용 파라미터. Family != Arc 이면 읽지 않는다. */
	struct FArcDef
	{
		float FlightTime = 0.f;
		float MaxHeight  = 0.f;
		int32 Count      = 0;
	};

	/**
	 *  패턴 1종의 정의 전부. **이 구조체가 패턴 지식의 유일한 출처다.**
	 *  패턴을 추가하려면 아래 테이블에 한 줄을 넣는다 — 넣지 않으면 컴파일이 실패한다.
	 */
	struct FPatternDef
	{
		EBulletPattern Pattern;        // 자기 자신 (테이블 순서 검증용)
		EFamily        Family;
		const TCHAR*   Name;           // 로그 표기
		float          PhaseSec;       // 발사 구간 길이
		float          FireInterval;   // < 0 이면 ini 기본(REBulletPattern::FireIntervalSec())
		bool           bInRotation;    // 로테이션 풀 포함 여부
		FArcDef        Arc;
		FBossLook      Look;
	};

	//========================================================================================
	//  패턴 파라미터. AREBossCharacter 의 private constexpr 에서 **값·이름·계산식 그대로**
	//  옮겨 왔다. 각 값이 왜 그 값인지 적어 둔 주석도 같이 온다 — 그게 이 프로젝트의 자산이다.
	//
	//  여기 있는 것은 **테이블이 쥐는 축**뿐이다: 페이즈 길이 / 발사 간격 / 곡사 체공·고도·발수 /
	//  외관 질감. 착지 지오메트리 파라미터(LissaExtent, StormMinRadius, SpiroSwing 등)는
	//  패턴 하나 안의 세부라 테이블 필드가 아니고 AREBossCharacter 에 남는다.
	//========================================================================================

	//~ 페이즈 길이 ---------------------------------------------------------------------------
	inline constexpr float SpiralPhaseSec     = 5.f;
	inline constexpr float FanPhaseSec        = 3.f;
	/** 파면이 여러 겹 쌓여야 무늬가 성립한다. */
	inline constexpr float RosePhaseSec       = 8.f;
	inline constexpr float CardioidPhaseSec   = 7.f;
	inline constexpr float LissaPhaseSec      = 9.f;
	inline constexpr float VortexPhaseSec     = 9.f;
	inline constexpr float StarBloomPhaseSec  = 8.f;
	inline constexpr float LemniPhaseSec      = 8.f;
	inline constexpr float SuperPhaseSec      = 9.f;
	inline constexpr float RoseFieldPhaseSec  = 9.f;
	inline constexpr float DomePhaseSec       = 9.f;
	inline constexpr float SpiroPhaseSec      = 9.f;
	inline constexpr float MicroPhaseSec      = 7.f;
	inline constexpr float ArtilleryPhaseSec  = 4.f;    // 페이즈 길이
	inline constexpr float StormPhaseSec      = 10.f;   // 스윕 1회 길이

	//~ 발사 간격 -----------------------------------------------------------------------------
	inline constexpr float FanFireIntervalSec = 0.5f;
	inline constexpr float ArtilleryFireInterval = 1.8f;   // 일제사 간격(비행시간보다 길게 → 겹침 억제)
	inline constexpr float StormFireInterval  = 0.05f;     // 볼리 간격 = 생성 이벤트 주기(RPC 20/s)
	/**
	 *  1.0(150/s)에서 패턴 15개 중 **최악**이었다: p99 14.95ms(720p) / 15.55ms(1080p).
	 *  런 간 노이즈가 ±0.9ms 라 게이트까지 1.05ms 는 방어됐다고 말할 폭이 아니다.
	 *  1.3(115/s)으로 낮춘다 — 겹수는 2.5/1.3 = 1.9 라 곡선 가독성은 그대로다
	 *  (RoseField·Spirograph 가 겹수 1로도 읽힌다).
	 */
	inline constexpr float LissaFireInterval  = 1.3f;
	/**
	 *  발수를 링 둘레에 맞춰 52로 올리면서 착지율이 180→260/s 로 뛰어 13.72ms 가 됐다 —
	 *  게이트 16.6 에 17%밖에 안 남는다. 간격으로 착지율을 되돌린다(52/0.28 = 186/s).
	 */
	inline constexpr float VortexFireInterval = 0.28f;
	/**
	 *  블룸 전용 발사 간격(s). **이 패턴이 읽히느냐를 이 값이 혼자 정한다.**
	 *  동시에 보이는 복사본 사이의 반경 간격이 R₀·ScaleRate·이 값이고, 그게 탄 지름(70uu)보다
	 *  작으면 이웃 복사본이 서로 겹쳐 도형이 통째로 뭉갠다.
	 *  Spiral 과 공유하던 0.15 는 165·1.7·0.15 = 42uu 라 지름보다 작았다 — 실제로 뭉갰다.
	 *  0.4 면 112uu 로 분리된다. 수명 7초 동안 17겹이 고른 간격으로 퍼진다.
	 */
	inline constexpr float BloomFireInterval  = 0.4f;
	//~ RoseField / Spirograph 는 **겹수(체공/발사간격)를 1로 유지하는 것이 생명이다.**
	//  블룸은 복사본이 반경으로 벌어져 여러 겹이 겹쳐도 읽혔지만, 여기는 복사본이 전부
	//  같은 크기라 회전만 다른 채 직접 포개진다 — 0.4/2.5(6겹)로 뒀더니 곡선이 통째로
	//  뭉개져 마커 덩어리가 됐다(실제로 그랬다).
	inline constexpr float RoseFieldFireInterval = 1.2f;
	inline constexpr float DomeFireInterval   = 0.4f;
	inline constexpr float SpiroFireInterval  = 1.2f;
	/** 볼리 주기(s). MicroCount 와 곱해 초당 발수를 낸다: 10/0.08 = 125발/s. RPC 는 12.5/s. */
	inline constexpr float MicroFireInterval  = 0.08f;

	//~ 곡사 체공 / 고도 / 발수 ----------------------------------------------------------------
	inline constexpr float ArtilleryFlightTime = 1.5f;   // 회피 시간
	inline constexpr float ArtilleryMaxHeight  = 400.f;  // 포물선 최대 고도
	inline constexpr int32 ArtilleryCount      = 12;     // 일제사 착지점 수(모양별 기준)
	/**
	 *  체공(s). 6 은 탄이 너무 느리게 떨어졌다 — 3 으로 줄여 낙하가 빨라진다.
	 *  동시 체공 탄이 여기에 정비례하므로 밀도가 절반이 된다(960 → 480). 착지율은 안 변해
	 *  프레임 비용도 그대로다 — 밀도를 되찾으려면 Arms 를 올려야 하고 그건 비용이 붙는다.
	 */
	inline constexpr float StormFlightTime     = 3.f;
	/**
	 *  포물선 최대 고도. 궤적이 물리가 아니라 정규화 보간이라(REArcSimProcessor) 높이와
	 *  체공 시간은 완전히 독립이다 — 이 값을 바꿔도 착지 타이밍은 안 변한다.
	 *  800 은 탑다운 카메라(높이 1500) 위로 탄이 솟아 화면 밖으로 나갔다 — 절반으로 낮췄다.
	 */
	inline constexpr float StormMaxHeight      = 400.f;
	inline constexpr int32 StormCount          = 4;      // 볼리당 슬롯 수 = 단발 사격의 시간 해상도
	inline constexpr float LissaFlightTime     = 2.5f;
	inline constexpr float LissaMaxHeight      = 350.f;
	/**
	 *  곡선 위 표본 수. 이 값이 곧 매듭의 해상도다 — 점 간격이 마커 지름(2×120=240)보다
	 *  커지면 곡선이 끊긴 점 무더기로 보인다. 30 으로는 실제로 그랬다.
	 */
	inline constexpr int32 LissaCount          = 150;
	inline constexpr float VortexFlightTime    = 3.5f;
	/**
	 *  곡사 기준 고도. 소용돌이는 탄마다 [VortexMinHeight, 이 값] 으로 덮어써 층을 만든다
	 *  (ShapeArcShots) — 궤적이 정규화 보간이라 높이를 바꿔도 착지 타이밍은 안 변한다.
	 *  800 은 탑다운 카메라(높이 1500) 위로 솟아 화면 밖으로 나갔다.
	 */
	inline constexpr float VortexMaxHeight     = 400.f;
	/**
	 *  볼리당 발수 = 착지 링의 점 수. 링 둘레 2π·900 을 이 값으로 나눈 간격이 마커 지름(240)
	 *  보다 작아야 링이 끊기지 않는다 — 36 이면 157uu 다.
	 */
	inline constexpr int32 VortexCount         = 52;
	/** 회피 예고 시간. Artillery(1.5)와 같게 둔다 — 곡선을 읽고 움직일 시간이다. */
	inline constexpr float RoseFieldFlightTime = 1.5f;
	inline constexpr float RoseFieldMaxHeight  = 150.f;
	/** 장미 둘레(≈18,000uu) / 마커 지름(240) 을 넘겨야 곡선이 이어진다. */
	inline constexpr int32 RoseFieldCount      = 110;
	inline constexpr float DomeFlightTime      = 3.f;
	inline constexpr float DomeMaxHeight       = 100.f;
	inline constexpr int32 DomeCount           = 44;
	inline constexpr float SpiroFlightTime     = 1.5f;
	inline constexpr float SpiroMaxHeight      = 150.f;
	/** 로제트는 고리가 겹쳐 곡선이 길다 — 장미보다 표본을 더 준다. */
	inline constexpr int32 SpiroCount          = 150;
	/**
	 *  체공(s). **이 값이 회피 가능성을 혼자 정한다.**
	 *  목표는 발사 순간의 플레이어 위치이므로 착지 시점까지 플레이어가 움직인 거리가
	 *  판정 반경(MicroRadius 45)을 넘으면 빗나간다. MaxWalkSpeed 는 ACharacter 기본 600 이라
	 *  600 × 0.6 = 360uu 로 **4배 여유** — 계속 움직이면 확실히 피해지고 서 있으면 확정 피격이다.
	 */
	inline constexpr float MicroFlightTime     = 0.6f;
	inline constexpr float MicroMaxHeight      = 100.f;
	/**
	 *  볼리당 발수 = 부채꼴을 나누는 각 수. 원주를 이 수로 등분해 동시에 뻗어 나간다.
	 *  동시 체공 = Count/Interval × FlightTime = 10/0.08 × 0.6 = 75발.
	 */
	inline constexpr int32 MicroCount          = 10;

	//~ 외관 질감 -----------------------------------------------------------------------------
	//  팩 MI 프리셋에서 그대로 가져온 값 — MI_Stone_Golem_Inst1(Snow) / Inst2(Lava).
	//  패턴이 16개다. 질감 3종 × 색으로도 슬슬 빠듯하다 — **(질감, 이미시브 색) 쌍**이
	//  유일하도록 배분했다. 색만으로는 안 갈린다.
	inline constexpr float FanSnowAmount       = 1.34f;
	inline constexpr float ArtilleryLavaAmount = 2.47f;
	inline constexpr float StormLavaAmount     = 2.47f;
	inline constexpr float CardioidSnowAmount  = 1.34f;
	inline constexpr float RoseFieldLavaAmount = 2.47f;
	inline constexpr float DomeSnowAmount      = 1.34f;
	inline constexpr float SpiroLavaAmount     = 2.47f;
	inline constexpr float MicroSnowAmount     = 1.34f;
	inline constexpr float LissaLavaAmount     = 2.47f;
	inline constexpr float VortexSnowAmount    = 1.34f;

	//========================================================================================
	//  테이블. 인덱스 == (int32)EBulletPattern — 아래 static_assert 가 강제한다.
	//========================================================================================
	inline constexpr FPatternDef PatternTable[] =
	{
		//~ 0 Spiral — 기준 패턴. 발사 간격은 ini(BossFireInterval)를 쓴다.
		//  외관은 회색 화강암 + 팩 기본 빨강 이미시브(FBossLook 기본값).
		{ EBulletPattern::Spiral, EFamily::Direct, TEXT("Spiral"),
		  SpiralPhaseSec, -1.f, true, {}, {} },

		//~ 1 Fan — Snow 스칼라만으로는 하얘지지 않는다. 그 값은 균열 이미시브를 증폭할 뿐이고
		//  색은 Emis 가 쥔다. 안 덮으면 적열이 되어 빨강+흰색 탄막에 섞이고 주황 용암
		//  (Artillery)과도 계열이 겹친다. 청록으로 가른다.
		{ EBulletPattern::Fan, EFamily::Direct, TEXT("Fan"),
		  FanPhaseSec, FanFireIntervalSec, true, {},
		  { FanSnowAmount, 0.f, FLinearColor(0.15f, 0.70f, 1.0f, 1.0f) } },

		//~ 2 Homing — #67 백로그 스텁. 로테이션 풀에 없고 re.Debug.BossPattern 도 풀 인덱스로
		//  클램프하므로 **도달 불가**다. PhaseSec/Name 이 Spiral 인 것은 현행 동작 그대로다:
		//  전에는 BeginPhase 의 switch 에 Homing case 가 없어 default(=Spiral)로 떨어졌다.
		//  구현할 때 이 행을 실제 값으로 채운다.
		{ EBulletPattern::Homing, EFamily::Direct, TEXT("Spiral"),
		  SpiralPhaseSec, -1.f, false, {}, {} },

		//~ 3 Artillery — 곡사 기준. 용암 질감 + 기본 빨강.
		{ EBulletPattern::Artillery, EFamily::Arc, TEXT("Artillery"),
		  ArtilleryPhaseSec, ArtilleryFireInterval, true,
		  { ArtilleryFlightTime, ArtilleryMaxHeight, ArtilleryCount },
		  { 0.f, ArtilleryLavaAmount, FLinearColor(1.f, 0.f, 0.f, 1.f) } },

		//~ 4 ArtilleryStorm — 용암은 Artillery 와 같은 값이라 그것만으론 두 곡사 페이즈가
		//  구분되지 않는다. 이미시브를 금색으로 올려 가른다 — Fan(청록)/Spiral(빨강)과도 안 겹친다.
		{ EBulletPattern::ArtilleryStorm, EFamily::Arc, TEXT("ArtilleryStorm"),
		  StormPhaseSec, StormFireInterval, true,
		  { StormFlightTime, StormMaxHeight, StormCount },
		  { 0.f, StormLavaAmount, FLinearColor(1.0f, 0.85f, 0.2f, 1.0f) } },

		//~ 5 RoseEnvelope — 질감 축(Snow/Lava)에는 남는 조합이 없다. Fan 이 Snow, 곡사 둘이
		//  Lava 를 쓴다. Spiral 과 같은 화강암에 이미시브만 보라로 가른다.
		//  발사 수와 간격은 Spiral 과 공유한다 — 링 지오메트리가 같아 새 손잡이를 만들 이유가 없다.
		{ EBulletPattern::RoseEnvelope, EFamily::Direct, TEXT("RoseEnvelope"),
		  RosePhaseSec, -1.f, true, {},
		  { 0.f, 0.f, FLinearColor(0.70f, 0.10f, 1.0f, 1.0f) } },

		//~ 6 Cardioid — 색만 주황으로 가르면 Spiral(회색 화강암 + 빨강)과 화면에서 잘 안 갈렸다.
		//  질감까지 흰 화강암으로 바꾼다(흰 화강암 + 주황은 남는 조합이다).
		{ EBulletPattern::Cardioid, EFamily::Direct, TEXT("Cardioid"),
		  CardioidPhaseSec, -1.f, true, {},
		  { CardioidSnowAmount, 0.f, FLinearColor(1.0f, 0.45f, 0.05f, 1.0f) } },

		//~ 7 LissajousStorm — 용암 + 연두.
		{ EBulletPattern::LissajousStorm, EFamily::Arc, TEXT("LissajousStorm"),
		  LissaPhaseSec, LissaFireInterval, true,
		  { LissaFlightTime, LissaMaxHeight, LissaCount },
		  { 0.f, LissaLavaAmount, FLinearColor(0.55f, 1.0f, 0.20f, 1.0f) } },

		//~ 8 BezierVortex — 흰 화강암 + 진파랑. 고도는 탄마다 덮어써 층을 만든다(ShapeArcShots).
		{ EBulletPattern::BezierVortex, EFamily::Arc, TEXT("BezierVortex"),
		  VortexPhaseSec, VortexFireInterval, true,
		  { VortexFlightTime, VortexMaxHeight, VortexCount },
		  { VortexSnowAmount, 0.f, FLinearColor(0.10f, 0.25f, 1.0f, 1.0f) } },

		//~ 9~11 블룸 3종은 질감을 안 쓴다(회색 화강암) — 색만으로 가른다. 위에서 회색 화강암을
		//  쓰는 건 Spiral(빨강)과 RoseEnvelope(보라)뿐이라 아래 셋과 안 겹친다.
		{ EBulletPattern::StarBloom, EFamily::Bloom, TEXT("StarBloom"),
		  StarBloomPhaseSec, BloomFireInterval, true, {},
		  { 0.f, 0.f, FLinearColor(1.0f, 0.90f, 0.40f, 1.0f) } },        // 담금색
		{ EBulletPattern::LemniscateBloom, EFamily::Bloom, TEXT("LemniscateBloom"),
		  LemniPhaseSec, BloomFireInterval, true, {},
		  { 0.f, 0.f, FLinearColor(0.10f, 0.90f, 0.90f, 1.0f) } },       // 청록
		{ EBulletPattern::SuperformulaBloom, EFamily::Bloom, TEXT("SuperformulaBloom"),
		  SuperPhaseSec, BloomFireInterval, true, {},
		  { 0.f, 0.f, FLinearColor(0.30f, 1.0f, 0.40f, 1.0f) } },        // 연녹

		//~ 12 RoseField — 용암 + 진파랑.
		{ EBulletPattern::RoseField, EFamily::Arc, TEXT("RoseField"),
		  RoseFieldPhaseSec, RoseFieldFireInterval, true,
		  { RoseFieldFlightTime, RoseFieldMaxHeight, RoseFieldCount },
		  { 0.f, RoseFieldLavaAmount, FLinearColor(0.20f, 0.35f, 1.0f, 1.0f) } },

		//~ 13 AerialDome — 흰 화강암 + 자홍.
		{ EBulletPattern::AerialDome, EFamily::Arc, TEXT("AerialDome"),
		  DomePhaseSec, DomeFireInterval, true,
		  { DomeFlightTime, DomeMaxHeight, DomeCount },
		  { DomeSnowAmount, 0.f, FLinearColor(1.0f, 0.20f, 0.70f, 1.0f) } },

		//~ 14 Spirograph — 용암 + 청록.
		{ EBulletPattern::Spirograph, EFamily::Arc, TEXT("Spirograph"),
		  SpiroPhaseSec, SpiroFireInterval, true,
		  { SpiroFlightTime, SpiroMaxHeight, SpiroCount },
		  { 0.f, SpiroLavaAmount, FLinearColor(0.15f, 0.80f, 0.95f, 1.0f) } },

		//~ 15 MicroMissile — 흰 화강암 + 경고등 빨강.
		{ EBulletPattern::MicroMissile, EFamily::Arc, TEXT("MicroMissile"),
		  MicroPhaseSec, MicroFireInterval, true,
		  { MicroFlightTime, MicroMaxHeight, MicroCount },
		  { MicroSnowAmount, 0.f, FLinearColor(1.0f, 0.15f, 0.10f, 1.0f) } },
	};

	inline constexpr int32 PatternTableNum = (int32)UE_ARRAY_COUNT(PatternTable);

	/**
	 *  테이블 인덱스가 enum 값과 1:1인지 컴파일 타임에 확인한다.
	 *  **이게 이 리팩터링의 목적 전부다** — 패턴을 추가하면서 테이블 행을 빠뜨리거나
	 *  순서를 어긋내면 빌드가 실패한다. 전에는 조용히 오동작했다.
	 */
	constexpr bool IsTableOrdered()
	{
		for (int32 i = 0; i < PatternTableNum; ++i)
		{
			if ((int32)PatternTable[i].Pattern != i)
			{
				return false;
			}
		}
		return true;
	}
	static_assert(IsTableOrdered(), "PatternTable 순서가 EBulletPattern 과 어긋났다");
	static_assert(PatternTableNum == (int32)EBulletPattern::Count,
	              "EBulletPattern 에 패턴을 추가했으면 PatternTable 에도 행을 추가하라");

	/** 테이블 조회. 범위 밖이면 Spiral(인덱스 0) 폴백 + 로그. */
	const FPatternDef& GetPatternDef(EBulletPattern P);

	inline bool IsArcPattern  (EBulletPattern P) { return GetPatternDef(P).Family == EFamily::Arc;   }
	inline bool IsBloomPattern(EBulletPattern P) { return GetPatternDef(P).Family == EFamily::Bloom; }
}
