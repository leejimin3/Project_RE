// Copyright Epic Games, Inc. All Rights Reserved.

#include "REHealthComponent.h"
#include "REHealthBarComponent.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UREHealthComponent::UREHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UREHealthComponent::Init(float InMaxHealth)
{
	MaxHealth = InMaxHealth;
	Health = MaxHealth;
}

bool UREHealthComponent::ApplyDamage(float Amount)
{
	Health = FMath::Clamp(Health - Amount, 0.f, MaxHealth);
	OnRep_Health();   // 서버/싱글 경로 — 복제 OnRep은 원격 클라 전용이라 직접 호출

	if (Health > 0.f || bDeathHandled)
	{
		return false;
	}
	bDeathHandled = true;
	return true;
}

void UREHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UREHealthComponent, Health);
}

void UREHealthComponent::OnRep_Health()
{
	const AActor* Owner = GetOwner();
	if (UREHealthBarComponent* Bar = Owner ? Owner->FindComponentByClass<UREHealthBarComponent>() : nullptr)
	{
		Bar->SetHealthPercent(MaxHealth > 0.f ? Health / MaxHealth : 0.f);
	}
}
