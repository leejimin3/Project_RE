// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionFx.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponentPoolMethodEnum.h"
#include "Engine/World.h"

namespace
{
	/** 폭발 에셋. scripts/make_explosion_fx.py 가 엔진 템플릿을 복제해 만든다. */
	const TCHAR* ExplosionAssetPath = TEXT("/Game/FX/NS_REBulletExplosion.NS_REBulletExplosion");

	/** 로드 캐시 — 매 폭발마다 LoadObject 하지 않는다. */
	UNiagaraSystem* GCachedSystem = nullptr;
	bool GLoadAttempted = false;
}

void REExplosionFx::SpawnBulletExplosion(const UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;   // 데디서버는 렌더가 없다
	}

	if (!GLoadAttempted)
	{
		GLoadAttempted = true;
		GCachedSystem = LoadObject<UNiagaraSystem>(nullptr, ExplosionAssetPath);
		if (!GCachedSystem)
		{
			// 조용한 무동작 금지 — 폭발이 안 나오는 것을 눈치채기 어렵다.
			UE_LOG(LogTemp, Error, TEXT("[RE] NS_REBulletExplosion 로드 실패 — 폭발이 표시되지 않는다 (#98)"));
		}
	}
	if (!GCachedSystem)
	{
		return;
	}

	// AutoRelease: 엔진 컴포넌트 풀에서 꺼내 쓰고 자동 반납. one-shot fx 전용 경로다.
	// 개별 스폰의 실용 상한을 크게 올려준다 — 인자 하나이므로 안 쓸 이유가 없다.
	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		World, GCachedSystem, Location,
		FRotator::ZeroRotator, FVector(1.f),
		/*bAutoDestroy=*/true, /*bAutoActivate=*/true,
		ENCPoolMethod::AutoRelease);
}
