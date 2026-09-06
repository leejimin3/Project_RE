# 보스 곡사(Artillery) 탄막 패턴 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 보스가 탄환을 포물선으로 뿜어 지정 지점에 착지 예고(빨간 원)를 미리 보여준 뒤 착지 순간 범위 데미지를 주는 곡사 패턴을, 착지점 생성 6종(원형/라인/격자/나선/조준/랜덤)과 함께 추가한다.

**Architecture:** 기존 Mass 탄막 파이프를 재사용해 arc 전용 태그/프래그먼트/프로세서 3종을 얹는다. 운동은 목표주도 파라메트릭 궤적(`XY=lerp`, `Z=4H·t(1-t)`) — 중력 sim 아님. 착지 마커는 arc탄 엔티티의 렌더 부산물(별도 엔티티 없음). 착지 판정은 착지 프레임 스냅샷(XY 반경). 착지점 생성기는 순수함수로 `REBulletPattern` 네임스페이스에 추가, 보스 페이즈 로테이션에 `Artillery` 편입.

**Tech Stack:** UE5.8 C++, Mass Entity(프래그먼트/태그/프로세서), `FMassEntityQuery`, ISM 렌더, `FRandomStream`, 기존 `REBulletPatternGenerator` 순수함수.

## Global Constraints

- **테스트 하네스 없음** — 이 프로젝트는 in-engine 유닛 테스트를 안 쓴다. 검증은 ① 빌드 게이트, ② headless `-game -nullrhi` 로그 프로브, ③ 실RHI 창모드 스크린샷이다.
- **빌드 명령 (Git Bash):** `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex`
- **headless 프로브 실행 (Git Bash):** `export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"` 필수. 안 하면 `/Game/...` 경로 깨져 엉뚱한 맵 로드. 예: `"/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Maps/YourMap -game -nullrhi -unattended -nosplash -log -LogCmds="LogTemp Log" -ExecCmds="..."`
- **Mass 프로세서 게임스레드** — 씬 컴포넌트(ISM) 변형 프로세서(`ArcRender`)와 액터 호출(`TakeDamage`) 프로세서(`ArcHit`)는 `bRequiresGameThreadExecution = true` 필수. 안 하면 워커 스레드 크래시.
- **Mass Defer 커맨드버퍼 규약** — `Context.Defer().DestroyEntity()`는 프로세서 실행 중 즉시 반영되지 않고 프레임 끝 flush. 착지 프레임에 Sim이 소멸 예약해도 같은 프레임 Hit이 엔티티를 관측할 수 있다(판정 경합 해결의 핵심 전제).
- **판정은 XY 평면만** — 탑다운. Z는 시각(궤적 높이/마커 바닥)용. 히트 판정에 Z 미사용(기존 `REBulletHitProcessor` 규칙 계승).
- **`State.Dashing` 무적** — 대쉬 중 판정 스킵(탄 미파괴는 arc엔 무의미, 판정만 스킵). 기존 `RETag_State_Dashing` 재사용.
- **Gitflow** — 브랜치 `feature/M5-artillery-pattern`(dev에서 분기). dev로 PR. main 직접 커밋 금지.
- **측정 하네스 보존** — `re.Profiling.KeepFiring`은 Spiral 클로즈드루프 고정. Artillery는 미개입 — 회귀 금지.

## 파일 구조

| 파일 | 책임 | 변경 |
|------|------|------|
| `Source/Project_RE/Mass/REBulletFragments.h` | `FArcBulletFragment` + `FArcBulletTag` 추가 | 수정 |
| `Source/Project_RE/Mass/REBulletPattern.h` | `EBulletPattern::Artillery`, `EArtilleryShape` 추가 | 수정 |
| `Source/Project_RE/Mass/REBulletPatternGenerator.h` | 착지점 생성기 6종 + `FArcBulletSpawnParams` 선언 | 수정 |
| `Source/Project_RE/Mass/REBulletPatternGenerator.cpp` | 착지점 생성기 6종 구현 | 수정 |
| `Source/Project_RE/Mass/REBulletSpawnSubsystem.h` | `SpawnArcBullet`/`Batch` + arc Archetype 선언 | 수정 |
| `Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp` | arc 스폰 구현 | 수정 |
| `Source/Project_RE/Mass/REBulletRenderSubsystem.h` | `GetArcISM()`/`GetMarkerISM()` 핸들 | 수정 |
| `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp` | arc탄 ISM(주황 구체) + 마커 ISM(빨강 평면원) 생성 | 수정 |
| `Source/Project_RE/Mass/REArcSimProcessor.h/.cpp` | 궤적 보간 + 착지 소멸 | 신규 |
| `Source/Project_RE/Mass/REArcHitProcessor.h/.cpp` | 착지 프레임 스냅샷 범위 판정 | 신규 |
| `Source/Project_RE/Mass/REArcRenderProcessor.h/.cpp` | arc탄 + 마커 ISM 갱신 | 신규 |
| `Source/Project_RE/Core/REBossCharacter.h` | Artillery 페이즈 상태 + 파라미터 상수 | 수정 |
| `Source/Project_RE/Core/REBossCharacter.cpp` | Artillery 페이즈 분기 + shape 선택 + arc 스폰 | 수정 |

---

### Task 1: 착지점 생성기 6종 + enum + arc 파라미터 구조체

순수함수 착지점 생성기와 enum·구조체를 추가한다. 엔진/액터 의존 없음(FRandomStream 제외) → headless 검증가능. 이 태스크만으로 "착지점 배열을 뽑는 수학"이 완성된다.

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletPattern.h`
- Modify: `Source/Project_RE/Mass/REBulletPatternGenerator.h`
- Modify: `Source/Project_RE/Mass/REBulletPatternGenerator.cpp`

**Interfaces:**
- Consumes: `FVector`, `FRandomStream` (엔진 기본).
- Produces:
  - `enum class EBulletPattern : uint8 { Spiral, Fan, Homing, Artillery }`
  - `enum class EArtilleryShape : uint8 { Ring, Line, Grid, Spiral, PlayerAimed, Random }`
  - `struct REBulletPattern::FArcBulletSpawnParams { FVector Start, Target; float FlightTime, MaxHeight, Damage, Radius; }`
  - `TArray<FVector> REBulletPattern::GenRing(const FVector& Center, float Radius, int32 N, float GroundZ)`
  - `TArray<FVector> REBulletPattern::GenLine(const FVector& BossLoc, const FVector& PlayerLoc, float WallLen, int32 N, float GroundZ)`
  - `TArray<FVector> REBulletPattern::GenGrid(const FVector& Center, float ExtentX, float ExtentY, int32 Cols, int32 Rows, float GroundZ)`
  - `TArray<FVector> REBulletPattern::GenArcSpiral(const FVector& Center, float MaxRadius, int32 N, float GroundZ)`
  - `TArray<FVector> REBulletPattern::GenPlayerCluster(const FVector& PlayerLoc, float ClusterRadius, int32 RingN, float GroundZ)`
  - `TArray<FVector> REBulletPattern::GenRandom(const FVector& Center, float ArenaRadius, int32 N, FRandomStream& Rng, float GroundZ)`

- [ ] **Step 1: enum 확장**

`REBulletPattern.h`의 `EBulletPattern`에 `Artillery` 추가하고 `EArtilleryShape` 신규 추가:

```cpp
UENUM(BlueprintType)
enum class EBulletPattern : uint8
{
	Spiral,
	Fan,
	Homing,
	Artillery   // 곡사: 포물선 착지 + 예고 마커 + 범위 데미지
};

/** 곡사 착지점 모양. Artillery 페이즈에서 랜덤 선택. */
UENUM(BlueprintType)
enum class EArtilleryShape : uint8
{
	Ring,          // 원형 링
	Line,          // 보스→플레이어 수직 벽
	Grid,          // 아레나 균등 격자
	Spiral,        // 아르키메데스 나선(황금각)
	PlayerAimed,   // 플레이어 위치 + 주변 클러스터
	Random         // 아레나 반경 내 균등 랜덤
};
```

- [ ] **Step 2: 생성기 + 구조체 선언**

`REBulletPatternGenerator.h`의 `namespace REBulletPattern { ... }` 안, 기존 선언 아래에 추가. 파일 상단 include에 `#include "Math/RandomStream.h"` 추가:

```cpp
	/** 곡사탄 1발 스폰 파라미터(UStruct 아님 — 함수 인자 전용). */
	struct FArcBulletSpawnParams
	{
		FVector Start      = FVector::ZeroVector;
		FVector Target     = FVector::ZeroVector;
		float   FlightTime = 1.5f;
		float   MaxHeight  = 400.f;
		float   Damage     = 15.f;
		float   Radius     = 120.f;
	};

	//~ 착지점 생성기 — 전부 월드 착지점(Z=GroundZ) 배열 반환. 순수함수(FRandomStream 제외).
	/** 원형 링: 중심 C, 반경 R, N개 균등각. */
	TArray<FVector> GenRing(const FVector& Center, float Radius, int32 N, float GroundZ);
	/** 라인: 보스→플레이어 방향의 수직 벽. 중심=플레이어, 길이 WallLen, N등분. */
	TArray<FVector> GenLine(const FVector& BossLoc, const FVector& PlayerLoc, float WallLen, int32 N, float GroundZ);
	/** 격자: 중심 기준 ±Extent 범위 Cols×Rows 균등 그리드. */
	TArray<FVector> GenGrid(const FVector& Center, float ExtentX, float ExtentY, int32 Cols, int32 Rows, float GroundZ);
	/** 나선: 아르키메데스 — 각 i·137.5°, 반경 MaxRadius·√((i+1)/N). */
	TArray<FVector> GenArcSpiral(const FVector& Center, float MaxRadius, int32 N, float GroundZ);
	/** 플레이어 조준: 중심 1점 + 반경 ClusterRadius 링 RingN점. */
	TArray<FVector> GenPlayerCluster(const FVector& PlayerLoc, float ClusterRadius, int32 RingN, float GroundZ);
	/** 랜덤: 중심 기준 반경 ArenaRadius 내 균등 면적 분포 N점(√ 보정). */
	TArray<FVector> GenRandom(const FVector& Center, float ArenaRadius, int32 N, FRandomStream& Rng, float GroundZ);
```

- [ ] **Step 3: 생성기 구현**

`REBulletPatternGenerator.cpp` 끝(네임스페이스 밖이면 `REBulletPattern::` 접두어 유지 — 기존 `GenerateSpiral`과 동일 정의 스타일)에 추가:

```cpp
TArray<FVector> REBulletPattern::GenRing(const FVector& Center, float Radius, int32 N, float GroundZ)
{
	TArray<FVector> Out;
	const int32 Count = FMath::Max(N, 0);
	Out.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const float Ang = 2.f * PI * i / FMath::Max(Count, 1);
		Out.Add(FVector(Center.X + Radius * FMath::Cos(Ang),
		                Center.Y + Radius * FMath::Sin(Ang), GroundZ));
	}
	return Out;
}

TArray<FVector> REBulletPattern::GenLine(const FVector& BossLoc, const FVector& PlayerLoc, float WallLen, int32 N, float GroundZ)
{
	TArray<FVector> Out;
	const int32 Count = FMath::Max(N, 0);
	Out.Reserve(Count);

	FVector Dir = PlayerLoc - BossLoc;
	Dir.Z = 0.f;
	if (!Dir.Normalize())
	{
		Dir = FVector(1.f, 0.f, 0.f);   // 보스=플레이어 겹침 폴백
	}
	const FVector Normal(-Dir.Y, Dir.X, 0.f);          // 벽 방향(진행 수직)
	const FVector Mid(PlayerLoc.X, PlayerLoc.Y, GroundZ);
	for (int32 i = 0; i < Count; ++i)
	{
		const float f = (Count > 1) ? ((float)i / (Count - 1) - 0.5f) : 0.f;  // -0.5..0.5
		Out.Add(Mid + Normal * (f * WallLen));
	}
	return Out;
}

TArray<FVector> REBulletPattern::GenGrid(const FVector& Center, float ExtentX, float ExtentY, int32 Cols, int32 Rows, float GroundZ)
{
	TArray<FVector> Out;
	const int32 C = FMath::Max(Cols, 1);
	const int32 R = FMath::Max(Rows, 1);
	Out.Reserve(C * R);
	for (int32 r = 0; r < R; ++r)
	{
		for (int32 c = 0; c < C; ++c)
		{
			const float fx = (C > 1) ? ((float)c / (C - 1) - 0.5f) : 0.f;
			const float fy = (R > 1) ? ((float)r / (R - 1) - 0.5f) : 0.f;
			Out.Add(FVector(Center.X + fx * 2.f * ExtentX,
			                Center.Y + fy * 2.f * ExtentY, GroundZ));
		}
	}
	return Out;
}

TArray<FVector> REBulletPattern::GenArcSpiral(const FVector& Center, float MaxRadius, int32 N, float GroundZ)
{
	TArray<FVector> Out;
	const int32 Count = FMath::Max(N, 0);
	Out.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const float Ang = FMath::DegreesToRadians(i * 137.5f);            // 황금각(기존 Spiral 재사용)
		const float Rad = MaxRadius * FMath::Sqrt((float)(i + 1) / FMath::Max(Count, 1));
		Out.Add(FVector(Center.X + Rad * FMath::Cos(Ang),
		                Center.Y + Rad * FMath::Sin(Ang), GroundZ));
	}
	return Out;
}

TArray<FVector> REBulletPattern::GenPlayerCluster(const FVector& PlayerLoc, float ClusterRadius, int32 RingN, float GroundZ)
{
	TArray<FVector> Out;
	const int32 Ring = FMath::Max(RingN, 0);
	Out.Reserve(Ring + 1);
	Out.Add(FVector(PlayerLoc.X, PlayerLoc.Y, GroundZ));   // 중심(직격)
	for (int32 i = 0; i < Ring; ++i)
	{
		const float Ang = 2.f * PI * i / FMath::Max(Ring, 1);
		Out.Add(FVector(PlayerLoc.X + ClusterRadius * FMath::Cos(Ang),
		                PlayerLoc.Y + ClusterRadius * FMath::Sin(Ang), GroundZ));
	}
	return Out;
}

TArray<FVector> REBulletPattern::GenRandom(const FVector& Center, float ArenaRadius, int32 N, FRandomStream& Rng, float GroundZ)
{
	TArray<FVector> Out;
	const int32 Count = FMath::Max(N, 0);
	Out.Reserve(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		const float Ang = Rng.FRandRange(0.f, 2.f * PI);
		const float Rad = ArenaRadius * FMath::Sqrt(Rng.FRand());  // √ 보정 = 원판 균등 면적
		Out.Add(FVector(Center.X + Rad * FMath::Cos(Ang),
		                Center.Y + Rad * FMath::Sin(Ang), GroundZ));
	}
	return Out;
}
```

- [ ] **Step 4: 빌드 게이트**

Run: `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex`
Expected: `** BUILD SUCCESSFUL **`. 컴파일 에러 0.

- [ ] **Step 5: Commit**

```bash
git add Source/Project_RE/Mass/REBulletPattern.h Source/Project_RE/Mass/REBulletPatternGenerator.h Source/Project_RE/Mass/REBulletPatternGenerator.cpp
git commit -m "feat(M5): 곡사 착지점 생성기 6종 + Artillery enum"
```

---

### Task 2: arc 프래그먼트/태그 + 스폰 서브시스템 + 마커 ISM 핸들

arc탄 엔티티의 데이터(프래그먼트/태그)와 스폰 경로, 렌더 서브시스템의 arc/마커 ISM을 만든다. 이 태스크만으로 "arc탄 엔티티를 스폰하고 렌더 리소스가 준비된" 상태가 된다(아직 안 움직이고 안 보임 — 프로세서는 Task 3).

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletFragments.h`
- Modify: `Source/Project_RE/Mass/REBulletSpawnSubsystem.h`
- Modify: `Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp`
- Modify: `Source/Project_RE/Mass/REBulletRenderSubsystem.h`
- Modify: `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp`

**Interfaces:**
- Consumes: `REBulletPattern::FArcBulletSpawnParams` (Task 1), `FTransformFragment`, `FMassEntityManager`.
- Produces:
  - `struct FArcBulletFragment : FMassFragment { FVector Start, Target; float FlightTime, Elapsed, MaxHeight, Damage, Radius; }`
  - `struct FArcBulletTag : FMassTag {}`
  - `FMassEntityHandle UREBulletSpawnSubsystem::SpawnArcBullet(FVector Start, FVector Target, float FlightTime, float MaxHeight, float Damage, float Radius)`
  - `void UREBulletSpawnSubsystem::SpawnArcBulletBatch(TConstArrayView<REBulletPattern::FArcBulletSpawnParams>)`
  - `UInstancedStaticMeshComponent* UREBulletRenderSubsystem::GetArcISM() const`
  - `UInstancedStaticMeshComponent* UREBulletRenderSubsystem::GetMarkerISM() const`

- [ ] **Step 1: 프래그먼트 + 태그**

`REBulletFragments.h`의 `FBulletTag` 정의 아래에 추가:

```cpp
/** 곡사탄 시뮬 상태. 위치는 FTransformFragment, arc 파라미터는 여기. */
USTRUCT()
struct FArcBulletFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Start      = FVector::ZeroVector;
	FVector Target     = FVector::ZeroVector;
	float   FlightTime = 1.5f;
	float   Elapsed    = 0.f;
	float   MaxHeight  = 400.f;
	float   Damage     = 15.f;
	float   Radius     = 120.f;
};

/** 곡사탄 식별 태그. 기존 FBulletTag(직선탄)와 분리 — arc 프로세서만 선별. */
USTRUCT()
struct FArcBulletTag : public FMassTag
{
	GENERATED_BODY()
};
```

- [ ] **Step 2: 스폰 서브시스템 선언**

`REBulletSpawnSubsystem.h`. 파일 상단에 전방선언 추가(기존 include 아래):

```cpp
namespace REBulletPattern { struct FArcBulletSpawnParams; }
```

`public:` 블록, `SpawnBulletBatch` 선언 아래에 추가:

```cpp
	/** 곡사탄 1발 스폰 + arc Fragment 초기화. EntityManager 없으면 무효 핸들 반환. */
	FMassEntityHandle SpawnArcBullet(FVector Start, FVector Target, float FlightTime,
	                                 float MaxHeight, float Damage, float Radius);

	/** N발 배치 스폰. 내부는 SpawnArcBullet 루프. */
	void SpawnArcBulletBatch(TConstArrayView<REBulletPattern::FArcBulletSpawnParams> Params);
```

`private:` 블록에 arc Archetype 멤버 + Ensure 함수 추가(`BulletArchetype` 아래):

```cpp
	/** 곡사탄 Archetype 최초 스폰 시 1회 생성·캐싱. */
	void EnsureArcArchetype(FMassEntityManager& EntityManager);

	FMassArchetypeHandle ArcArchetype;
```

- [ ] **Step 3: 스폰 서브시스템 구현**

`REBulletSpawnSubsystem.cpp`. 상단 include에 `#include "REBulletPatternGenerator.h"` 추가. 파일 끝에 추가:

```cpp
void UREBulletSpawnSubsystem::EnsureArcArchetype(FMassEntityManager& EntityManager)
{
	if (ArcArchetype.IsValid())
	{
		return;
	}
	ArcArchetype = EntityManager.CreateArchetype({
		FTransformFragment::StaticStruct(),
		FArcBulletFragment::StaticStruct(),
		FBulletRenderFragment::StaticStruct(),
		FArcBulletTag::StaticStruct() });
}

FMassEntityHandle UREBulletSpawnSubsystem::SpawnArcBullet(FVector Start, FVector Target, float FlightTime,
                                                          float MaxHeight, float Damage, float Radius)
{
	FMassEntityManager* EM = GetEntityManager();
	if (!EM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] SpawnArcBullet: EntityManager NULL"));
		return FMassEntityHandle();
	}

	EnsureArcArchetype(*EM);
	FMassEntityHandle Entity = EM->CreateEntity(ArcArchetype);

	// 발사 순간 위치 = Start (t=0에서 Sim이 곧바로 궤적으로 덮어씀).
	EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Start);
	FArcBulletFragment& Arc = EM->GetFragmentDataChecked<FArcBulletFragment>(Entity);
	Arc.Start      = Start;
	Arc.Target     = Target;
	Arc.FlightTime = FlightTime;
	Arc.Elapsed    = 0.f;
	Arc.MaxHeight  = MaxHeight;
	Arc.Damage     = Damage;
	Arc.Radius     = Radius;

	return Entity;
}

void UREBulletSpawnSubsystem::SpawnArcBulletBatch(TConstArrayView<REBulletPattern::FArcBulletSpawnParams> Params)
{
	for (const REBulletPattern::FArcBulletSpawnParams& P : Params)
	{
		SpawnArcBullet(P.Start, P.Target, P.FlightTime, P.MaxHeight, P.Damage, P.Radius);
	}
}
```

- [ ] **Step 4: 렌더 서브시스템 핸들 선언**

`REBulletRenderSubsystem.h`. `GetISM()` 아래에 추가:

```cpp
	UInstancedStaticMeshComponent* GetArcISM() const { return ArcISM; }
	UInstancedStaticMeshComponent* GetMarkerISM() const { return MarkerISM; }
```

`private:`의 `ISM` 아래에 추가:

```cpp
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ArcISM = nullptr;     // 곡사탄(주황 구체, Z 궤적)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MarkerISM = nullptr;  // 착지 예고(빨강 평면 원)
```

- [ ] **Step 5: 렌더 서브시스템 ISM 생성**

`REBulletRenderSubsystem.cpp`의 `OnWorldBeginPlay`, 기존 ISM 준비 로그(`ISM ready`) 위에 arc/마커 ISM 생성 블록 추가. 기존 직선탄 ISM 셋업 코드를 헬퍼로 뽑지 말고(surgical), 아래를 그대로 삽입:

```cpp
	// 곡사탄 ISM — 주황 구체(직선탄 빨강과 구분). Z 살아있어 궤적 높이가 보인다.
	ArcISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	ArcISM->SetupAttachment(ISM);
	ArcISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ArcISM->bAffectDynamicIndirectLighting = false;
	ArcISM->bAffectDistanceFieldLighting = false;
	ArcISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ArcISM->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dyn = ArcISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.5f, 0.f));  // 주황
		}
	}

	// 착지 마커 ISM — 빨강 평면 원. Cylinder를 납작하게(Z scale 축소) 눌러 디스크로.
	MarkerISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	MarkerISM->SetupAttachment(ISM);
	MarkerISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MarkerISM->bAffectDynamicIndirectLighting = false;
	MarkerISM->bAffectDistanceFieldLighting = false;
	MarkerISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		MarkerISM->SetStaticMesh(Mesh);
	}
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dyn = MarkerISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);
		}
	}
```

> **참고**: `/Engine/BasicShapes/Cylinder`는 반경 50 / 높이 100. Render 프로세서(Task 3)가 인스턴스 스케일로 반경=Radius, 높이=납작(Z≈0.02)을 준다.

- [ ] **Step 6: 빌드 게이트**

Run: (Global Constraints의 빌드 명령)
Expected: `** BUILD SUCCESSFUL **`.

- [ ] **Step 7: Commit**

```bash
git add Source/Project_RE/Mass/REBulletFragments.h Source/Project_RE/Mass/REBulletSpawnSubsystem.h Source/Project_RE/Mass/REBulletSpawnSubsystem.cpp Source/Project_RE/Mass/REBulletRenderSubsystem.h Source/Project_RE/Mass/REBulletRenderSubsystem.cpp
git commit -m "feat(M5): 곡사탄 프래그먼트/태그 + 스폰 경로 + arc·마커 ISM"
```

---

### Task 3: ArcSim + ArcRender 프로세서 (탄 날아가고 마커 보임)

궤적 보간 시뮬과 렌더 프로세서를 만든다. 판정(Task 4) 없이도 이 태스크로 "arc탄이 포물선으로 날아 착지점에 떨어지고, 발사 순간부터 빨간 마커가 보이는" 시각이 완성된다.

**Files:**
- Create: `Source/Project_RE/Mass/REArcSimProcessor.h`
- Create: `Source/Project_RE/Mass/REArcSimProcessor.cpp`
- Create: `Source/Project_RE/Mass/REArcRenderProcessor.h`
- Create: `Source/Project_RE/Mass/REArcRenderProcessor.cpp`

**Interfaces:**
- Consumes: `FArcBulletTag`, `FArcBulletFragment` (Task 2), `FTransformFragment`, `UREBulletRenderSubsystem::GetArcISM/GetMarkerISM` (Task 2).
- Produces:
  - `class UREArcSimProcessor : public UMassProcessor` (착지 프레임 `Elapsed>=FlightTime`에 `Defer().DestroyEntity`)
  - `class UREArcRenderProcessor : public UMassProcessor`

- [ ] **Step 1: ArcSim 헤더**

Create `REArcSimProcessor.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REArcSimProcessor.generated.h"

/**
 *  곡사탄 궤적 시뮬. 목표주도 파라메트릭: XY=lerp(Start,Target,t), Z=baseZ+4H·t(1-t).
 *  t=Elapsed/FlightTime. Elapsed>=FlightTime이면 착지 → Defer 소멸.
 *  AllNetModes: 시뮬은 서버/클라 동일. 소멸도 여기서 담당(Hit은 판정만).
 */
UCLASS()
class UREArcSimProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcSimProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
```

- [ ] **Step 2: ArcSim 구현**

Create `REArcSimProcessor.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment

UREArcSimProcessor::UREArcSimProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
}

void UREArcSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcSimProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcSim);

	EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Ctx)
	{
		const float Dt = Ctx.GetDeltaTimeSeconds();
		const int32 Num = Ctx.GetNumEntities();
		const TArrayView<FTransformFragment> Transforms = Ctx.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FArcBulletFragment> Arcs       = Ctx.GetMutableFragmentView<FArcBulletFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			FArcBulletFragment& A = Arcs[i];
			A.Elapsed += Dt;
			const float t = (A.FlightTime > 0.f) ? FMath::Min(A.Elapsed / A.FlightTime, 1.f) : 1.f;

			const float X = FMath::Lerp(A.Start.X, A.Target.X, t);
			const float Y = FMath::Lerp(A.Start.Y, A.Target.Y, t);
			const float BaseZ = FMath::Lerp(A.Start.Z, A.Target.Z, t);
			const float Z = BaseZ + 4.f * A.MaxHeight * t * (1.f - t);   // 포물선 높이

			Transforms[i].GetMutableTransform().SetLocation(FVector(X, Y, Z));

			if (A.Elapsed >= A.FlightTime)
			{
				// 착지. Defer 소멸(커맨드버퍼 — 이 프레임 Hit이 아직 관측 가능).
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
			}
		}
	});
}
```

- [ ] **Step 3: ArcRender 헤더**

Create `REArcRenderProcessor.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REArcRenderProcessor.generated.h"

/**
 *  곡사탄 렌더. arc탄 → ArcISM(주황 구체, Z 궤적). 착지 마커 → MarkerISM(빨강 평면 원, Target 바닥).
 *  arc탄 1개당 탄 인스턴스 1 + 마커 인스턴스 1. 매 프레임 리빌드(기존 RenderProcessor 패턴).
 *  Standalone|Client: 데디서버 skip. ISM 변형은 GT 전용.
 */
UCLASS()
class UREArcRenderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcRenderProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
```

- [ ] **Step 4: ArcRender 구현**

Create `REArcRenderProcessor.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcRenderProcessor.h"
#include "REBulletFragments.h"
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Mass/EntityFragments.h"  // FTransformFragment
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"

namespace
{
	/** 곡사탄 구체 스케일 — 직선탄(0.5)보다 약간 크게(0.7) 눈에 띄게. */
	constexpr float ArcBulletScale = 0.7f;
	/** 마커 원판 두께 스케일 — Cylinder(높이 100)를 거의 평면으로. */
	constexpr float MarkerThickness = 0.02f;
	/** Cylinder 기본 반경(cm) — /Engine/BasicShapes/Cylinder. 스케일 = Radius/50. */
	constexpr float CylinderBaseRadius = 50.f;
	/** 마커 바닥 오프셋(cm) — Target.Z에서 살짝 띄워 Z-fighting 방지. */
	constexpr float MarkerZOffset = 2.f;
}

UREArcRenderProcessor::UREArcRenderProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);
	bRequiresGameThreadExecution = true;   // ISM 변형은 GT 전용
}

void UREArcRenderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcRender);

	UWorld* World = EntityManager.GetWorld();
	UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
	UInstancedStaticMeshComponent* ArcISM    = RS ? RS->GetArcISM() : nullptr;
	UInstancedStaticMeshComponent* MarkerISM = RS ? RS->GetMarkerISM() : nullptr;
	if (!ArcISM || !MarkerISM)
	{
		return;  // 데디서버 등 ISM 없으면 no-op
	}

	// 1) live arc탄 → 탄 트랜스폼 + 마커 트랜스폼 수집.
	TArray<FTransform> BulletXf;
	TArray<FTransform> MarkerXf;
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FArcBulletFragment> A = Ctx.GetFragmentView<FArcBulletFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(ArcBulletScale));
			BulletXf.Add(B);

			// 마커: Target 바닥, 반경=Radius(Cylinder 스케일), 납작.
			const float RadScale = A[i].Radius / CylinderBaseRadius;
			FTransform M;
			M.SetLocation(FVector(A[i].Target.X, A[i].Target.Y, A[i].Target.Z + MarkerZOffset));
			M.SetScale3D(FVector(RadScale, RadScale, MarkerThickness));
			MarkerXf.Add(M);
		}
	});

	// 2) 두 ISM 인스턴스 수를 각각 맞춤(꼬리 add/remove → 타 인덱스 불변).
	auto SyncISM = [](UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xf)
	{
		const int32 M = Xf.Num();
		int32 Count = ISM->GetInstanceCount();
		while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
		while (Count > M) { ISM->RemoveInstance(Count - 1);                               --Count; }
		for (int32 i = 0; i < M; ++i)
		{
			ISM->UpdateInstanceTransform(i, Xf[i], /*bWorldSpace=*/true,
				/*bMarkRenderStateDirty=*/(i == M - 1), /*bTeleport=*/true);
		}
	};
	SyncISM(ArcISM, BulletXf);
	SyncISM(MarkerISM, MarkerXf);
}
```

- [ ] **Step 5: 빌드 게이트**

Run: (Global Constraints의 빌드 명령)
Expected: `** BUILD SUCCESSFUL **`.

- [ ] **Step 6: 스크린샷 검증 (실RHI 창모드)**

Task 5(보스 발사 통합) 전이라 아직 자동 발사 없음. 이 스텝은 **Task 5 완료 후** 수행한다. 여기서는 빌드만 게이트하고, 시각 검증은 Task 5 Step 3에서 arc탄 궤적 높이 + 빨강 마커 원을 PNG로 확인한다. (`-nullrhi`는 ISM Bounds=0 오진 함정 — 실RHI `-windowed` 필수.)

- [ ] **Step 7: Commit**

```bash
git add Source/Project_RE/Mass/REArcSimProcessor.h Source/Project_RE/Mass/REArcSimProcessor.cpp Source/Project_RE/Mass/REArcRenderProcessor.h Source/Project_RE/Mass/REArcRenderProcessor.cpp
git commit -m "feat(M5): 곡사탄 궤적 시뮬 + arc·마커 렌더 프로세서"
```

---

### Task 4: ArcHit 프로세서 (착지 범위 데미지)

착지 프레임에 마커 반경 안 플레이어에게 범위 데미지를 준다. `ExecuteAfter(ArcSim)` + Defer 커맨드버퍼 규약으로 Sim이 소멸 예약한 착지탄을 같은 프레임에 관측해 판정한다.

**Files:**
- Create: `Source/Project_RE/Mass/REArcHitProcessor.h`
- Create: `Source/Project_RE/Mass/REArcHitProcessor.cpp`

**Interfaces:**
- Consumes: `FArcBulletTag`, `FArcBulletFragment` (Task 2), `UREArcSimProcessor` (Task 3, ExecuteAfter), `ARECharacterBase::TakeDamage`, `RETag_State_Dashing` (기존).
- Produces: `class UREArcHitProcessor : public UMassProcessor`.

- [ ] **Step 1: ArcHit 헤더**

Create `REArcHitProcessor.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "REArcHitProcessor.generated.h"

/**
 *  곡사탄 착지 판정. Elapsed>=FlightTime(착지 프레임)인 탄만, 플레이어 XY가 Target 반경 안이면
 *  TakeDamage(범위 데미지). 소멸은 Sim이 담당(여기선 판정만) — ExecuteAfter(ArcSim) + Defer 규약으로
 *  Sim이 소멸 예약한 같은 프레임에 엔티티가 아직 살아있어 관측 가능.
 *  Standalone|Server: 서버 권위 판정. GT 전용(TakeDamage 액터 호출).
 */
UCLASS()
class UREArcHitProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREArcHitProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
```

- [ ] **Step 2: ArcHit 구현**

Create `REArcHitProcessor.cpp`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REArcHitProcessor.h"
#include "REArcSimProcessor.h"
#include "REBulletFragments.h"
#include "MassExecutionContext.h"
#include "Core/RECharacterBase.h"
#include "Abilities/REGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

UREArcHitProcessor::UREArcHitProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	bRequiresGameThreadExecution = true;   // TakeDamage 액터 호출 → GT 전용

	// Sim이 위치·착지 상태를 갱신한 뒤 같은 프레임에 판정.
	ExecutionOrder.ExecuteAfter.Add(UREArcSimProcessor::StaticClass()->GetFName());
}

void UREArcHitProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FArcBulletFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FArcBulletTag>(EMassFragmentPresence::All);
}

void UREArcHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ArcHit);

	UWorld* World = EntityManager.GetWorld();
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	ARECharacterBase* Player = Cast<ARECharacterBase>(Pawn);
	if (!Player)
	{
		return;  // 플레이어 없으면 no-op
	}

	// 대쉬 무적 — State.Dashing이면 이번 프레임 착지 판정 전체 스킵.
	const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
	if (ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing))
	{
		return;
	}

	const FVector PlayerLoc = Player->GetActorLocation();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FArcBulletFragment> Arcs = Ctx.GetFragmentView<FArcBulletFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			const FArcBulletFragment& A = Arcs[i];
			if (A.Elapsed < A.FlightTime)
			{
				continue;   // 아직 비행 중 — 착지 프레임만 판정
			}
			// 착지: 마커 반경 안 플레이어면 범위 데미지. XY 평면 거리(탑다운).
			if (FVector::DistSquaredXY(A.Target, PlayerLoc) <= A.Radius * A.Radius)
			{
				const float Applied = Player->TakeDamage(A.Damage, FDamageEvent(), nullptr, nullptr);
				UE_LOG(LogTemp, Log, TEXT("[RE] ArcHit: Applied=%.0f R=%.0f"), Applied, A.Radius);
			}
		}
	});
}
```

- [ ] **Step 3: 빌드 게이트**

Run: (Global Constraints의 빌드 명령)
Expected: `** BUILD SUCCESSFUL **`.

- [ ] **Step 4: Commit**

```bash
git add Source/Project_RE/Mass/REArcHitProcessor.h Source/Project_RE/Mass/REArcHitProcessor.cpp
git commit -m "feat(M5): 곡사탄 착지 범위 판정 프로세서"
```

---

### Task 5: 보스 Artillery 페이즈 통합

보스 페이즈 로테이션에 Artillery를 편입한다. Artillery 페이즈에서 `EArtilleryShape`를 랜덤 선택 → 착지점 생성 → `SpawnArcBulletBatch`. 이 태스크로 전체 기능이 인게임에서 동작한다.

**Files:**
- Modify: `Source/Project_RE/Core/REBossCharacter.h`
- Modify: `Source/Project_RE/Core/REBossCharacter.cpp`

**Interfaces:**
- Consumes: `EBulletPattern::Artillery`, `EArtilleryShape` (Task 1), `REBulletPattern::Gen*` + `FArcBulletSpawnParams` (Task 1), `UREBulletSpawnSubsystem::SpawnArcBulletBatch` (Task 2).
- Produces: 없음(최종 통합).

- [ ] **Step 1: 헤더 — Artillery 상태 + 파라미터 상수**

`REBossCharacter.h`의 `private:` 페이즈 상수 블록(`FanFireIntervalSec` 아래)에 추가:

```cpp
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

	/** 현재 페이즈 Artillery 1회 일제사(FireCurrentPattern에서 분기). */
	void FireArtillery();
```

- [ ] **Step 2: cpp — BeginPhase에 Artillery 편입**

`REBossCharacter.cpp`의 `BeginPhase`, `else` 블록의 패턴 선택을 3종(Spiral/Fan/Artillery)으로 확장. 기존:

```cpp
		CurrentPhasePattern = (PhaseRng.RandRange(0, 1) == 0)
			? EBulletPattern::Spiral : EBulletPattern::Fan;
```

를 아래로 교체:

```cpp
		switch (PhaseRng.RandRange(0, 2))
		{
		case 0:  CurrentPhasePattern = EBulletPattern::Spiral; break;
		case 1:  CurrentPhasePattern = EBulletPattern::Fan; break;
		default: CurrentPhasePattern = EBulletPattern::Artillery; break;
		}
		if (CurrentPhasePattern == EBulletPattern::Artillery)
		{
			CurrentArtilleryShape = (EArtilleryShape)PhaseRng.RandRange(
				(int32)EArtilleryShape::Ring, (int32)EArtilleryShape::Random);
		}
```

그리고 페이즈 시간/발사간격 분기(기존 `const bool bSpiral = ...` 블록)를 Artillery까지 처리하도록 교체. 기존:

```cpp
	const bool bSpiral = (CurrentPhasePattern == EBulletPattern::Spiral);
	const float PhaseSec  = bSpiral ? SpiralPhaseSec : FanPhaseSec;
	const float FireInterval = bSpiral ? REBulletPattern::FireIntervalSec() : FanFireIntervalSec;

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: %s %.1fs"),
		bSpiral ? TEXT("Spiral") : TEXT("Fan"), PhaseSec);
```

를 아래로 교체:

```cpp
	float PhaseSec = SpiralPhaseSec;
	float FireInterval = REBulletPattern::FireIntervalSec();
	const TCHAR* PhaseName = TEXT("Spiral");
	switch (CurrentPhasePattern)
	{
	case EBulletPattern::Fan:
		PhaseSec = FanPhaseSec; FireInterval = FanFireIntervalSec; PhaseName = TEXT("Fan"); break;
	case EBulletPattern::Artillery:
		PhaseSec = ArtilleryPhaseSec; FireInterval = ArtilleryFireInterval; PhaseName = TEXT("Artillery"); break;
	default: break;   // Spiral 기본값
	}

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: %s %.1fs"), PhaseName, PhaseSec);
```

- [ ] **Step 3: cpp — FireCurrentPattern에서 Artillery 분기**

`FireCurrentPattern`을 Artillery면 `FireArtillery`로 위임하도록 교체. 기존:

```cpp
void AREBossCharacter::FireCurrentPattern()
{
	TriggerBulletPattern(CurrentPhasePattern, /*Seed=*/12345, /*StartTime=*/0.f);
}
```

를:

```cpp
void AREBossCharacter::FireCurrentPattern()
{
	if (CurrentPhasePattern == EBulletPattern::Artillery)
	{
		FireArtillery();
		return;
	}
	TriggerBulletPattern(CurrentPhasePattern, /*Seed=*/12345, /*StartTime=*/0.f);
}
```

- [ ] **Step 4: cpp — FireArtillery 구현**

`FireCurrentPattern` 아래에 추가. 상단 include에 `#include "Mass/EntityFragments.h"` 불필요 — 이미 `REBulletPatternGenerator.h`가 `FVector`/생성기 노출. `GameFramework/PlayerController.h`는 기존 include됨:

```cpp
void AREBossCharacter::FireArtillery()
{
	if (bIsDead)
	{
		return;
	}
	UREBulletSpawnSubsystem* Spawner = GetWorld() ? GetWorld()->GetSubsystem<UREBulletSpawnSubsystem>() : nullptr;
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RE] Boss::FireArtillery: Spawner NULL"));
		return;
	}

	const FVector BossLoc = GetActorLocation();
	const float GroundZ = BossLoc.Z + MarkerGroundOffset;   // 착지 평면(보스 캡슐 바닥 근사)

	// 플레이어 위치(조준/라인용). 없으면 보스 앞쪽 폴백.
	FVector PlayerLoc = BossLoc + FVector(300.f, 0.f, 0.f);
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (const APawn* P = PC->GetPawn())
		{
			PlayerLoc = P->GetActorLocation();
		}
	}

	// 모양별 착지점 생성.
	TArray<FVector> Targets;
	switch (CurrentArtilleryShape)
	{
	case EArtilleryShape::Ring:
		Targets = REBulletPattern::GenRing(BossLoc, /*Radius=*/500.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::Line:
		Targets = REBulletPattern::GenLine(BossLoc, PlayerLoc, /*WallLen=*/900.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::Grid:
		Targets = REBulletPattern::GenGrid(BossLoc, /*ExtentX=*/600.f, /*ExtentY=*/600.f, /*Cols=*/4, /*Rows=*/3, GroundZ);
		break;
	case EArtilleryShape::Spiral:
		Targets = REBulletPattern::GenArcSpiral(BossLoc, /*MaxRadius=*/600.f, ArtilleryCount, GroundZ);
		break;
	case EArtilleryShape::PlayerAimed:
		Targets = REBulletPattern::GenPlayerCluster(PlayerLoc, /*ClusterRadius=*/150.f, /*RingN=*/4, GroundZ);
		break;
	case EArtilleryShape::Random:
		Targets = REBulletPattern::GenRandom(BossLoc, /*ArenaRadius=*/800.f, ArtilleryCount, PhaseRng, GroundZ);
		break;
	}

	// 착지점 → arc 스폰 파라미터. 발사 원점 = 보스.
	TArray<REBulletPattern::FArcBulletSpawnParams> Shots;
	Shots.Reserve(Targets.Num());
	for (const FVector& T : Targets)
	{
		REBulletPattern::FArcBulletSpawnParams P;
		P.Start      = BossLoc;
		P.Target     = T;
		P.FlightTime = ArtilleryFlightTime;
		P.MaxHeight  = ArtilleryMaxHeight;
		P.Damage     = ArtilleryDamage;
		P.Radius     = ArtilleryRadius;
		Shots.Add(P);
	}
	Spawner->SpawnArcBulletBatch(Shots);

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Artillery: Shape=%d N=%d"),
		(int32)CurrentArtilleryShape, Shots.Num());
}
```

- [ ] **Step 5: 빌드 게이트**

Run: (Global Constraints의 빌드 명령)
Expected: `** BUILD SUCCESSFUL **`.

- [ ] **Step 6: headless 런타임 프로브**

Git Bash에서(`export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"` 먼저):

```bash
"/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" <YourGameMap> -game -nullrhi -unattended -nosplash -log -LogCmds="LogTemp Log" 2>&1 | grep -E "Boss Artillery|ArcHit"
```

Expected: `[RE] Boss Artillery: Shape=N N=M` 로그가 뜨고(페이즈 로테이션에 Artillery 등장), 플레이어가 착지 반경 안일 때 `[RE] ArcHit: Applied=15` 관측. Shape 값이 페이즈마다 0~5 범위에서 바뀌는지 확인.

> ⚠️ **판정 경합 실측**: `ArcHit` 로그가 실제로 찍히면 "Sim 소멸 예약 + 같은 프레임 Hit 관측" 규약이 성립한 것. 안 찍히면(착지했는데 Applied 없음) `ExecutionOrder`/Defer 타이밍 재점검 — Hit을 `ExecuteBefore(ArcSim)`로 바꾸거나 Sim 소멸을 `Elapsed >= FlightTime` → 다음 프레임으로 1틱 지연.

- [ ] **Step 7: 스크린샷 검증 (실RHI 창모드)**

실RHI `-windowed`로 실행, `FScreenshotRequest`(또는 콘솔 `HighResShot`)로 PNG 캡처. 확인 항목:
1. 주황 arc탄이 포물선 궤적으로 **높이를 갖고** 날아감(Z 살아있음).
2. 발사 순간부터 착지점에 **빨강 평면 원 마커**가 보임.
3. 모양(원형/라인/격자/나선)이 착지점 배치로 식별됨.

(`-nullrhi`는 ISM Bounds=0 오진 함정 — 실RHI 필수. `[[ue_visual_verify_screenshot]]` 방식.)

- [ ] **Step 8: Commit**

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "feat(M5): 보스 Artillery 페이즈 통합 + 모양 6종 발사"
```

---

## Self-Review

**1. Spec coverage:**
- 포물선 궤적(목표주도) → Task 3 ArcSim `Z=4H·t(1-t)` ✅
- 착지 예고 마커 → Task 2 MarkerISM + Task 3 ArcRender 마커 인스턴스 ✅
- 착지 범위 데미지 → Task 4 ArcHit ✅
- 모양 4종(원형/라인/격자/나선) → Task 1 Gen* + Task 5 shape 분기 ✅
- 플레이어 조준 → Task 1 GenPlayerCluster + Task 5 ✅
- 랜덤 → Task 1 GenRandom(PhaseRng) + Task 5 ✅
- 페이즈 통합 → Task 5 BeginPhase 3종 분기 ✅
- Mass 재사용 → Task 2 arc Archetype, 기존 스폰/렌더 인프라 확장 ✅
- 판정 경합 해결 → Task 3/4 Defer 커맨드버퍼 규약 + Task 5 Step 6 실측 게이트 ✅

**2. Placeholder scan:** `<YourGameMap>` = 실행 맵 경로(환경 의존, 실행자가 채움 — headless 프로브 관례). 그 외 TBD/TODO 없음. 코드 스텝 전부 실 코드.

**3. Type consistency:**
- `EArtilleryShape` 6값(Ring..Random) — Task 1 정의, Task 5 `RandRange(Ring, Random)` + switch 일치 ✅
- `FArcBulletSpawnParams{Start,Target,FlightTime,MaxHeight,Damage,Radius}` — Task 1 선언, Task 2 소비, Task 5 생성 필드명 일치 ✅
- `FArcBulletFragment` 필드 = SpawnParams와 동일 + `Elapsed` — Task 2 정의, Task 3/4 소비 일치 ✅
- `GetArcISM`/`GetMarkerISM` — Task 2 정의, Task 3 소비 일치 ✅
- `SpawnArcBulletBatch(TConstArrayView<FArcBulletSpawnParams>)` — Task 2 정의, Task 5 호출 일치 ✅

## 미해결 / 후속

- **판정 경합**은 Task 5 Step 6에서 실측 게이트로 확정(폴백 경로 명시). 구현 중 반증되면 그 스텝 지침대로 조정.
- 폭발 VFX, 리드 예측 조준, 다중 모양 조합, 데칼 마커, arc탄 Lumen 제외(직선탄처럼 GPU 스파이크 나면) — 후속.
- 파라미터 상수(반경/고도/개수/페이즈시간)는 전부 헤더 상수 — 플레이 후 튜닝.
