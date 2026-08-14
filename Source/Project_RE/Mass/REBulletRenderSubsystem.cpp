// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletRenderSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

void UREBulletRenderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 데디서버는 렌더 안 함. 게임 월드(PIE/스탠드얼론)만 ISM 생성.
	if (InWorld.GetNetMode() == NM_DedicatedServer || !InWorld.IsGameWorld())
	{
		return;
	}

	Holder = InWorld.SpawnActor<AActor>();
	if (!Holder)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] RenderSubsystem: Holder spawn failed"));
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

	// 탄막 전용 머티리얼(#97) — 언릿 발광 + 프레넬 림. 겹친 탄 사이 경계가 보이게 한다.
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REBullet.M_REBullet")))
	{
		if (UMaterialInstanceDynamic* Dyn = ISM->CreateDynamicMaterialInstance(0, Base))
		{
			// 인접 탄을 두 색으로 교차시켜 겹침을 읽히게 한다 — 커스텀데이터 [1] 이 고른다 (#97).
			Dyn->SetVectorParameterValue(TEXT("Color"),  FLinearColor(1.f, 0.12f, 0.12f));  // 빨강
			Dyn->SetVectorParameterValue(TEXT("ColorB"), FLinearColor(0.12f, 0.4f, 1.f));   // 파랑
		}
	}
	else
	{
		// 조용한 폴백 금지 — 머티리얼 없이 렌더되면 림이 사라진 것을 눈치채기 어렵다.
		UE_LOG(LogTemp, Error, TEXT("[RE] M_REBullet 로드 실패 — 탄환 머티리얼 없이 렌더된다 (#97)"));
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
		UE_LOG(LogTemp, Error, TEXT("[RE] M_REBullet 로드 실패 — 곡사탄 머티리얼 없이 렌더된다 (#97)"));
	}

	// 착지 마커 ISM — 빨강 평면 원. Cylinder를 납작하게(Z scale 축소) 눌러 디스크로.
	MarkerISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	MarkerISM->SetupAttachment(ISM);
	MarkerISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerISM->bAffectDynamicIndirectLighting = false;
	MarkerISM->bAffectDistanceFieldLighting = false;
	// 마커는 바닥에 붙은 납작한 디스크라 그림자가 자기 자신에 가려 보이지도 않는다 (#95).
	MarkerISM->SetCastShadow(false);
	MarkerISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		MarkerISM->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dyn = MarkerISM->CreateDynamicMaterialInstance(0, Base))
		{
			// HDR 고강도 — 탄환 구체는 곡면 스페큘러 하이라이트로 블룸이 걸려 밝게 보이지만,
			// 마커는 평평한 원판이라 그 하이라이트가 없어 같은 Color=1.0 값이어도 어둡게 죽는다.
			// 값 자체를 1.0 위로 올려 블룸 임계값을 넘겨야 각도와 무관하게 확실히 보인다.
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor(4.f, 0.f, 0.f));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] RenderSubsystem: ISM ready (mesh=%d)"), ISM->GetStaticMesh() != nullptr);
}
