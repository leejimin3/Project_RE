# 구현 목표: M1 #14 — 탄환 Fragment 확정 + FBulletTag + Archetype 스폰

## 컨텍스트
UE 5.8 C++ 탄막(bullet-hell) 프로젝트. MassEntity ECS로 탄환 관리. 게임 모듈명 `Project_RE`(단일 모듈), 저장소 `E:\UnrealProjects\Project_RE`.
GitHub 이슈 #14 (마일스톤 M1). 이 goal: 탄환 데이터 레이아웃 확정 + EntityManager 경유 재사용 스폰 헬퍼.
스코프 밖 후속 이슈 — **절대 손대지 말 것**: 이동/수명(#15), 패턴 수학(#16), ISM 렌더(#17).

설계 스펙: docs/superpowers/specs/2026-07-09-bullet-fragment-archetype-spawn-design.md
상세 플랜: docs/superpowers/plans/2026-07-09-bullet-fragment-archetype-spawn.md
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치
dev에서 분기: `feature/M1-bullet-archetype-spawn`

## 전역 제약
- 엔진 빌드: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload`
- **Build.cs / .uproject 변경 금지** — `FTransformFragment`는 MassCore(`Mass/EntityFragments.h`)에 있고 MassCore는 이미 의존. MassCommon/MassGameplay 플러그인 불필요.
- 자동화 테스트 인프라 없음 → 게이트 = **빌드 성공** + **headless 프로브 로그 관측**.
- 로그 접두어 `[RE]` 고정. 클래스/타입명 아래와 동일.
- API 매크로(`PROJECT_RE_API`) 불필요 — 게임 모듈 단일.
- 기존 코드 스타일 유지, surgical change. 요청 밖 리팩터 금지.

## 검증된 API (실물 확인됨, UE 5.8)
- `FMassEntityManager::CreateArchetype(TConstArrayView<const UScriptStruct*>)` → `FMassArchetypeHandle` (Fragment+Tag 혼합 리스트 허용)
- `FMassEntityManager::CreateEntity(const FMassArchetypeHandle&)` → `FMassEntityHandle`
- `FMassEntityManager::GetFragmentDataChecked<T>(FMassEntityHandle)` → `T&`
- `FTransformFragment::GetMutableTransform()` → `FTransform&` (`Mass/EntityFragments.h`, MassCore)
- `FMassArchetypeHandle::IsValid()` → `bool`
- EntityManager 획득: `GetWorld()->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager()`
- include 경로: `Subsystems/WorldSubsystem.h`(Engine Public), `Mass/EntityFragments.h`(MassCore), `MassEntitySubsystem.h`, `MassEntityManager.h`, `MassEntityTypes.h`, `MassArchetypeTypes.h`

## 기존 파일 현황 (변경 대상)
- `Source/Project_RE/Mass/REBulletFragments.h` — `FBulletSimFragment{FVector Velocity; float Lifetime;}`, `FBulletRenderFragment{int32 InstanceIndex=INDEX_NONE;}` 이미 존재. `#include "MassEntityTypes.h"` 포함.
- `Source/Project_RE/Core/REBossCharacter.cpp` — `TriggerBulletPattern`에 인라인 Archetype/CreateEntity 루프 존재(교체 대상). 상단에 `namespace { constexpr int32 BulletsPerPattern = 16; }` 존재.

================================================================
## TASK 1: FBulletTag + UREBulletSpawnSubsystem
================================================================

### 1-1. Source/Project_RE/Mass/REBulletFragments.h — FBulletTag 추가
`FBulletRenderFragment` 닫는 `};` 뒤에 삽입:

```cpp

/** 탄환 식별 태그. Query 필터 전용(데이터 없음). 후속 Processor가 이 태그로 탄환만 선별. */
USTRUCT()
struct FBulletTag : public FMassTag
{
	GENERATED_BODY()
};
```

### 1-2. Source/Project_RE/Mass/REBulletSpawnSubsystem.h (신규)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "REBulletSpawnSubsystem.generated.h"

/** 탄환 1발 스폰 파라미터. UStruct 아님 — 함수 인자 전용 경량 구조체. */
struct FBulletSpawnParams
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float   Lifetime = 0.f;
};

/**
 *  탄환 스폰 단일 진입점. 탄환 Archetype을 lazy 캐싱하고 EntityManager로 스폰 + Fragment 초기화.
 *  Boss/패턴 제너레이터(#16)가 GetSubsystem으로 접근한다. 이동/렌더는 후속 이슈.
 */
UCLASS()
class UREBulletSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 탄환 1발 스폰 + Fragment 초기값 주입. EntityManager 없으면 무효 핸들 반환. */
	FMassEntityHandle SpawnBullet(FVector Location, FVector Velocity, float Lifetime);

	/** N발 배치 스폰. 내부는 SpawnBullet 루프(배치 최적화는 YAGNI). */
	void SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params);

private:
	/** 탄환 Archetype 최초 스폰 시 1회 생성·캐싱. */
	void EnsureArchetype(FMassEntityManager& EntityManager);

	/** 같은 World의 UMassEntitySubsystem에서 EntityManager 획득. 없으면 nullptr. */
	FMassEntityManager* GetEntityManager() const;

	FMassArchetypeHandle BulletArchetype;
};
```

### 1-3. Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp (신규)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REBulletSpawnSubsystem.h"
#include "REBulletFragments.h"
#include "MassEntitySubsystem.h"
#include "MassEntityManager.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

FMassEntityManager* UREBulletSpawnSubsystem::GetEntityManager() const
{
	UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
	return Mass ? &Mass->GetMutableEntityManager() : nullptr;
}

void UREBulletSpawnSubsystem::EnsureArchetype(FMassEntityManager& EntityManager)
{
	if (BulletArchetype.IsValid())
	{
		return;
	}
	BulletArchetype = EntityManager.CreateArchetype({
		FTransformFragment::StaticStruct(),
		FBulletSimFragment::StaticStruct(),
		FBulletRenderFragment::StaticStruct(),
		FBulletTag::StaticStruct() });
}

FMassEntityHandle UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity, float Lifetime)
{
	FMassEntityManager* EM = GetEntityManager();
	if (!EM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] SpawnBullet: EntityManager NULL"));
		return FMassEntityHandle();
	}

	EnsureArchetype(*EM);
	FMassEntityHandle Entity = EM->CreateEntity(BulletArchetype);

	EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Location);
	FBulletSimFragment& Sim = EM->GetFragmentDataChecked<FBulletSimFragment>(Entity);
	Sim.Velocity = Velocity;
	Sim.Lifetime = Lifetime;
	// FBulletRenderFragment.InstanceIndex는 기본값 INDEX_NONE 유지 (#17에서 할당).

	return Entity;
}

void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
	for (const FBulletSpawnParams& P : Params)
	{
		SpawnBullet(P.Location, P.Velocity, P.Lifetime);
	}
}
```

### 1-4. 빌드 게이트
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.
(신규 .h/.cpp 인식 안 되면 `Project_RE.uproject` 우클릭 → Generate VS project files 후 재빌드.)

### 1-5. 커밋
```bash
git add Source/Project_RE/Mass/REBulletFragments.h Source/Project_RE/Mass/REBulletSpawnSubsystem.h Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp
git commit -m "feat(M1): add FBulletTag + UREBulletSpawnSubsystem (#14)"
```

================================================================
## TASK 2: Boss 배선 + headless 검증
================================================================

### 2-1. Source/Project_RE/Core/REBossCharacter.cpp 수정

(a) 파일 상단 include 블록을 아래로 교체 — `REBulletFragments.h` / `MassEntitySubsystem.h` / `MassEntityManager.h` 3개 제거, subsystem include 추가:
```cpp
#include "REBossCharacter.h"
#include "REBulletSpawnSubsystem.h"
```

(b) 파일 상단 `namespace { constexpr int32 BulletsPerPattern = 16; }` 블록은 그대로 유지.

(c) `TriggerBulletPattern` 함수 전체를 아래로 교체:
```cpp
void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)
{
	// TODO M5: Multicast_TriggerPattern RPC로 교체 (서버→클라 시드 브로드캐스트, 총알 자체는 미전송).
	//          현재는 싱글 로컬 직접 스폰 경로.

	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::TriggerBulletPattern: UREBulletSpawnSubsystem NULL"));
		return;
	}

	switch (Pattern)
	{
	case EBulletPattern::Spiral:
		// TODO M1(#16): 나선 — 각도 증분으로 Velocity 세팅.
		break;
	case EBulletPattern::Fan:
		// TODO M1(#16): 부채꼴 — 중심각 기준 좌우 분산 Velocity.
		break;
	case EBulletPattern::Homing:
		// TODO M1(#16): 호밍 — 타깃 방향 Velocity + 추적 플래그.
		break;
	}

	// #14: 보스 위치에서 N발 스폰. Velocity=0/Lifetime=0 (패턴 수학은 #16, 이동은 #15).
	TArray<FBulletSpawnParams> Params;
	Params.Reserve(BulletsPerPattern);
	for (int32 i = 0; i < BulletsPerPattern; ++i)
	{
		Params.Add({ GetActorLocation(), FVector::ZeroVector, 0.f });
	}
	Spawner->SpawnBulletBatch(Params);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss::TriggerBulletPattern: Pattern=%d Seed=%d Start=%.2f -> spawned %d entities at %s"),
		(int32)Pattern, Seed, StartTime, BulletsPerPattern, *GetActorLocation().ToString());
}
```

### 2-2. 빌드 게이트
```bash
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-3. headless 프로브 (최종 Acceptance)
기존 GameMode BeginPlay가 Boss 스폰 + `TriggerBulletPattern(Spiral,12345,0)` 자동 호출함. PIE 없이 headless로 로그 관측. Git Bash에서 (`MSYS_NO_PATHCONV=1` 필수 — 경로 mangling 방지):
```bash
MSYS_NO_PATHCONV=1 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main \
  -game -nullrhi -unattended -nosplash -stdout -NoSound -log=RE_probe14.log &
sleep 30
grep "\[RE\]" "Saved/Logs/RE_probe14.log"
"/c/Windows/System32/taskkill.exe" //IM UnrealEditor-Cmd.exe //F
```
기대 로그 포함:
```
[RE] Boss::TriggerBulletPattern: Pattern=0 Seed=12345 Start=0.00 -> spawned 16 entities at X=0.000 Y=0.000 Z=0.000
```
합격 기준:
- `spawned 16 entities` 출력 → 배치 스폰 수 확인.
- `UREBulletSpawnSubsystem NULL` 경고 **없어야** 함 → subsystem 배선 OK.
- `EntityManager NULL` 경고 **없어야** 함 → Archetype 생성 + Fragment 초기화 경로 도달 OK.
- 로그의 `at X=/Z=` 값이 보스 스폰 위치와 일치 → Location 주입 실증.

### 2-4. 커밋
```bash
git add Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M1): route boss bullet spawn through UREBulletSpawnSubsystem (closes #14)"
```

## 완료 후
- `feature/M1-bullet-archetype-spawn` → PR. base=dev.
- PR/이슈 메타 미러링: label(`mass-entity`,`C++`) + milestone(M1) + assignee + project 전부. 6개 필드 다 채움. Reviewer 생략.
- 남은 의도된 TODO 마커: `TODO M1(#16)`(패턴 수학), `TODO M5`(Multicast RPC) — 후속 이슈 몫, 지금 구현 금지.

## 하지 말 것 (스코프 밖)
- 이동/수명 감소 로직 (#15)
- 패턴별 Velocity 수학(나선/부채꼴) (#16)
- ISM 인스턴스 생성 / InstanceIndex 할당 (#17)
- 배치 최적화 API(BatchCreateEntities) — SpawnBullet 루프로 충분
- Build.cs / .uproject 수정
