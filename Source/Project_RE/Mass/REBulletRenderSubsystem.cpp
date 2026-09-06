// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "REExplosionFx.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

void UREBulletRenderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 데디서버는 렌더 안 함. 게임 월드(PIE/스탠드얼론)만 ISM 생성.
	if (InWorld.GetNetMode() == NM_DedicatedServer || !InWorld.IsGameWorld())
	{
		return;
	}

	// 폭발 에셋 선로드 (#107). 첫 폭발에서 동기 로드가 걸리면 ~290ms 멈추고,
	// 그 히치가 대쉬 구간에 겹치면 이동거리까지 틀어졌다(#106). 시작 시 한 번에 끝낸다.
	// 여기가 적기다 — 위 가드가 데디서버·비게임월드를 이미 걸러냈고, 레벨 로드 중이라
	// 로드 비용이 눈에 띄지 않는다.
	REExplosionFx::Preload(&InWorld);

	Holder = InWorld.SpawnActor<AActor>();
	if (!Holder)
	{
		UE_LOG(LogREBullet, Warning, TEXT("[RE] RenderSubsystem: Holder spawn failed"));
		return;
	}

	ISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	Holder->SetRootComponent(ISM);
	// 탄막은 시각 표현 전용 — 콜리전 기본값 BlockAll이 AutoFire linetrace(#34)/플레이어 이동을 막던 문제.
	ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Lumen 씬 제외 — 움직이는 인스턴스 수천 개가 서피스 캐시를 매 프레임 무효화해
	// 시점(視點)에 따라 GPU가 수십 ms 튀는 문제(카메라가 탄막을 담을 때). 탄환은 작고 빨라
	// 간접광 기여 체감 0 — 레벨/캐릭터 GI는 유지된다. 실측: Lumen GI off 시 프레임 정상 복귀.
	ISM->bAffectDynamicIndirectLighting = false;
	ISM->bAffectDistanceFieldLighting = false;
	// 그림자 제외 — 탄막에서 탄환 그림자는 시각 기여가 사실상 없는데 GPU 최대 소비처였다.
	// 실측(40,000발): 그림자 켬 GPU 17.16 ms → 끔 6.13 ms. 병목이 GPU에서 게임 스레드로 넘어간다 (#95).
	ISM->SetCastShadow(false);
	ISM->RegisterComponent();

	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ISM->SetStaticMesh(Mesh);
	}
	// 퍼인스턴스 커스텀데이터 [0]=스폰 팝, [1]=색 선택. 머티리얼이 두 슬롯을 읽는다 (#97).
	ISM->SetNumCustomDataFloats(2);

	// 탄막 전용 머티리얼(#97) — 언릿 발광 + 프레넬 림 + 인접 탄 색 교차.
	// 직선탄은 파라미터를 덮어쓰지 않으므로 MID 가 필요 없다 — 머티리얼 기본값(Color/ColorB)이
	// 정본이고, 에디터에서 색·림을 바꾸면 코드 수정 없이 반영된다. 어느 색을 쓸지는
	// 퍼인스턴스 커스텀데이터 [1] 이 고른다.
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REBullet.M_REBullet")))
	{
		ISM->SetMaterial(0, Base);
	}
	else
	{
		// 조용한 폴백 금지 — 머티리얼 없이 렌더되면 림이 사라진 것을 눈치채기 어렵다.
		UE_LOG(LogREBullet, Error, TEXT("[RE] M_REBullet 로드 실패 — 탄환 머티리얼 없이 렌더된다 (#97)"));
	}

	// 곡사탄 ISM — 주황 구체(직선탄 빨강과 구분). Z 살아있어 궤적 높이가 보인다.
	ArcISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	ArcISM->SetupAttachment(ISM);
	ArcISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArcISM->bAffectDynamicIndirectLighting = false;
	ArcISM->bAffectDistanceFieldLighting = false;
	ArcISM->SetCastShadow(false);   // 위와 같은 이유 (#95)
	ArcISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ArcISM->SetStaticMesh(Mesh);
	}
	ArcISM->SetNumCustomDataFloats(2);   // [0]=스폰 팝, [1]=색 선택(곡사탄은 항상 0) (#97)

	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REBullet.M_REBullet")))
	{
		if (UMaterialInstanceDynamic* Dyn = ArcISM->CreateDynamicMaterialInstance(0, Base))
		{
			// 곡사탄은 색 교차를 쓰지 않는다 — 두 색을 같게 둬서 커스텀데이터와 무관하게 단색.
			Dyn->SetVectorParameterValue(TEXT("Color"),  FLinearColor(1.f, 0.5f, 0.f));  // 주황
			Dyn->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(1.f, 0.5f, 0.f));
		}
	}
	else
	{
		UE_LOG(LogREBullet, Error, TEXT("[RE] M_REBullet 로드 실패 — 곡사탄 머티리얼 없이 렌더된다 (#97)"));
	}

	// 착지 마커 ISM — 게임 경고 표시처럼 아주 얇은 반투명 링.
	// Plane 을 쓴다. 이전에는 Cylinder 였는데, Cylinder 는 높이가 100uu 라 Z 스케일 1.0 이
	// 두께 100uu 짜리 '낮은 원기둥'이었다(#122 피드백). Plane 은 두께 개념이 없어 원천 해결이고,
	// 0~1 UV 가 깔끔해서 머티리얼에서 링 마스크를 만들기도 쉽다.
	MarkerISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	MarkerISM->SetupAttachment(ISM);
	MarkerISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerISM->bAffectDynamicIndirectLighting = false;
	MarkerISM->bAffectDistanceFieldLighting = false;
	// 마커는 바닥에 붙은 납작한 디스크라 그림자가 자기 자신에 가려 보이지도 않는다 (#95).
	MarkerISM->SetCastShadow(false);
	MarkerISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		MarkerISM->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_ArenaMarker.M_ArenaMarker")))
	{
		if (UMaterialInstanceDynamic* Dyn = MarkerISM->CreateDynamicMaterialInstance(0, Base))
		{
			// 링 마스크(Unlit/Translucent)라 스페큘러 하이라이트에 기대지 않는다. 그래도 HDR
			// 값을 1.0 위로 두어야 블룸 임계값을 넘겨 경고 표시로 또렷하게 읽힌다.
			// 파라미터 이름 "Color" 는 M_ArenaMarker 쪽과 맞춰야 한다 - 다르면 조용히 안 먹는다.
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor(3.f, 0.15f, 0.15f));
		}
	}
	else
	{
		UE_LOG(LogREBullet, Error, TEXT("[RE] M_ArenaMarker 로드 실패 — 마커가 기본 머티리얼로 렌더된다 (#122)"));
	}

	// 실제 적용된 머티리얼 이름을 찍는다 — CreateDynamicMaterialInstance 가 실패하면
	// 로드는 성공했는데도 조용히 기본 머티리얼로 렌더된다(라이팅 음영이 생겨 언릿 의도가 깨진다). (#97)
	{
		const UMaterialInterface* Applied = ISM->GetMaterial(0);
		UE_LOG(LogREBullet, Log, TEXT("[RE] RenderSubsystem: ISM ready (mesh=%d) material=%s"),
			ISM->GetStaticMesh() != nullptr,
			Applied ? *Applied->GetName() : TEXT("NULL"));
	}
}
