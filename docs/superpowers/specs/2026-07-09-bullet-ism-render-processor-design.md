# M1 #17 ISM 렌더 Processor + 탄막 데모 영상 Design

**이슈:** #17 (M1: Mass 보스 탄막 스폰 + 이동 (싱글)) — 마일스톤 마지막
**날짜:** 2026-07-09
**선행:** #14(Fragment/Archetype 스폰), #15(SimProcessor 이동/수명), #16(패턴 제너레이터) 완료

## 목표

`UREBulletRenderProcessor::Execute` 스텁을 채워 탄환 트랜스폼을 ISM(Instanced Static Mesh) 인스턴스로 렌더. 싱글(standalone)·클라만 그리고, 데디서버는 skip(`ExecutionFlags=5`). M1 산출물인 **탄막 데모 영상**을 위한 지속 발사 씬까지 코드로 마련. 영상 녹화 자체는 수동 단계.

## 배경 (현재 상태)

- 위치는 `FTransformFragment`에 삶. `UREBulletSimProcessor`(#15)가 매틱 전진, Lifetime 만료 시 `Defer().DestroyEntity`로 파괴.
- `UREBulletRenderProcessor`는 M0 스텁: `ExecutionFlags=Standalone|Client(5)`, Query가 `FBulletSimFragment(RO)`+`FBulletRenderFragment(RW)` 요구, `Execute`는 `UE_LOG` 스텁.
- `FBulletRenderFragment.InstanceIndex`(INDEX_NONE 기본)는 M0에서 #17용으로 예약됨.
- `AREBossCharacter::TriggerBulletPattern`은 1회성 버스트. 주기 발사 루프는 없음(다른 마일스톤도 소유 안 함).

## 설계 결정 (브레인스토밍 확정)

| # | 결정 | 대안 기각 사유 |
|---|------|----------------|
| Q1 | **매틱 재구성**: 매 프레임 live 탄환 수 M에 ISM 인스턴스 수를 맞추고, 인스턴스 i = i번째 live 탄환으로 UpdateTransform | 영속 인스턴스(스폰시 Add/파괴시 Remove)는 `RemoveInstance`의 꼬리-swap이 타 탄환 `InstanceIndex`를 무효화 → Observer Processor + index remap 필요, 복잡도 큼. M1 싱글·수백발엔 과함 |
| Q2 | **`UREBulletRenderSubsystem`(UWorldSubsystem)** 가 홀더 액터+ISM 소유, `GetISM()` 노출 | Processor는 액터 아님 → ISM 직접 소유 불가. TActorIterator 탐색보다 서브시스템 O(1) 획득이 깔끔(기존 `REBulletSpawnSubsystem` 패턴과 대칭) |
| Q3 | **엔진 구체 + 빨강 Dynamic Material** | 커스텀 애셋은 에디터 작업 필요(헤드리스 밖). 데모엔 빨강 구체로 충분 |
| Q4 | **코드 + 헤드리스 검증 + 데모 발사 루프**. 영상 녹화는 수동 | 발사 루프 소유 마일스톤 없음 → 없으면 #17이 데모 씬 마련 |

## 변경 범위

**신규 2 파일:** `Mass/REBulletRenderSubsystem.h/.cpp`
**수정 3 파일:** `Mass/REBulletRenderProcessor.cpp`, `Core/REGameMode.cpp`(데모 발사 루프), `Core/REBossCharacter.*`(선택 — 발사 위치 결정에 따라)

`REBulletFragments.h`는 **변경 없음**. `FBulletRenderFragment`는 매틱 재구성 전략에선 미사용(InstanceIndex를 프레임마다 i로 재배정하므로 프래그먼트 필드에 저장 불요) — 필드 자체는 M5 시드 경로 여지로 남겨둠, 삭제하지 않음.

### 1. `UREBulletRenderSubsystem` (신규, UWorldSubsystem)

```cpp
// REBulletRenderSubsystem.h
UCLASS()
class UREBulletRenderSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    UInstancedStaticMeshComponent* GetISM() const { return ISM; }

private:
    UPROPERTY() TObjectPtr<AActor> Holder = nullptr;
    UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> ISM = nullptr;
};
```

`OnWorldBeginPlay`:
- 데디서버(`InWorld.GetNetMode() == NM_DedicatedServer`)면 **즉시 return** — 서버는 렌더 안 함(Processor도 flag 5로 skip이나, 홀더/ISM도 안 만듦).
- 게임/에디터 월드가 아니면(에디터 프리뷰 등) 생성 skip: `InWorld.IsGameWorld()` 가드.
- 홀더 `AActor` 스폰 → `UInstancedStaticMeshComponent`를 NewObject로 만들어 RootComponent로 지정 후 RegisterComponent.
- 메시: `LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"))`, `ISM->SetStaticMesh(...)`.
- 머티리얼: 베이스 `/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial` → `CreateDynamicMaterialInstance` → `SetVectorParameterValue("Color", 빨강)` → `ISM->SetMaterial(0, DynMat)`. (파라미터명은 구현 중 BasicShapeMaterial에서 실제 확인; 없으면 단색 커스텀 대신 기본 머티리얼 유지하고 색은 후속.)
- 인스턴스 크기: 탄환은 홀더 스케일이 아닌 인스턴스 트랜스폼 스케일로 축소 — Sphere(반경 50cm 기본)을 ~0.2배(반경 10cm)로. Execute의 UpdateTransform이 스케일 포함 트랜스폼 전달.

### 2. `UREBulletRenderProcessor` 수정

**ConfigureQueries** (`.cpp`):
```cpp
EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
```
- 기존 `FBulletSimFragment(RO)` + `FBulletRenderFragment(RW)` 요구 → `FTransformFragment(RO)` + `FBulletTag(All)`로 교체. 위치만 읽으면 되고, 재구성 전략상 RenderFragment 불요.
- `#include "Mass/EntityFragments.h"`(FTransformFragment) 추가 — MassCore, Build.cs 불요(#14/#15 확인).

**Execute** (`.cpp`):
```cpp
UWorld* World = EntityManager.GetWorld();
UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
UInstancedStaticMeshComponent* ISM = RS ? RS->GetISM() : nullptr;
if (!ISM) { return; }  // 데디서버 등 ISM 없으면 no-op

// 1) live 탄환 트랜스폼 수집 (청크 가로질러 누적)
TArray<FTransform> Xf;
EntityQuery.ForEachEntityChunk(Context, [&Xf](FMassExecutionContext& Ctx)
{
    const int32 Num = Ctx.GetNumEntities();
    const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
    for (int32 i = 0; i < Num; ++i)
    {
        FTransform B = T[i].GetTransform();
        B.SetScale3D(FVector(BulletScale));  // 탄환 크기 통일
        Xf.Add(B);
    }
});

// 2) 인스턴스 수를 M에 맞춤 (꼬리에서 add/remove → swap 없음)
const int32 M = Xf.Num();
int32 Count = ISM->GetInstanceCount();
while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
while (Count > M) { ISM->RemoveInstance(Count - 1); --Count; }

// 3) i번째 인스턴스 = i번째 live 탄환
for (int32 i = 0; i < M; ++i)
{
    ISM->UpdateInstanceTransform(i, Xf[i], /*bWorldSpace=*/true,
        /*bMarkRenderStateDirty=*/(i == M - 1), /*bTeleport=*/true);
}
```

**로직 근거:**
- **수집 후 일괄** — `ForEachEntityChunk`는 청크 단위 콜백. 전역 인스턴스 인덱스가 청크 경계를 넘어 연속돼야 하므로 먼저 `Xf`로 모은 뒤 ISM 갱신.
- **꼬리 add/remove** — `RemoveInstance(last)`는 swap이 자기 자신이라 타 인덱스 불변. 앞에서 지우면 뒷 인스턴스가 당겨져 매핑 붕괴.
- **`bMarkRenderStateDirty`는 마지막 1회만** — 매 인스턴스 dirty는 렌더 스테이트 재생성 폭주. 마지막에 한 번 flush.
- **`bWorldSpace=true`** — 홀더 액터가 원점 아닐 수 있으니 월드 트랜스폼 직접 지정.
- **`bTeleport=true`** — 탄막은 물리 보간 불요, 순간이동.

`BulletScale`(예 0.2f)는 Processor 상수 or 서브시스템 노출값 — M1은 Processor 로컬 상수로 충분.

### 3. 데모 발사 루프 (`REGameMode.cpp`)

BeginPlay의 #14~#16 프로브 로그는 검증 완료물 — 유지하되, 지속 발사를 위해 타이머 추가:
```cpp
// 데모: 보스가 0.1초마다 Spiral 발사 (BaseAngle 누적으로 회전 탄막)
GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, [Boss]()
{
    if (Boss) { Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f); }
}, 0.1f, /*bLoop=*/true);
```
- `AREGameMode`에 `FTimerHandle DemoFireTimer` 멤버 추가.
- 보스 포인터 캡처(약참조 고려 — 데모라 단순 캡처로 충분, 필요 시 TWeakObjectPtr).
- Spiral만으로 회전 탄막 데모 충분. Fan 병행은 선택.

### 4. .uproject / 모듈

변경 불요. `UInstancedStaticMeshComponent`는 Engine 모듈(기존 링크). MassGameplay 페이즈 드라이버가 RenderProcessor도 매틱 구동(#15에서 활성화됨) — 단 RenderProcessor는 어느 페이즈? `EMassProcessingPhase` 기본(PrePhysics) OK. SimProcessor(위치 갱신) → RenderProcessor(읽기) 순서 보장 필요 시 `ExecuteAfter`로 SimProcessor 지정 검토(구현 중 확인; 같은 프레임 1틱 지연은 데모상 무시 가능).

## API 근거 (UE 5.8 엔진 헤더 확인)

`E:/UE_5.8/Engine/Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h` 실물:

| API | 근거 |
|-----|------|
| `int32 AddInstance(const FTransform&, bool bWorldSpace=false)` | `:271` |
| `bool RemoveInstance(int32 InstanceIndex)` | `:417` |
| `bool UpdateInstanceTransform(int32, const FTransform&, bool bWorldSpace=false, bool bMarkRenderStateDirty=false, bool bTeleport=false)` | `:375` |
| `int32 GetInstanceCount() const` | `:435` |
| 메시 `/Engine/BasicShapes/Sphere.Sphere` | `Engine/Content/BasicShapes/Sphere.uasset` 존재 |
| 머티리얼 `/Engine/BasicShapes/BasicShapeMaterial` | `Engine/Content/BasicShapes/BasicShapeMaterial.uasset` 존재 |

## 검증 (완료 기준)

### 헤드리스 (`-game -nullrhi`, `[[headless-runtime-probe]]`)
데모 발사 루프로 탄막 스폰 중, RenderProcessor Execute가 매틱:
```
[RE] RenderProbe: live=N  ISM.Count=N   ← 두 수 일치
```
- Execute 끝에 `UE_LOG`로 `M`(수집 수)과 `ISM->GetInstanceCount()` 로그.
- **핵심 실증:** ISM 인스턴스 수가 live 탄환 수를 추종(스폰 시 증가, Lifetime 만료 파괴 시 감소).
- **리스크:** nullrhi에서 ISM AddInstance/GetInstanceCount 동작 불확실. 인스턴스 데이터가 렌더 프록시 없이도 갱신되면 카운트 검증 성립. 안 되면 헤드리스는 수집 수 `M`(순수 Mass 카운트)만 실증하고, 실제 ISM 반영은 PIE 육안으로 이관.

### 수동 (영상 산출)
- PIE 실행 → 원점 보스가 회전 나선 탄막 발사, 빨강 구체들이 바깥으로 이동.
- 탑다운 카메라(기존 topdown 셋업 재사용)로 탄막 육안 확인.
- 화면 녹화 → **탄막 데모 영상** = M1 마일스톤 완료 조건 충족.

## 스코프 밖 (YAGNI)

- 영속 InstanceIndex / Observer Processor 파괴 훅 → 재구성 전략이라 불요
- ISM `AddInstances`(배치) 최적화 → M1 수백발엔 루프 add로 충분, M3 프로파일링에서 필요 시
- HISM/Nanite/LOD, GPU 인스턴싱 튜닝 → M3/M6
- Niagara 렌더 → M6 (ISM→Niagara 교체)
- 커스텀 탄환 메시/발광 머티리얼 → 미요청, 엔진 구체로 충분
- 카메라/조명/포스트 셋업 → 기존 topdown 셋업 재사용, 신규 불요
- 영상 파일 녹화/편집 → 수동 물리 단계, 코드 밖
