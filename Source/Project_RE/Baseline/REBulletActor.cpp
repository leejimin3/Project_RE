// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Mass/REBulletGeometry.h"                         // 탄환 스케일 단일 출처 (#141)

AREBulletActor::AREBulletActor()
{
	// 개별 Tick은 액터 경로의 본질적 비용이다. 이게 비교의 요점이라 끄지 않는다.
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// Mass ISM도 NoCollision(REBulletRenderSubsystem.cpp:30, #34). 콜리전 켜면 액터 쪽에
	// 불공정한 추가 비용이 붙어 비교가 오염된다.
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Mass 와 **같은 상수**를 쓴다 — 비교군이 다른 크기면 비교가 성립하지 않는다.
	// 전에는 유니티 빌드 C4459 를 피하려고 이름을 ActorBulletScale 로 비틀어 복사해
	// 두었다. 네임스페이스로 합쳐 원인부터 없앴다 (#141).
	Mesh->SetRelativeScale3D(FVector(REBulletGeometry::BulletScale));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		Mesh->SetStaticMesh(SphereMesh.Object);
	}
}

void AREBulletActor::Init(const FVector& InVelocity, float InLifetime, UMaterialInterface* InMaterial)
{
	Velocity = InVelocity;
	Lifetime = InLifetime;
	if (InMaterial)
	{
		Mesh->SetMaterial(0, InMaterial);
	}
}

void AREBulletActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Mass SimProcessor 대응: Transform += V*dt, Lifetime -= dt, 소진 시 파괴.
	AddActorWorldOffset(Velocity * DeltaSeconds);

	Lifetime -= DeltaSeconds;
	if (Lifetime <= 0.f)
	{
		Destroy();
	}
}
