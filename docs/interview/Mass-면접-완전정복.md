# Mass 면접 완전정복 — Project_RE

> **이 문서 목적**: 면접에서 "Mass 왜 썼어요?" 부터 "청크가 뭐예요?" 까지 **어떤 각도로 물어도 막힘없이** 답하기.
> 5살이 알아듣게 쉬운 비유부터 시작해서, 실제 이 프로젝트 코드와 실측 숫자까지 한 줄로 잇는다.
>
> **읽는 법**: 1부(개념) → 2부(우리 코드) → 3부(숫자) → 4부(예상 질문). 시간 없으면 **맨 뒤 §14 치트시트**만 외워도 절반은 간다.
>
> **원본 근거**: `Source/Project_RE/Mass/`, `docs/profiling/M3-mass-vs-actor.md`, `docs/profiling/M6-gpu-breakdown.md`, `docs/portfolio/portfolio-source.md`

---

# 목차

| 부 | 내용 |
|---|---|
| **0부** | 30초 프로젝트 요약 — 면접 첫 답변 |
| **1부** | Mass 이전에 알아야 할 것 (캐시·DOD·ECS) — 5살 버전 |
| **2부** | Mass 핵심 용어 8개 완전정복 |
| **3부** | 우리 프로젝트의 Mass 구조 (실제 코드) |
| **4부** | 프로세서 7종 하나씩 해부 |
| **5부** | 렌더 — ISM 완전정복 |
| **6부** | 판정 — 액터가 아닌 것을 어떻게 때리나 |
| **7부** | 네트워크 — 5만 발을 복제 없이 맞추기 |
| **8부** | 성능 측정 — 숫자와 그 숫자를 의심한 이야기 |
| **9부** | 클로즈드루프 스폰 컨트롤러 (제어이론) |
| **10부** | 패턴 수학 |
| **11부** | 이 프로젝트에서 밟은 함정 모음 |
| **12부** | 예상 질문 40개 + 답변 스크립트 |
| **13부** | 용어 사전 |
| **14부** | 치트시트 (암기용 한 장) |

---
---

# 0부. 30초 프로젝트 요약

## 면접 첫 답변 (외워라)

> "UE5 **Mass Entity**로 만든 탑뷰 탄막 보스전입니다.
> **60fps에서 투사체 45,000개**를 유지하고, **데디케이티드 서버에서 여러 명이 같은 탄막을 봅니다.**
> 특징은 아키텍처 선택을 전부 **실측으로 정당화**했다는 겁니다 —
> Actor 방식과 같은 조건에서 재보니 5,000발에서 **게임 스레드 4.03ms vs 16.99ms, 드로우콜 96 vs 3,358** 이었습니다."

이 한 문단에 면접관이 물고 늘어질 고리가 5개 박혀 있다:
1. Mass Entity → "그게 뭔데요?"
2. 45,000 → "어떻게 쟀어요?"
3. 데디 서버 → "5만 발을 복제해요?"
4. 실측 → "어떻게 측정했어요?"
5. 4.03 vs 16.99 → "왜 그 차이가 나죠?"

**이 문서는 그 5개 고리를 전부 대비한다.**

## 프로젝트 사실 정보

| 항목 | 값 |
|---|---|
| 엔진 | UE 5.8.1 **소스 빌드** |
| 장르 | 탑뷰(Top-down) 탄막 보스전, 협동 가능 |
| 규모 | C++ **71파일 / 8,481 LOC — 전부 실사용**, 이슈 100+, PR 71, 마일스톤 8개 |
| 정리 | M8에서 템플릿 잔재 제거: 소스 76파일 + 에셋 515개 = **591파일 삭제** (#141) |
| 핵심 기술 | Mass Entity, ISM 렌더, GAS(대쉬), 데디케이티드 서버, Niagara(폭발) |
| 검증 | PowerShell 3종(프로파일·통계·데디검증), 헤드리스 프로브, 스크린샷 CVar |
| 보스 패턴 | **15종** (전부 60fps 게이트 통과) |

---
---

# 1부. Mass 이전에 알아야 할 것 — 5살 버전

Mass를 설명하려면 먼저 **"왜 보통 방식으로는 안 되는가"** 를 알아야 한다.
면접에서 이 순서로 말하면 설득력이 훨씬 세다.

## 1-1. 컴퓨터는 왜 느려지나 — 라면 비유

CPU는 요리사, 메모리(RAM)는 **창고**다.

- CPU가 계산하는 속도: **엄청 빠름** (1초에 수십억 번)
- 메모리에서 데이터 가져오는 속도: **엄청 느림** (CPU 기준으로 수백 배 느림)

요리사가 라면 하나 끓이는 데 3초 걸리는데, 재료 가지러 창고 갔다 오는 데 300초 걸린다고 생각하면 된다.

**그래서 CPU 옆에 "캐시(Cache)"라는 작은 냉장고가 있다.**
자주 쓰는 재료를 미리 꺼내 놓는 곳이다. 캐시에 있으면 3초, 없으면 300초.

### 캐시 라인 — 냉장고는 "한 칸씩" 채운다

CPU가 메모리에서 데이터를 가져올 때 **딱 그것만** 가져오지 않는다.
**주변 64바이트를 통째로** 퍼온다. 이걸 **캐시 라인(cache line)** 이라 한다.

> 라면 하나 가지러 창고 갔는데, 옆에 있는 라면 5개를 같이 들고 온다.
> 다음에 그 5개 중 하나가 필요하면? **창고 안 가도 된다.** 이게 공짜 이득이다.

**결론: 다음에 쓸 데이터가 방금 쓴 데이터 바로 옆에 있으면 빠르다.**
이걸 **"메모리 지역성(locality)"** 이라고 한다.

## 1-2. 객체지향(OOP)이 왜 캐시에 나쁜가

보통 게임에서 총알을 이렇게 만든다:

```cpp
class ABullet : public AActor
{
    FVector Velocity;      // 24바이트 — 내가 쓸 것
    float   Lifetime;      //  4바이트 — 내가 쓸 것
    // ↓ 아래는 Actor가 원래 갖고 있는 것들 (수백 바이트)
    USceneComponent*  RootComponent;
    UStaticMeshComponent* Mesh;
    bool bReplicates, bHidden, bCanBeDamaged, ...;
    FName Tags[...];
    TArray<UActorComponent*> Components;
    // ... 실제로는 1,000바이트가 넘는다
};
```

총알 5,000개를 만들면 메모리에 이렇게 흩어진다:

```
[총알0: 1000바이트][총알1: 1000바이트][총알2: 1000바이트]...
    ↑ 이 중 내가 매 프레임 쓰는 건 Velocity(24) + Lifetime(4) = 28바이트뿐
```

**캐시 라인 64바이트를 퍼오면 그중 28바이트만 쓸모 있다. 나머지 36바이트는 쓰레기다.**
게다가 액터는 힙 여기저기 흩어져 있어서 다음 총알은 아예 다른 창고 칸에 있다.

> **5살 비유**: 라면 5,000개를 창고 5,000군데에 하나씩 따로 보관했다.
> 하나 가지러 갈 때마다 창고를 새로 찾아가야 한다. **왕복 5,000번.**

### 여기에 UE 액터의 추가 비용까지 붙는다

| 비용 | 설명 |
|---|---|
| **개별 Tick** | 액터 5,000개 = `Tick()` 가상 함수 호출 5,000번. 함수 포인터 따라가느라 분기 예측도 실패 |
| **드로우콜 5,000개** | 액터마다 메시 컴포넌트 1개 = GPU에 "이거 그려" 5,000번 명령 |
| **Spawn/Destroy 비용** | 액터 생성은 UObject 등록, 컴포넌트 등록, 월드 등록… 무겁다 |
| **컴포넌트 트랜스폼 전파** | 부모-자식 갱신 체인 |

**실측이 정확히 이걸 보여준다:**

```
탄환 수 →      100     1000     5000
Actor GT      2.84     5.22    16.99 ms   ← 개수에 비례해 선형으로 무너진다
```

## 1-3. 데이터 지향 설계(DOD) — 해법

**"객체 단위로 묶지 말고, 데이터 종류별로 묶어라."**

### AoS vs SoA (이거 물어보면 만점 질문이다)

**AoS = Array of Structs (구조체의 배열)** — 객체지향 방식

```
[위치0, 속도0, 색0, 체력0][위치1, 속도1, 색1, 체력1][위치2, 속도2, ...]
```

**SoA = Struct of Arrays (배열의 구조체)** — 데이터 지향 방식

```
위치: [위치0][위치1][위치2][위치3]...   ← 위치만 쭉
속도: [속도0][속도1][속도2][속도3]...   ← 속도만 쭉
색:   [색0][색1][색2][색3]...
```

**이동 계산은 "위치 += 속도 * dt" 뿐이다. 색이나 체력은 안 쓴다.**

- AoS면 캐시 라인에 안 쓰는 색·체력이 딸려 온다 → 낭비
- SoA면 캐시 라인이 **전부 위치**다 → 100% 활용

> **5살 비유**: 라면·김치·계란을 한 상자에 섞어 5,000상자 만드는 대신,
> **라면만 담은 상자, 김치만 담은 상자**로 나눈다.
> 라면 5,000개가 필요하면 라면 상자만 통째로 들고 오면 된다. **왕복 1번.**

### 덤: SIMD가 공짜로 붙는다

위치가 배열로 쭉 붙어 있으면 CPU가 **한 번에 4개, 8개씩 동시 계산**할 수 있다(SIMD 명령어).
흩어져 있으면 불가능하다. Mass가 빠른 이유 중 하나다.

## 1-4. ECS — DOD를 게임에 적용한 패턴

**ECS = Entity(개체) + Component(부품) + System(시스템)**

| 이름 | 비유 | 뜻 |
|---|---|---|
| **Entity** | **주민번호** | 그냥 번호. 데이터가 없다. "3번 총알" 같은 ID |
| **Component** | **속성 딱지** | 데이터 덩어리. "위치", "속도", "체력" |
| **System** | **일하는 사람** | "속도 딱지 가진 애들 전부 위치 옮겨" |

> **5살 버전**:
> - **Entity** = 학생 번호표. 번호만 있고 아무것도 안 적혀 있음.
> - **Component** = 그 번호에 붙는 스티커. "키 스티커", "몸무게 스티커"
> - **System** = 선생님. "키 스티커 있는 애들 전부 1cm 자라라!" 하고 한 번에 처리

**핵심: 상속(is-a)이 아니라 조합(has-a)이다.**

```
OOP:  ABullet extends AProjectile extends AActor   ← 계층 구조
ECS:  Entity#3 = [Transform] + [Velocity] + [BulletTag]   ← 부품 조립
```

## 1-5. 그래서 Mass가 뭔가

**Mass = 언리얼이 엔진에 내장한 ECS 프레임워크다.**

정확한 소개 문장:

> "Mass는 UE5에 내장된 **데이터 지향 ECS 프레임워크**입니다.
> 같은 구성의 엔티티를 **아키타입** 단위로 묶고, 그 안에서 **청크(연속 메모리 블록)** 로 관리해서
> 프로세서가 청크 단위로 순회합니다. 캐시 친화적이고 워커 스레드 분산이 쉽습니다."

### Mass가 원래 왜 만들어졌나 (물어보면 가산점)

에픽이 **『포트나이트』/『매트릭스 어웨이큰스』 데모의 군중·차량 시뮬**을 위해 만들었다.
"수만 개의 저비용 개체를 저비용으로 돌리기" 가 목적이다.
**탄막은 그 요구와 정확히 같은 모양**이다 — 개체가 많고, 개체당 로직은 단순하고, 개별성이 거의 없다.

**→ "왜 Mass를 썼나"의 첫 번째 답: 문제의 모양이 Mass가 겨냥한 모양과 같았다.**

---
---

# 2부. Mass 핵심 용어 8개 완전정복

이 8개만 정확히 알면 Mass 질문의 90%가 막힌다.

## ① Entity (엔티티) — 그냥 번호표

```cpp
FMassEntityHandle Entity = EM->CreateEntity(BulletArchetype);
// Entity 안에는 Index(int32)와 SerialNumber(int32) 두 개뿐이다. 데이터 없음.
```

**중요: 엔티티는 `UObject`가 아니다.** GC 대상도 아니고, 액터도 아니다. **그냥 숫자 두 개다.**

- `Index` — 배열 위치
- `SerialNumber` — 재사용 감지용. 3번 총알이 죽고 새 총알이 3번을 받으면 Serial이 달라진다 → 옛날 핸들로 접근하면 "이건 다른 애다"를 알 수 있다

> **5살 비유**: 사물함 번호. 사물함 안에 뭐가 들었는지는 번호가 모른다.

## ② Fragment (프래그먼트) — 데이터 딱지

ECS의 "Component"를 Mass는 **Fragment**라고 부른다.
(UE에는 이미 `UActorComponent`가 있어서 이름이 겹치기 때문이다. **이거 물어보면 정확히 답하면 좋다.**)

우리 프로젝트 실제 코드:

```cpp
/** 탄막 시뮬 상태. 위치는 FTransformFragment로 다룬다. */
USTRUCT()
struct FBulletSimFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector Velocity = FVector::ZeroVector;
    float   Lifetime = 0.f;
    float   ColorSel = 0.f;   // 색 선택 0/1 — 스폰 시 고정
};
```

특징:
- `USTRUCT` + `FMassFragment` 상속
- **POD에 가까운 순수 데이터**. 함수 없음, 가상함수 없음, 포인터 최소
- 작을수록 좋다 (캐시 라인에 많이 들어가니까)

## ③ Tag (태그) — 데이터 없는 딱지

```cpp
/** 탄환 식별 태그. Query 필터 전용(데이터 없음). */
USTRUCT()
struct FBulletTag : public FMassTag
{
    GENERATED_BODY()
};
```

**필드가 하나도 없다.** 크기 0이다. 오직 **"이 엔티티는 직선탄이다"** 를 표시하는 용도.

**왜 필요한가?**
직선탄과 곡사탄이 둘 다 `FTransformFragment`를 갖고 있다.
"트랜스폼 가진 애들" 로 쿼리하면 **둘 다 잡힌다.** 태그로 갈라야 한다.

우리 프로젝트에는 태그가 2개:
- `FBulletTag` — 직선탄 (Spiral/Fan/Rose/Cardioid/Bloom)
- `FArcBulletTag` — 곡사탄 (Artillery 계열, 포물선 그리고 착지)

> **5살 비유**: 스티커에 아무 글씨도 없지만, **빨간 스티커가 붙은 애들만 나와!** 라고 부를 수 있다.

## ④ Archetype (아키타입) — 같은 부품 조합의 이름표

**"똑같은 Fragment/Tag 조합을 가진 엔티티들의 그룹"** 이다.

우리 프로젝트 실제 코드 (`REBulletSpawnSubsystem.cpp`):

```cpp
void UREBulletSpawnSubsystem::EnsureArchetype(FMassEntityManager& EntityManager)
{
    if (BulletArchetype.IsValid()) { return; }   // 1회만 생성하고 캐싱

    BulletArchetype = EntityManager.CreateArchetype({
        FTransformFragment::StaticStruct(),      // 위치 (엔진 제공)
        FBulletSimFragment::StaticStruct(),      // 속도/수명/색
        FBulletRenderFragment::StaticStruct(),   // ISM 인스턴스 인덱스
        FBulletTag::StaticStruct() });           // "직선탄이다" 표시
}

void UREBulletSpawnSubsystem::EnsureArcArchetype(FMassEntityManager& EntityManager)
{
    if (ArcArchetype.IsValid()) { return; }

    ArcArchetype = EntityManager.CreateArchetype({
        FTransformFragment::StaticStruct(),
        FArcBulletFragment::StaticStruct(),      // 베지어 궤적 데이터
        FBulletRenderFragment::StaticStruct(),
        FArcBulletTag::StaticStruct() });        // "곡사탄이다" 표시
}
```

**우리 프로젝트에는 아키타입이 딱 2개다: 직선탄, 곡사탄.**

> **5살 비유**: 아키타입은 **"레고 설명서"** 다.
> "이 설명서로 만든 로봇들은 전부 팔2개+다리2개+머리1개" 라고 정해져 있다.
> 같은 설명서로 만든 로봇은 **같은 서랍**에 넣는다.

### 왜 아키타입으로 묶나?

**같은 부품 조합이면 메모리 레이아웃이 똑같기 때문**이다.
레이아웃이 같으면 배열로 쭉 붙여 놓을 수 있다 → SoA가 성립한다.

부품이 다르면(예: 어떤 총알은 '유도' 프래그먼트를 더 가짐) **다른 아키타입**이 되고 **다른 서랍**에 들어간다.

### ⚠️ 아키타입 이동은 비싸다 (이거 물어보면 고급 질문)

엔티티에 Fragment를 추가/제거하면 **아키타입이 바뀌므로 메모리를 통째로 옮겨야 한다**(archetype migration).
그래서 Mass에서는 **런타임에 부품을 자주 붙였다 뗐다 하면 느려진다.**

우리 프로젝트는 **스폰 시점에 조합을 확정하고 절대 안 바꾼다.** 그래서 이 비용이 0이다.

## ⑤ Chunk (청크) — 연속 메모리 블록

아키타입 안에서도 엔티티들은 **고정 크기 블록(청크)** 으로 나뉘어 저장된다.

```
BulletArchetype (직선탄 서랍)
├── Chunk 0: [엔티티 0~C-1]   ← 이 안에서 Transform C개가 연속, Sim C개가 연속
├── Chunk 1: [엔티티 C~2C-1]
├── Chunk 2: [엔티티 2C~3C-1]
└── ...
```

**청크 하나 안에서는 완벽한 SoA다.**

```
Chunk 0 내부:
  Transform: [T0][T1][T2]...[T(C-1)]    ← 연속!
  Sim:       [S0][S1][S2]...[S(C-1)]    ← 연속!
  Render:    [R0][R1][R2]...[R(C-1)]    ← 연속!
```

> **5살 비유**: 서랍(아키타입) 안에 **칸막이 상자(청크)** 가 있고,
> 상자 하나에 라면이 **줄 맞춰** 들어있다.
> 상자를 통째로 꺼내면 그 라면들이 한 번에 딸려 온다.

### ⚠️ 청크 크기를 정확히 말할 것 (틀리기 쉬운 곳)

**청크는 "캐시 라인에 맞춘 수 KB"가 아니다. UE 5.8 기본값은 128KB다.**

```cpp
// MassEntitySettings.h
uint32 ChunkMemorySize = 128 * 1024;   // 128KB (sanitize 범위 1KB ~ 512KB)
```

**한 청크에 엔티티가 몇 개 들어가는지는 고정 수가 아니라 프래그먼트 크기가 정한다.**
`청크당 엔티티 수 ≈ ChunkMemorySize / 엔티티당 바이트`.

우리 직선탄 아키타입으로 계산하면:

| 프래그먼트 | 대략 크기 |
|---|---:|
| `FTransformFragment` (FTransform, LWC double) | ~96 B |
| `FBulletSimFragment` (FVector + float 2) | ~32 B |
| `FBulletRenderFragment` (int32) | ~8 B |
| **합계** | **~136 B** |

→ **131,072 / 136 ≈ 청크당 900개 안팎.** 5,000발이면 청크가 **6개 정도**다.

> **면접 답변**: "청크 크기는 `UMassEntitySettings::ChunkMemorySize`이고 기본 128KB입니다.
> 엔티티 수는 **프래그먼트 크기의 함수**라서 아키타입마다 다릅니다 —
> 저희 직선탄은 엔티티당 약 136바이트라 청크당 900개 안팎, 5,000발이면 청크 6개 수준입니다.
> **L1 캐시에 통째로 얹으려는 크기가 아니라, 순차 접근으로 하드웨어 프리페처가 먹게 하는 크기**입니다."

### 청크가 곧 병렬화 단위다

청크끼리는 **완전히 독립**이다 → **다른 스레드가 다른 청크를 동시에 처리해도 안전**하다.
이게 Mass가 워커 스레드로 잘 퍼지는 이유다.

**우리 실측이 그걸 증명한다:**
```
REBullet/AllWorkers/BulletSim   0.04 ms   ← 이름에 "AllWorkers"가 박혀 있다
```
CSV 컬럼 이름 자체가 **"이 일은 워커 스레드에서 했다"** 는 증거다.

## ⑥ Processor (프로세서) — 일하는 사람

ECS의 "System"을 Mass는 **Processor**라고 부른다.

```cpp
UCLASS()
class UREBulletSimProcessor : public UMassProcessor
{
    GENERATED_BODY()
public:
    UREBulletSimProcessor();
protected:
    virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
    virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
private:
    FMassEntityQuery EntityQuery;
};
```

세 부분으로 이루어진다:

| 부분 | 하는 일 |
|---|---|
| **생성자** | 언제/어디서 돌지 결정 (넷모드, 게임스레드 강제, 실행 순서) |
| **ConfigureQueries** | "나는 이런 부품 가진 애들만 다룬다" 선언 |
| **Execute** | 실제 일. 매 프레임(정확히는 매 처리 페이즈) 호출된다 |

## ⑦ Query (쿼리) — "이런 애들 불러줘"

```cpp
void UREBulletSimProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
    EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadWrite);
    EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
}
```

읽는 법: **"Transform과 BulletSim을 읽고쓸 수 있게, 그리고 BulletTag가 반드시 있는 엔티티만."**

### `EMassFragmentAccess` — 접근 권한 (중요!)

| 값 | 뜻 | 왜 중요한가 |
|---|---|---|
| `ReadOnly` | 읽기만 | **여러 프로세서가 동시에 읽어도 안전** → 병렬 실행 가능 |
| `ReadWrite` | 읽고 쓰기 | 쓰는 사람은 하나여야 안전 → Mass가 스케줄링에 반영 |

**이건 단순한 힌트가 아니라 Mass의 스레드 안전성 근거다.**
Mass는 이 선언을 보고 "이 두 프로세서는 같은 프래그먼트를 안 건드리니 동시에 돌려도 된다"를 판단한다.

**우리 코드의 실제 사례 — Render는 ReadOnly다:**
```cpp
// REBulletRenderProcessor::ConfigureQueries
EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);  // 위치 읽기만
EntityQuery.AddRequirement<FBulletSimFragment>(EMassFragmentAccess::ReadOnly);  // Lifetime 읽기만
```
렌더는 위치를 **읽어서 ISM에 복사만** 한다. 시뮬레이션 값을 고치지 않는다.
**이 선언이 곧 "렌더는 게임 로직에 영향을 주지 않는다"는 계약이다.**

### `EMassFragmentPresence` — 존재 조건

| 값 | 뜻 |
|---|---|
| `All` | 반드시 있어야 함 |
| `Any` | 하나라도 있으면 |
| `None` | 없어야 함 (제외 필터) |
| `Optional` | 있으면 좋고 없어도 됨 |

우리는 `All`만 쓴다 — `FBulletTag`가 반드시 있는 것 = 직선탄만.

## ⑧ ForEachEntityChunk — 청크 단위 순회

```cpp
EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
{
    const float Dt  = Context.GetDeltaTimeSeconds();
    const int32 Num = Context.GetNumEntities();        // 이 청크 안의 엔티티 수

    // ★ 여기가 핵심 — 배열 통째로 받아온다
    const TArrayView<FTransformFragment> Transforms = Context.GetMutableFragmentView<FTransformFragment>();
    const TArrayView<FBulletSimFragment> Sims       = Context.GetMutableFragmentView<FBulletSimFragment>();

    for (int32 i = 0; i < Num; ++i)
    {
        FBulletSimFragment& Sim = Sims[i];
        Transforms[i].GetMutableTransform().AddToTranslation(Sim.Velocity * Dt);
        Sim.Lifetime -= Dt;
        if (Sim.Lifetime <= 0.f)
        {
            Context.Defer().DestroyEntity(Context.GetEntity(i));
        }
    }
});
```

### 왜 "엔티티 하나씩"이 아니라 "청크 하나씩"인가? (핵심 질문!)

**답: 함수 호출 비용을 없애고, 배열 접근으로 만들기 위해서다.**

| 방식 | 5,000발 처리 |
|---|---|
| 엔티티 단위 콜백 | 람다 호출 **5,000번** + 매번 데이터 위치 조회 |
| **청크 단위 콜백** | 람다 호출 **6번 정도**(청크당 900개 안팎), 안쪽은 **평범한 배열 for문** |

안쪽 for문은:
- 연속 메모리 순차 접근 → **하드웨어 프리페처가 다음 캐시 라인을 미리 가져온다**
- 분기 없음 → 파이프라인 안 깨짐
- 컴파일러가 **SIMD 자동 벡터화** 가능

**"이 for문은 그냥 C 배열 순회와 똑같다"** 가 핵심 문장이다.

---
---

# 3부. 우리 프로젝트의 Mass 구조

## 3-1. 전체 그림

```
                     [보스가 발사 결정]  (서버)
                              ↓
                     Multicast RPC (패턴, 원점, 각, 개수, 서버시각)
                              ↓  ← 서버와 모든 클라가 같은 함수를 실행
                   REBulletPatternGenerator   (순수 수학 함수)
                              ↓  TArray<FBulletSpawnParams>
                   UREBulletSpawnSubsystem    (스폰 단일 진입점)
                              ↓  EntityManager.CreateEntity(Archetype)
        ┌─────────────────────────────────────────────────┐
        │           Mass Entity 세계 (엔티티 수만 개)          │
        └─────────────────────────────────────────────────┘
                              ↓ 매 프레임, 처리 페이즈에서
   ┌──────────────┬──────────────┬─────────────────┐
   │ SimProcessor │ HitProcessor │ RenderProcessor │
   │ (워커 스레드)  │ (게임 스레드)  │  (게임 스레드)    │
   │ 위치·수명     │ 거리 판정      │  ISM 갱신        │
   │ 모든 넷모드    │ 모든 넷모드     │ 데디서버 skip    │
   └──────────────┴──────────────┴─────────────────┘
```

## 3-2. 빌드 배선 — 이거 물어보면 정확히 답해야 한다

### 모듈 (`Project_RE.Build.cs`)

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
    "AIModule", "UMG", "Slate", "SlateCore",
    "MassEntity",     // ★ FMassFragment, FMassEntityManager, UMassProcessor
    "MassCore",       // ★ FTransformFragment (Mass/EntityFragments.h)
    "GameplayAbilities", "Niagara", "GameplayTags", "GameplayTasks",
    "NavigationSystem", "DeveloperSettings"
});
```

**중요 사실 (M0 조사에서 확인):**

| 흔한 오해 | 실제 (UE 5.8) |
|---|---|
| "MassEntity는 플러그인이다" | ❌ **엔진 런타임 모듈**이다 (`Engine/Source/Runtime/MassEntity`). 플러그인 토글이 아니라 Build.cs 의존만 추가하면 된다 |
| "MassActorIntegration 플러그인 필요" | ❌ 그런 플러그인 없다. 모듈명은 `MassActors`(MassGameplay 안) |
| "StructUtils 필요" | ❌ `MassEntity`는 `MassCore`에만 의존 |
| "FTransformFragment는 MassGameplay에 있다" | ❌ **`MassCore`의 `Mass/EntityFragments.h`에 있다** |

### 플러그인 (`Project_RE.uproject`)

```json
{ "Name": "MassGameplay", "Enabled": true }
```

### ⚠️ 여기가 가장 중요한 함정 — 왜 MassGameplay 플러그인이 필요한가

**`MassEntity` 모듈만 있으면 엔티티를 만들 수는 있지만 프로세서가 한 번도 안 돈다.**

이유:
- 프로세서를 **틱시키는 것**은 `UMassSimulationSubsystem`이고, 이건 **MassGameplay 플러그인의 MassSimulation 모듈**에 있다.
- `UMassEntitySubsystem`(MassEntity 모듈)은 **데이터만 관리하고 페이즈를 돌리지 않는다.**

**즉 MassGameplay가 꺼져 있으면 `Execute()`가 0번 호출된다. 크래시도 로그도 없다. 조용히 죽어 있다.**

> **면접 답변**: "Mass 코어(엔티티/프래그먼트/쿼리)는 엔진 런타임 모듈이라 Build.cs 의존만으로 쓸 수 있지만,
> **프로세서를 실제로 틱시키는 페이즈 드라이버는 MassGameplay 플러그인**에 있습니다.
> 그래서 M0에서는 구조만 만들고 CDO의 ExecutionFlags 값으로 검증했고,
> MassGameplay는 실제 이동이 필요한 M1에서 켰습니다."

## 3-3. Fragment 전체 목록 (실제 코드)

```cpp
// ── 직선탄 ────────────────────────────────────────────
USTRUCT() struct FBulletSimFragment : public FMassFragment
{
    FVector Velocity;    // 등속 직선. 매 프레임 위치에 더한다
    float   Lifetime;    // 잔여 수명(초). 0 되면 소멸
    float   ColorSel;    // 0 또는 1 — 머티리얼이 두 색을 Lerp
};

USTRUCT() struct FBulletRenderFragment : public FMassFragment
{
    int32 InstanceIndex = INDEX_NONE;   // ISM 인스턴스 인덱스 슬롯
};

USTRUCT() struct FBulletTag : public FMassTag {};      // 데이터 없음

// ── 곡사탄 ────────────────────────────────────────────
USTRUCT() struct FArcBulletFragment : public FMassFragment
{
    FVector Start;        // 발사점 (보스)
    FVector Target;       // 착지점 (마커가 여기 뜬다)
    FVector Ctrl1, Ctrl2; // 3차 베지어 제어점 (월드 좌표)
    float   FlightTime;   // 총 비행시간
    float   Elapsed;      // 경과시간 → t = Elapsed/FlightTime
    float   Damage;       // 착지 스플래시 데미지
    float   Radius;       // 착지 스플래시 반경
};

USTRUCT() struct FArcBulletTag : public FMassTag {};   // 데이터 없음
```

### 설계 포인트 3개 (전부 물어볼 만함)

**① 위치는 왜 직접 안 들고 `FTransformFragment`를 쓰나?**
> 엔진 표준이라 다른 Mass 시스템과 호환된다. 그리고 위치는 두 아키타입이 공통으로 필요한데,
> 공통 프래그먼트를 쓰면 미래에 "모든 탄의 위치를 순회하는" 쿼리도 가능하다.

**② `ColorSel`은 왜 스폰 시 고정하나? (실제로 겪은 버그)**
> 처음엔 매 프레임 "탄 나이"에서 색을 파생시켰다. 그랬더니 **같은 탄의 색이 주기적으로 뒤집혀
> 화면 전체가 깜빡였다.** 색은 그 탄의 정체성이지 시간의 함수가 아니다 → 스폰 시 프래그먼트에 굳혔다.

**③ 곡사탄이 왜 Elapsed를 들고 있나? (네트워크와 연결)**
> 클라는 RPC를 늦게 받는다. `Elapsed`를 초기값으로 넣을 수 있어야
> "이 탄은 이미 0.08초 날아간 상태로 시작" 이 가능하다. 지연 보정의 핵심 필드다.

## 3-4. 스폰 — `UREBulletSpawnSubsystem`

`UWorldSubsystem`을 상속했다. **월드마다 자동으로 하나 생기고, 월드가 죽으면 같이 죽는다.**
싱글턴 직접 만들 필요가 없고 `GetWorld()->GetSubsystem<T>()`로 어디서나 접근한다.

```cpp
FMassEntityManager* UREBulletSpawnSubsystem::GetEntityManager() const
{
    UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr;
    return Mass ? &Mass->GetMutableEntityManager() : nullptr;
}

FMassEntityHandle UREBulletSpawnSubsystem::SpawnBullet(FVector Location, FVector Velocity,
                                                       float Lifetime, float ColorSel)
{
    FMassEntityManager* EM = GetEntityManager();
    if (!EM)
    {
        UE_LOG(LogREBullet, Warning, TEXT("[RE] SpawnBullet: EntityManager NULL"));
        return FMassEntityHandle();     // 무효 핸들
    }

    EnsureArchetype(*EM);                              // lazy 캐싱
    FMassEntityHandle Entity = EM->CreateEntity(BulletArchetype);

    EM->GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Location);
    FBulletSimFragment& Sim = EM->GetFragmentDataChecked<FBulletSimFragment>(Entity);
    Sim.Velocity = Velocity;
    Sim.Lifetime = Lifetime;
    Sim.ColorSel = ColorSel;

    return Entity;
}
```

### 왜 아키타입을 `Initialize()`가 아니라 **lazy**로 만드나? (좋은 질문거리)

> 서브시스템 초기화 순서가 보장되지 않는다. `UREBulletSpawnSubsystem::Initialize`에서
> `UMassEntitySubsystem`을 찾으면 아직 준비 전일 수 있다.
> **최초 스폰 시점에는 확실히 준비돼 있으므로** 그때 만들고 캐싱한다.
> `EnsureArchetype`는 `IsValid()` 체크 한 줄이라 이후 호출 비용이 0에 가깝다.

### 배치 스폰이 왜 그냥 for문인가? (솔직하게 답할 것)

```cpp
void UREBulletSpawnSubsystem::SpawnBulletBatch(TConstArrayView<FBulletSpawnParams> Params)
{
    for (const FBulletSpawnParams& P : Params)
    {
        SpawnBullet(P.Location, P.Velocity, P.Lifetime, P.ColorSel);
    }
}
```

> "**의도적으로 YAGNI로 남겼습니다.** Mass에 `BatchCreateEntities` API가 있지만,
> 프로파일링에서 스폰이 병목으로 나오지 않았습니다 — 5,000발에서 프로세서 총합이 0.88ms고
> 스폰은 발사 순간에만 일어납니다. **측정이 지목하지 않은 최적화는 안 합니다.**
> 그리고 이건 추측이 아닙니다 — **실제로 바꿔보고 측정해서 기각했습니다.**"

### 🏆 R-06 — 배치 스폰을 만들었다가 측정이 기각했다 (#141)

**이게 이 항목의 진짜 이야기다.** 하드닝 리팩터에서 두 안을 구현해 쟀다.

| 안 | 내용 |
|---|---|
| **A. 배치 생성** | 조회·아키타입을 볼리당 1회로 빼고 `FMassEntityManager::BatchCreateEntities(Archetype, N, Out)` 로 한 번에 생성 |
| **B. 조회만 호이스트** | 조회·아키타입만 볼리당 1회로 빼고 `CreateEntity` 는 발당 유지 |

**GT mean (ms) — 낮을수록 좋다. 720프레임, `profile.ps1` 고정 조건:**

| 구성 | 원본 | A 배치 | B 호이스트 |
|---|--:|--:|--:|
| 클로즈드루프 `re.Bullets.Count 5000` | **4.52 / 4.58** | 4.86 / 4.98 | 4.80 / 4.70 |
| 오픈루프 `BossPattern 0` (Spiral) | **4.48** | 4.69 | 4.56 |
| 오픈루프 `BossPattern 6` (LissajousStorm, 3회 중앙값) | **4.90** | 4.99 | — |

**세 구성 전부, 두 안 전부 원본보다 느리다.** 부호가 일관되므로 노이즈가 아니다.
열 드리프트도 배제했다 — B의 클로즈드루프 두 런은 빌드 직후 **가장 차가운** 상태였고,
원본은 앞서 네 런을 돈 **가장 더운** 상태였는데도 원본이 빨랐다.

**왜 느려졌나:**

> "**A는 배치 *생성*만 하고 프래그먼트 기입은 여전히 엔티티당 `GetFragmentDataChecked`입니다.**
> 그래서 배치 API의 오버헤드 — 생성 컨텍스트 `TSharedRef<FEntityCreationContext>` 힙 할당,
> 옵저버 통지 기구, 핸들 `TArray` — 만 얻고 **정작 이득인 연속 청크 기입은 못 얻었습니다.**
> 볼리가 48발(`BulletsPerShot`)이라 그 고정 비용이 절약분을 넘습니다.
> B는 절약분 자체가 작습니다 — 지우는 게 발당 `GetWorld` + `GetSubsystem` + `IsValid()` 셋인데
> 전부 인라인·분기예측이 잘 먹는 값싼 연산입니다."

**진짜 이유 — 스폰은 애초에 병목이 아니었다:**

```
오픈루프 발사율 = 48발 / 0.15초 = 320 스폰/s
매 프레임 도는 것 = 체공 중인 약 4,800발의 Sim / Render / Hit
→ 스폰은 프레임 작업량의 1% 미만. 몇 배 빠르게 해도 GT에 안 보인다.
```

**남긴 것: 없다. 소스는 원본 그대로다.**

> "`SpawnBullet`을 배치의 래퍼로 합치는 **구조 개선**(제어점 ⅔ 승격식이 한 곳에만 있게 되는 이득)도
> 같이 되돌렸습니다. **R-06의 범위는 성능이고, 게이트가 기각한 변경에 구조 개선을 얹어 남기면
> 그건 범위 이탈입니다.** 필요하면 별도 이슈로 냅니다."

**이 답변이 좋은 이유: "안 했다"도 "했다"도 아니라 "해보고 측정이 기각했고, 되돌리는 것도 결과다"** 다.
계획 문서(`refactor-plan.md` §7)가 **미리** "나빠지면 되돌린다"를 적어뒀다는 것도 같이 말할 것 —
사후 합리화가 아니라 사전에 정한 게이트다.

---
---

# 4부. 프로세서 7종 하나씩 해부

우리 프로젝트의 프로세서 전체 목록:

| 프로세서 | 넷모드 | 스레드 | 하는 일 |
|---|---|---|---|
| `UREBulletSimProcessor` | AllNetModes(7) | **워커** | 직선탄 이동 + 수명 |
| `UREBulletRenderProcessor` | Standalone\|Client(5) | 게임(강제) | 직선탄 ISM 갱신 |
| `UREBulletHitProcessor` | AllNetModes(7) | 게임(강제) | 직선탄 피격 판정 |
| `UREArcSimProcessor` | AllNetModes(7) | **워커** | 곡사탄 베지어 궤적 |
| `UREArcRenderProcessor` | Standalone\|Client(5) | 게임(강제) | 곡사탄 + 착지 마커 ISM |
| `UREArcHitProcessor` | Standalone\|Server(3) | 게임(강제) | 곡사탄 착지 스플래시 |
| `UREArcFxProcessor` | Standalone\|Client(5) | 게임(강제) | 곡사탄 착지 폭발 FX |

## 4-1. `ExecutionFlags` — 어느 넷모드에서 돌 것인가

```cpp
// MassProcessingTypes.h
enum class EProcessorExecutionFlags
{
    Standalone = 1 << 0,   // 1  — 싱글플레이 (PIE 기본)
    Server     = 1 << 1,   // 2  — 데디케이티드 서버
    Client     = 1 << 2,   // 4  — 클라이언트
    AllNetModes = Standalone | Server | Client   // 7
};
```

| 프로세서 | 값 | 데디서버 | 싱글 | 클라 |
|---|---:|---|---|---|
| Sim | **7** | 실행 | 실행 | 실행 |
| Render | **5** | **skip** | 실행 | 실행 |
| Hit(직선) | **7** | 실행 | 실행 | 실행 |
| ArcHit | **3** | 실행 | 실행 | **skip** |

### 이 설계가 왜 예쁜가 (면접에서 강조할 것)

> "**서버/클라 역할 분리를 `if (HasAuthority())` 분기가 아니라 프로세서 등록 단계에서 처리**했습니다.
> 데디서버는 렌더 프로세서를 **아예 실행하지 않습니다.**
> 코드에 조건문이 없으니 실수로 빠뜨릴 수도 없고, 서버에서 렌더 비용이 정확히 0입니다."

### ⚠️ 이슈 원문을 고친 이야기 (설계 판단력 어필)

원래 이슈에는 "Sim은 `Server | Client`" 라고 적혀 있었다. 그대로 하면:

**싱글플레이(Standalone) PIE에서 탄이 안 움직인다.** Standalone은 Server도 Client도 아니기 때문이다.

> "이슈 텍스트를 그대로 따르면 M1~M3 싱글 데모가 통째로 죽습니다.
> `AllNetModes`로 바꾸면서 **핵심 목표(데디서버가 렌더 skip)는 그대로 유지**했습니다.
> 검증은 CDO의 `GetExecutionFlags()` 로그로 `flags=7 / flags=5`를 찍어 확인했습니다."

## 4-2. `bRequiresGameThreadExecution` — 이건 반드시 물어본다

```cpp
UREBulletRenderProcessor::UREBulletRenderProcessor() : EntityQuery(*this)
{
    ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);

    // ISM(씬 컴포넌트) 변형은 게임 스레드 전용 — AddInstance가 물리 바디를 만들어
    // 워커 스레드에서 실행 시 BodyInstance 어서션 크래시. Execute를 GT에 고정.
    bRequiresGameThreadExecution = true;
}
```

**왜 필요한가:**

| 프로세서 | GT 강제 이유 |
|---|---|
| Render | `ISM->AddInstance()`가 내부에서 **물리 BodyInstance를 만든다** → 워커에서 부르면 어서션 크래시 |
| Hit | `Actor->TakeDamage()`는 **액터 호출** → 게임 스레드 전용 |
| ArcFx | **Niagara 스폰**은 게임 스레드 전용 |

**핵심 통찰 (외워라):**

> "**Mass 세계 안쪽은 스레드 자유롭지만, Mass 밖의 UE 객체(액터·씬컴포넌트·Niagara)를 만지는 순간
> 게임 스레드로 묶입니다.** 그래서 이 프로젝트에서 순수 워커 스레드로 도는 건 Sim 둘뿐이고,
> **그 제약이 곧 남은 병목의 위치를 정합니다** — 50,000발에서 GT 12.08 vs GPU 7.69로 게임 스레드 바운드입니다."

**이게 이 프로젝트에서 가장 좋은 아키텍처 설명 한 문장이다.** 반드시 외울 것.

## 4-3. `ExecutionOrder` — 실행 순서, 그리고 여기서 난 버그

```cpp
UREBulletHitProcessor::UREBulletHitProcessor() : EntityQuery(*this)
{
    // 이동 후 판정 — Sim이 위치를 전진시킨 뒤 같은 프레임에 히트 체크.
    ExecutionOrder.ExecuteAfter.Add(UREBulletSimProcessor::StaticClass()->GetFName());
}
```

**문자열이 아니라 클래스에서 이름을 얻는다.**
> "문자열 리터럴은 오타가 나도 조용히 무시됩니다. `StaticClass()->GetFName()`은 컴파일러가 잡습니다."

### 🔴 실제 버그: 곡사탄 폭발이 한 번도 안 떴다 (#98) — 최고의 스토리다

`UREArcFxProcessor`를 **`ExecuteBefore` 시뮬**로 등록했었다.
의도: "시뮬이 파괴하기 전에 위치를 읽자."

**그런데 `Elapsed`를 증가시키는 게 바로 그 시뮬이다.**

```
착지 프레임:  FX가 먼저 돔 → 아직 갱신 전 Elapsed(< FlightTime)를 봄 → 조건 거짓
다음 프레임:  엔티티가 이미 파괴됨 → 볼 대상 없음
                          ↓
             조건이 영원히 거짓. 크래시도 로그도 없다.
```

**어떻게 잡았나 (이 부분이 진짜 어필 포인트):**

| | 폭발 총합 | 직선탄 피격 | 곡사탄 초과분 |
|---|---|---|---|
| 수정 전 (착지점 12개) | 66 | 66 | **0** |
| 수정 전 (착지점 **200개**) | 73 | 73 | **0** |
| 수정 후 (착지점 12개) | 138 | 79 | **59** |

> "**착지점을 16배로 늘렸는데 폭발 수가 안 움직이는 게 결정타**였습니다.
> 폭발 총합이 직선탄 피격 건수와 **정확히 일치**하면 곡사탄 경로는 죽어 있다는 뜻입니다."

**고친 방법과 그게 안전한 이유:**

```cpp
// 시뮬 "뒤"에 돈다. 뒤에 돌아도 안전하다: 시뮬의 파괴는 Defer() 라 이 페이즈 끝에야
// 반영되므로 트랜스폼을 그대로 읽을 수 있다.
ExecutionOrder.ExecuteAfter.Add(UREArcSimProcessor::StaticClass()->GetFName());
```

이 문장이 다음 항목(Defer)으로 자연스럽게 이어진다.

## 4-4. `Defer()` — 지연 명령 버퍼 (중요 개념!)

```cpp
if (Sim.Lifetime <= 0.f)
{
    Context.Defer().DestroyEntity(Context.GetEntity(i));   // 즉시 파괴 아님!
}
```

### 왜 즉시 파괴하면 안 되나 — 5살 버전

> 지금 **줄 서 있는 사람들을 세고 있는데**, 중간에서 한 명이 빠지면?
> 뒷사람들이 앞으로 당겨진다. 그러면 **내가 세던 번호가 다 틀어진다.**

기술적으로:
- 순회 중 엔티티를 파괴하면 **청크의 배열이 재배치**된다 (뒤 엔티티를 끌어와 구멍을 메움)
- `TArrayView`로 잡아둔 포인터가 **무효화**된다 → 크래시 또는 데이터 손상
- 다른 스레드가 같은 청크를 보고 있으면 더 심각하다

### 그래서 Mass는 "커맨드 버퍼" 방식을 쓴다

```
Execute 중:  "3번 죽여줘", "17번 죽여줘"  →  명령을 리스트에 적어만 둠
                    ↓
처리 페이즈 끝:  Mass가 리스트를 한꺼번에 실행(flush)  →  안전하게 파괴
```

### 이 설계가 만든 **이득** (이게 진짜 포인트)

**"파괴가 지연된다"는 게 단점이 아니라 우리 설계의 전제다:**

```
같은 프레임 안에서:
  ArcSimProcessor:  Elapsed 갱신 → 착지 감지 → Defer().DestroyEntity
  ArcHitProcessor:  (아직 살아있는) 엔티티를 읽어 스플래시 데미지
  ArcFxProcessor:   (아직 살아있는) 엔티티의 트랜스폼을 읽어 폭발 스폰
        ↓
  페이즈 끝: 그제서야 실제 파괴
```

> "**착지한 탄을 세 프로세서가 같은 프레임에 정확히 한 번씩 관측할 수 있는 이유가 Defer입니다.**
> 그래서 FX 프로세서에 중복 스폰 가드(bFxSpawned 같은 플래그)를 두지 않았습니다 —
> 두 번 볼 경로가 구조적으로 없기 때문입니다."

**이건 "엔진 제약을 이해하고 그 위에 설계했다"는 좋은 증거다.**

## 4-5. 프로세서별 상세

### ⓐ `UREBulletSimProcessor` — 직선탄 이동 (워커 스레드!)

```cpp
CSV_DEFINE_CATEGORY(REBullet, true);   // CSV 프로파일러 카테고리 정의 (이 TU 한 곳에서만)

UREBulletSimProcessor::UREBulletSimProcessor() : EntityQuery(*this)
{
    ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;  // 7
    // bRequiresGameThreadExecution 을 안 건다 → 워커 스레드로 간다
}

void UREBulletSimProcessor::Execute(FMassEntityManager& EM, FMassExecutionContext& Context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletSim);     // Unreal Insights용
    CSV_SCOPED_TIMING_STAT(REBullet, BulletSim);     // CSV 프로파일러용

    EntityQuery.ForEachEntityChunk(Context, [](FMassExecutionContext& Context)
    {
        const float Dt = Context.GetDeltaTimeSeconds();
        const int32 Num = Context.GetNumEntities();
        const TArrayView<FTransformFragment> Transforms = Context.GetMutableFragmentView<FTransformFragment>();
        const TArrayView<FBulletSimFragment> Sims       = Context.GetMutableFragmentView<FBulletSimFragment>();

        for (int32 i = 0; i < Num; ++i)
        {
            FBulletSimFragment& Sim = Sims[i];
            Transforms[i].GetMutableTransform().AddToTranslation(Sim.Velocity * Dt);
            Sim.Lifetime -= Dt;
            if (Sim.Lifetime <= 0.f)
            {
                Context.Defer().DestroyEntity(Context.GetEntity(i));
            }
        }
    });
}
```

**포인트 정리:**
1. **등속 직선이다.** 중력도 유도도 없다 → 이게 나중에 네트워크 지연 보정을 정확하게 만든다 (`위치 += 속도 × 경과`가 오차 없이 맞는다)
2. **워커 스레드에서 돈다** — 5,000발에서 **0.04ms**. 사실상 공짜
3. `CSV_SCOPED_TIMING_STAT`이 CSV에 `REBullet/AllWorkers/BulletSim` 컬럼을 만든다 → **컬럼 이름 자체가 스레드 배치의 증거**

### ⓑ `UREArcSimProcessor` — 곡사탄 3차 베지어

```cpp
A.Elapsed += Dt;
const float t = (A.FlightTime > 0.f) ? FMath::Min(A.Elapsed / A.FlightTime, 1.f) : 1.f;

const float u = 1.f - t;
const FVector Pos = u*u*u       * A.Start
                  + 3.f*u*u*t   * A.Ctrl1
                  + 3.f*u*t*t   * A.Ctrl2
                  + t*t*t       * A.Target;

Transforms[i].GetMutableTransform().SetLocation(Pos);

if (A.Elapsed >= A.FlightTime)
{
    Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
}
```

**직선탄과 결정적 차이:**

| | 직선탄 | 곡사탄 |
|---|---|---|
| 위치 계산 | **누적** (`위치 += 속도×dt`) | **파라미터 함수** (`위치 = f(t)`) |
| 프레임 드랍 영향 | 없음(등속이라 동일) | 없음(t만 맞으면 정확히 같은 위치) |
| 착지 시점 | 수명 소진 | `Elapsed >= FlightTime` |

> **왜 파라미터 방식이 좋은가**: `Elapsed` 하나만 맞추면 **어느 시점에 스폰해도 궤적 위 정확한 위치**가 나온다.
> 네트워크 지연 보정(`Elapsed`를 미리 넣고 스폰)이 **오차 없이** 성립하는 이유다.

### ⓒ `UREBulletHitProcessor` — 6부에서 상세
### ⓓ `UREBulletRenderProcessor` — 5부에서 상세

---
---

# 5부. 렌더 — ISM 완전정복

## 5-1. 드로우콜이 뭔가 — 5살 버전

> CPU는 **주문받는 사람**, GPU는 **요리사**다.
> "이거 그려줘" 하고 주문 넣는 게 **드로우콜(Draw Call)** 이다.
>
> 주문 한 번 넣는 데 시간이 든다(상태 설정, 셰이더 바인딩, 커맨드 기록).
> **5,000명이 각자 주문하면 주문받는 사람이 죽는다.** 요리사는 놀고 있는데도.

**실측:**
```
Mass  5,000발 → 드로우콜    96
Actor 5,000발 → 드로우콜 3,358     ← 35배
```

Actor 쪽은 프레임이 **GT에 완전히 묶였다** (GT 16.99 ≈ Frame 17.08).
GPU는 오히려 비슷하다 (5.12 vs 5.28) — **같은 걸 그리니까 당연하다.**

> **면접 답변**: "드로우콜 차이가 곧 GPU 부담이 아니라 **CPU(게임 스레드/RHI) 부담**입니다.
> 실제로 두 경로의 GPU 시간은 거의 같습니다. 차이는 전부 CPU 쪽에 있습니다."

## 5-2. ISM (InstancedStaticMeshComponent)

**"같은 메시를 여러 개 그릴 때, 위치만 배열로 넘기고 드로우콜은 한 번"**

```
일반:  "구 그려(위치0)" "구 그려(위치1)" ... "구 그려(위치4999)"   ← 5,000번
ISM:   "구 5,000개 그려, 위치 배열은 여기"                        ← 1번(+α)
```

> **5살 비유**: 붕어빵 5,000개를 하나씩 주문하지 않고,
> **"이 틀로 5,000개, 놓을 자리는 이 종이에 적어놨어"** 하고 한 번에 넘긴다.

**드로우콜이 96인 이유**: 탄막 말고도 캐릭터·레벨·UI가 있어서다. **탄환 자체는 몇 개뿐**이다.
그리고 **탄 수가 5,000 → 50,000으로 10배가 돼도 드로우콜은 95~96으로 고정이다**(5,000에서 96, 50,000에서 95 — 탄 수와 무관하다). 이게 ISM의 힘이다.

## 5-3. 렌더 프로세서가 하는 일 — 3단계

```cpp
void UREBulletRenderProcessor::Execute(FMassEntityManager& EM, FMassExecutionContext& Context)
{
    UWorld* World = EM.GetWorld();
    UREBulletRenderSubsystem* RS = World ? World->GetSubsystem<UREBulletRenderSubsystem>() : nullptr;
    UInstancedStaticMeshComponent* ISM = RS ? RS->GetISM() : nullptr;
    if (!ISM) { return; }   // 데디서버 등 ISM 없으면 no-op

    constexpr float PopDuration = 0.1f;
    const float TotalLife = REBulletPattern::BulletLifetimeSec();

    // ── 1) live 탄환 트랜스폼 + 커스텀데이터 수집 ──────────────
    TArray<FTransform> Xf;
    TArray<float> Cd;
    EntityQuery.ForEachEntityChunk(Context, [&Xf, &Cd, TotalLife](FMassExecutionContext& Ctx)
    {
        const int32 Num = Ctx.GetNumEntities();
        const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
        const TConstArrayView<FBulletSimFragment> S = Ctx.GetFragmentView<FBulletSimFragment>();
        for (int32 i = 0; i < Num; ++i)
        {
            FTransform B = T[i].GetTransform();
            B.SetScale3D(FVector(REBulletGeometry::BulletScale));   // 0.5 — 단일 출처 (#141)
            Xf.Add(B);

            const float Age = TotalLife - S[i].Lifetime;      // 잔여 → 나이
            Cd.Add(FMath::Clamp(Age / PopDuration, 0.f, 1.f));  // [0] 스폰 팝
            Cd.Add(S[i].ColorSel);                              // [1] 색 선택
        }
    });

    // ── 2) 인스턴스 수를 M에 맞춤 (꼬리에서만 add/remove) ──────
    const int32 M = Xf.Num();
    int32 Count = ISM->GetInstanceCount();
    while (Count < M) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
    while (Count > M) { ISM->RemoveInstance(Count - 1);                               --Count; }

    // ── 3) 배열째 한 번에 넘긴다 ───────────────────────────
    if (M > 0)
    {
        ISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/false);
        ISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace=*/true,
            /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
    }
}
```

### 왜 청크를 가로질러 누적하나?

```cpp
TArray<FTransform> Xf;   // 람다 바깥에 선언 → 모든 청크의 결과가 여기 쌓인다
```

**i번째 인스턴스 = i번째 live 탄환.** 청크가 여러 개여도 전역 인덱스가 연속이 된다.
그래서 ISM 인스턴스 배열과 1:1로 대응하고, 배치 API에 그대로 넘길 수 있다.

### 왜 "꼬리에서만" add/remove 하나? (중요 디테일)

```cpp
while (Count > M) { ISM->RemoveInstance(Count - 1); --Count; }   // 항상 마지막 것만 제거
```

> 중간 인덱스를 제거하면 ISM이 **마지막 인스턴스를 그 자리로 swap**한다.
> 그러면 다른 인스턴스의 인덱스가 바뀌어서 "i번째 = i번째 탄환" 대응이 깨진다.
> **꼬리에서만 건드리면 나머지 인덱스는 절대 안 변한다.**

### ⚠️ `SetCustomData`를 먼저, 트랜스폼을 나중에 (미묘한 순서)

```cpp
ISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/false);   // dirty 안 찍음
ISM->BatchUpdateInstancesTransforms(..., /*bMarkRenderStateDirty=*/true, ...);  // 여기서 찍음
```

> "**dirty 마크(렌더 스테이트 갱신 요청)를 한 번만 걸기 위해서**입니다.
> 두 번 걸면 렌더 스테이트를 두 번 재생성해서 낭비입니다.
> 그리고 순서를 지켜야 합니다 — 위 `while` 루프가 인스턴스 수를 이미 M으로 맞췄기 때문에
> `SetCustomData`의 인덱스 범위가 유효합니다."

## 5-4. 🏆 #95 — 두 줄로 상한을 30,000 → 50,000으로 올린 이야기

**이건 면접 최고 소재 중 하나다. 순서대로 외워라.**

### (1) 문제 발견 — 상한이 30,000이었다

| 탄환 | Frame p99 | GT | GPU |
|---:|---:|---:|---:|
| 30,000 | **16.37** | 10.12 | 13.76 |
| 40,000 | 20.75 | 13.37 | 17.16 |

게이트는 **p99 ≤ 16.6ms**. 30,000이 상한이었다.

### (2) 원인 A — 탄환 그림자가 GPU를 지배했다

`r.ShadowQuality 0` A/B 테스트(40,000발): **GPU 17.16 → 6.13ms**

> "탄막에서 **탄환 그림자는 시각 기여가 사실상 없습니다.**
> 탄은 작고 빠르고 수천 개인데, 그림자 패스는 그 수천 개를 **전부 다시 그립니다**(그림자 뎁스 패스).
> 시각적으로 얻는 게 없는데 GPU 최대 소비처였습니다."

```cpp
ISM->SetCastShadow(false);                    // 직선탄/곡사탄/마커 ISM 3개 전부
ISM->bAffectDynamicIndirectLighting = false;  // Lumen 씬 제외
ISM->bAffectDistanceFieldLighting = false;
```

**Lumen을 왜 뺐나 (별개 이유, 물어보면 좋은 답):**
> "움직이는 인스턴스 수천 개가 **Lumen 서피스 캐시를 매 프레임 무효화**해서,
> **카메라가 탄막을 화면에 담을 때만 GPU가 수십 ms 튀었습니다.**
> 시점 의존적인 스파이크라 처음엔 원인을 못 찾았습니다.
> 탄환은 작고 빨라서 간접광 기여가 체감 0이고, **레벨·캐릭터 GI는 그대로 유지**했습니다."

### (3) 원인 B — 그림자를 끄자 병목이 게임 스레드로 넘어갔다

40,000발, 그림자 끈 상태의 GameThread 13.944ms 내역:

```
REBullet/GameThread/BulletRender        7.217 ms   (GT의 52%)
Exclusive/GameThread/EndOfFrameUpdates  3.462 ms
REBullet/AllWorkers/BulletSim           0.600 ms
REBullet/GameThread/BulletHit           0.227 ms
```

**ISM 렌더 전달이 GT의 77%. Mass 실제 시뮬은 40,000발에 0.83ms.**

원인: 렌더 프로세서가 `TArray<FTransform> Xf`를 이미 만들어놓고,
그걸 **인스턴스당 개별 `UpdateInstanceTransform` 호출**로 밀어넣고 있었다.

### (4) 고침 — 두 줄

```cpp
// 1. ISM 3개 그림자 끄기
ISM->SetCastShadow(false);

// 2. 개별 호출 루프 → 배치 API
ISM->BatchUpdateInstancesTransforms(0, Xf, true, true, true);
```

### (5) 결과 (40,000발 before → after)

| 항목 | before | after | 변화 |
|---|---:|---:|---:|
| `REBullet/GameThread/BulletRender` | 6.762 | **1.826** | **−73%** |
| `GPUTime` | 17.156 | **6.874** | **−60%** |
| `GameThreadTime` | 13.370 | **9.105** | −32% |
| `FrameTime` | 18.011 | **9.134** | **−49%** |

**60fps 상한 30,000 → 50,000.**

### (6) 🔍 렌더 결과가 안 망가졌음을 어떻게 확인했나 (이게 진짜 실력)

당시 저장소에 스크린샷 수단이 없었다. 그래서 **지표로 확정**했다:

| | before | after |
|---|---:|---:|
| `RHI/PrimitivesDrawn` | 81,087 | **80,997** |
| `GPUSceneInstanceCount` | 39,399 | 39,093 |
| `Exclusive/RenderThread/RenderBasePass` | 0.02 | 0.03 |
| `GPUTime` | 17.16 | **6.87** |

> "**메인 뷰에 그려지는 지오메트리가 그대로입니다.** GPU 시간만 떨어졌습니다 —
> **그림자 뎁스 패스만 사라지고 베이스 패스는 불변**이라는 지문입니다.
> `RenderShadows`가 0.19ms로 남은 것도 정합적입니다(캐릭터·보스·레벨 그림자는 유지)."

## 5-5. 퍼인스턴스 커스텀데이터 (Per-Instance Custom Data)

**ISM 인스턴스마다 float 몇 개를 실어 보낼 수 있고, 머티리얼이 그걸 읽는다.**

```cpp
ISM->SetNumCustomDataFloats(2);
// [0] = 스폰 팝 (태어난 뒤 0.1초 동안 밝기 상승)
// [1] = 색 선택 (0 또는 1 — 머티리얼이 Color/ColorB를 Lerp)
```

**인터리브 저장**: 인스턴스당 연속으로 붙는다.
```
Cd = [팝0, 색0, 팝1, 색1, 팝2, 색2, ...]
```

> **5살 비유**: 붕어빵 5,000개를 한 틀로 굽는데,
> 각 붕어빵에 **"이건 팥 0.3, 슈크림 1" 같은 쪽지**를 하나씩 끼워 보낸다.
> 굽는 기계(머티리얼)가 그 쪽지를 읽고 조금씩 다르게 굽는다.

**이게 왜 중요한가**: 인스턴싱은 "다 똑같이 그린다"가 원칙인데,
**커스텀데이터가 그 안에서 개별성을 만들어 주는 유일한 통로**다.
색을 다르게 하려고 ISM을 2개 만들면 드로우콜이 2배가 된다 — 그럴 필요가 없어진다.

## 5-6. 🔴 함정: 머티리얼이 조용히 폴백한다 (#97) — 킬러 에피소드

**증상**: 만든 머티리얼이 화면에 안 나온다. 그런데:
- 로그 에러 없음
- `ISM->GetMaterial(0)`은 **내가 설정한 머티리얼을 정상적으로 반환**
- 크래시 없음

**원인**: 머티리얼 에셋에 **`bUsedWithInstancedStaticMeshes` 용도 플래그**가 없었다.
ISM에 쓰려면 이 플래그가 켜져 있어야 하는데, **없으면 엔진이 조용히 기본 머티리얼로 대체**한다.

> "**코드로는 절대 못 잡습니다.** `GetMaterial()`이 정상 값을 돌려주니까요.
> 화면을 봐야만 보이는 종류의 버그입니다.
> 그래서 `re.Debug.ScreenshotFrame N` CVar를 **먼저 만들고** 그 위에서 반복했습니다."

**방어 코드도 넣었다:**
```cpp
const UMaterialInterface* Applied = ISM->GetMaterial(0);
UE_LOG(LogREBullet, Log, TEXT("[RE] RenderSubsystem: ISM ready (mesh=%d) material=%s"),
    ISM->GetStaticMesh() != nullptr, Applied ? *Applied->GetName() : TEXT("NULL"));
```

## 5-7. 🔴 색 교차 — 셰이딩 문제가 아니라 기하 문제였다 (#97)

**증상**: 4,800발을 띄웠는데 개별 구슬로 안 보이고 **선(튜브)** 으로 보인다.

**첫 진단(틀림)**: "셰이딩이 부족하다" → 프레넬 림(가장자리 강조)을 넣었다 → **오히려 더 뭉쳤다.**

**진짜 원인 — 산수 두 줄로 드러났다:**

```
탄 간격 = BulletSpeed(200) × BossFireInterval(0.15) = 30 uu
탄 지름 = Sphere(100uu)    × BulletScale(0.5)       = 50 uu

30 < 50  →  연속된 탄이 20uu(지름의 40%)씩 물리적으로 겹친다
```

**겹친 구체들의 합집합 실루엣은 "튜브 옆면"이다.**
그래서 프레넬 림이 **오히려 "테두리 있는 선"을 완성**시켰다.

### 시도했다가 되돌린 것

`BulletScale`을 0.5 → 0.2로 줄이면 틈 10uu가 생겨 겹침이 사라진다.
**실제로 상한도 50,000 → 60,000으로 올랐다.**
그런데 **육안 확인에서 "너무 작다"로 기각**됐다 — 포트폴리오 캡처에서 탄막이 압도적으로 보여야 하는데 탄이 작으면 그 인상이 죽는다.

> "**성능이 좋아졌는데 되돌렸습니다.** 이 프로젝트의 목적이 벤치마크 숫자가 아니라
> '탄막을 보여주는 것'이기 때문입니다. 그래서 상한 60,000은 무효고 50,000이 유효 수치입니다."

### 채택한 해법 — 체커보드 색 교차

**겹침을 없애는 대신, 인접 탄을 다른 색으로 칠해 가른다.**
불투명 렌더에서 앞 구체가 뒤 구체를 가릴 때, **색이 다르면 실루엣 경계가 보인다.**

**핵심: 겹침 축이 둘이다.**

| 축 | 겹침 원인 | 교차 기준 |
|---|---|---|
| **방사** (진행 방향) | 연속 발사 회차 | 발사 회차 패리티 |
| **원주** (링 내부) | 링 안 인접 탄 | 링 인덱스 |

한 축만 교차시키면 **다른 축이 통짜로 뭉친다.** 둘을 더해 홀짝을 낸다:

```cpp
const float ColorSel = float((P.ShotParity + i) & 1);
```

**그리고 `ShotParity`를 어디서 얻나 — 여기가 네트워크와 연결된다:**

```cpp
// 서버와 클라 양쪽이 같은 ServerTime 으로 이 함수를 실행하므로 별도 복제 없이 색이 일치한다.
SP.ShotParity = (Interval > 0.f) ? (FMath::FloorToInt(ServerTime / Interval) & 1) : 0;
```

> "카운터를 따로 두면 **멀티캐스트 유실 시 클라마다 색이 어긋납니다.**
> `ServerTime`에서 순수 유도하면 유실에 강합니다."

## 5-8. 머티리얼 — 언릿이 오히려 빨랐다 (반직관 사례)

`M_REBullet`: **언릿(Unlit) 발광 + 프레넬 림, 불투명**

**#95 대비 측정 (부록 B.3):**

| | #95 이후 | 머티리얼 적용 후 |
|---|---:|---:|
| 40,000 GPU | 6.87 | **6.59** |
| 50,000 GPU | 7.69 | **7.46** |
| 60,000 GPU | 8.62 | **8.44** |

> "**프레넬 림을 더했는데 GPU가 오히려 내려갔습니다.**
> 언릿(`MSM_Unlit`)이 라이팅 계산을 통째로 없앤 이득이 프레넬 비용보다 컸습니다.
> **'머티리얼을 화려하게 만들면 무조건 느려진다'는 통념과 반대**고,
> **셰이딩 모델 선택이 노드 개수보다 크게 작용한다**는 사례입니다."

### ⚠️ 불투명을 유지해야 하는 이유 (Niagara 논의와 연결)

> "**머티리얼을 불투명으로 유지해야 밀도 내성이 보존됩니다.**
> additive/translucent 스프라이트는 **early-Z가 없어서 오버드로우가 겹친 만큼 누적**됩니다.
> 탄막 장르에서 가장 위험한 선택이고, **현재 45,000 상한을 지탱하는 게 바로 불투명 렌더**입니다."

## 5-9. `UREBulletRenderSubsystem` — ISM 소유자

```cpp
void UREBulletRenderSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    // 데디서버는 렌더 안 함 — 여기서 조기 반환
    if (InWorld.GetNetMode() == NM_DedicatedServer || !InWorld.IsGameWorld()) { return; }

    REExplosionFx::Preload(&InWorld);        // 폭발 에셋 선로드 (#107)

    Holder = InWorld.SpawnActor<AActor>();   // ISM을 담을 빈 액터
    ISM = NewObject<UInstancedStaticMeshComponent>(Holder);
    Holder->SetRootComponent(ISM);
    ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // ★
    ISM->bAffectDynamicIndirectLighting = false;
    ISM->bAffectDistanceFieldLighting = false;
    ISM->SetCastShadow(false);
    ISM->RegisterComponent();
    // ... 메시/머티리얼/커스텀데이터 설정
}
```

**ISM 3개를 만든다:**
1. `ISM` — 직선탄 (빨강/파랑 교차)
2. `ArcISM` — 곡사탄 (주황 단색, 크기 0.7)
3. `MarkerISM` — 착지 마커 (얇은 반투명 링, Plane 메시)

### ⚠️ 왜 `NoCollision`인가 (실제 버그였다)

> "ISM 기본 콜리전이 `BlockAll`이라 **탄막이 플레이어 자동사격 라인트레이스를 막고 이동도 막았습니다.**
> 탄막은 시각 표현 전용이고 판정은 Mass 프로세서가 거리 비교로 하므로 콜리전이 필요 없습니다."

### ⚠️ 마커 메시를 Cylinder → Plane으로 바꾼 이야기 (재밌는 함정)

원래 Cylinder를 썼는데, Cylinder는 **높이가 100uu**라 Z 스케일 1.0이 "두께 100uu짜리 낮은 원기둥"이었다.
얇게 만들려고 Z 스케일을 낮췄더니:

> **실측(실RHI 스크린샷 이진탐색): XY 스케일(2.4) 대비 Z 스케일이 1.0 미만이면 인스턴스가 화면에서 완전히 사라졌다.**
> (0.02 / 0.15 / 0.4 전부 무렌더 확인, 등방 2.0은 정상 렌더 확인)
> → ISM 극단적 비등방 스케일에서의 컬링/바운즈 계산 이슈로 추정. **Z=1.0이 확인된 안전 하한선.**

**최종 해결**: `Plane` 메시로 교체. **두께 개념 자체가 없어서 원천 해결**이고, 0~1 UV라 머티리얼 링 마스크 만들기도 쉽다.

> "**증상을 우회하지 않고 문제를 없애는 쪽을 골랐습니다.** Z 스케일 하한을 지키는 규칙을 남기는 것보다,
> 두께가 없는 메시를 쓰면 그 규칙 자체가 필요 없어집니다."

---
---

# 6부. 판정 — 액터가 아닌 것을 어떻게 때리나

## 6-1. 근본 제약

**탄환은 액터가 아니다. 콜리전 컴포넌트도 없다(`NoCollision`).**
→ **물리 오버랩·스윕·라인트레이스를 쓸 수 없다.**

**해법: 직접 거리 계산.**

## 6-2. 실제 코드

```cpp
#include "REBulletGeometry.h"       // 히트 반경 단일 출처 (#141)
#include "Core/REStatsSettings.h"   // 탄 데미지 ini 이관 (#141)

void UREBulletHitProcessor::Execute(FMassEntityManager& EM, FMassExecutionContext& Context)
{
    // 1) 대상을 먼저 모은다 — 없으면 탄을 순회할 이유가 없다
    UWorld* World = EM.GetWorld();
    TArray<FREHitTarget> Targets;
    GatherHitTargets(World, Targets);

    // ini 조회는 진입부에서 1회. 엔티티 루프 안에서 GetDefault 를 부르지 않는다 —
    // 볼리당 수천 발이라 CDO 조회가 그대로 프레임 비용이 된다.
    const float BulletDamage = GetDefault<UREStatsSettings>()->BulletDamage;   // ini 10.0
    if (Targets.IsEmpty()) { return; }

    // 2) 탄 × 대상 이중 루프
    EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
    {
        const int32 Num = Ctx.GetNumEntities();
        const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();

        for (int32 i = 0; i < Num; ++i)
        {
            const FVector BulletLoc = Transforms[i].GetTransform().GetLocation();
            for (const FREHitTarget& T : Targets)
            {
                if (FVector::DistSquaredXY(BulletLoc, T.Location)
                    <= REBulletGeometry::HitRadius * REBulletGeometry::HitRadius)   // 유도값 60
                {
                    if (T.bInvulnerable)
                    {
                        // 대쉬 무적 — 데미지만 건너뛴다. 로그가 없으면 이 경로는 관측 불가다:
                        // 아래 Applied 로그가 안 찍혀 "탄이 그냥 사라진 것"과 구별되지 않는다.
                        UE_LOG(LogREBullet, Verbose, TEXT("[RE] BulletHit: dash-destroy (무적, 데미지 없음)"));
                    }
                    else if (T.Player->HasAuthority())
                    {
                        const float Applied = T.Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
                        UE_LOG(LogREBullet, Log, TEXT("[RE] BulletHit: Applied=%.0f netmode=%d"),
                            Applied, (int32)World->GetNetMode());
                    }
                    REExplosionFx::SpawnBulletExplosion(World, BulletLoc);
                    Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
                    break;   // 투사체 하나는 한 명만
                }
            }
        }
    });
}
```

## 6-3. 설계 결정 6개 — 전부 이유가 있다

### ① 왜 XY 평면 거리인가?

```cpp
FVector::DistSquaredXY(BulletLoc, T.Location)
```

> "**탑다운 게임**이라 Z는 게임플레이에 의미가 없습니다.
> 게다가 **탄환 Z(90)와 캡슐 중심 Z가 일치하지 않아서**, 3D 거리로 재면
> 눈으로는 명백히 맞았는데 판정이 안 되는 함정이 생깁니다. XY만 보면 그 함정이 없어집니다."

### ② 왜 `DistSquared`인가? (기초지만 반드시 답할 것)

> "**제곱근 계산을 피하기 위해서**입니다. `sqrt`는 비싸고, 비교만 할 거면 필요 없습니다.
> `dist < R` ⟺ `dist² < R²` 이니까요. 5,000발 × N명이면 이 차이가 누적됩니다."

### ③ 왜 대상을 먼저 모으고 조기 반환하나?

```cpp
GatherHitTargets(World, Targets);
if (Targets.IsEmpty()) { return; }
```

> "**전원 사망 상태에서 5만 발을 순회하는 것은 순수 낭비**입니다.
> 그리고 대상 수집은 한 번, 탄 순회는 5만 번이라 **순서가 중요**합니다.
> 탄 안쪽에서 매번 플레이어를 찾으면 5만 번 찾게 됩니다."

### ④ 왜 `break`인가?

```cpp
break;   // 투사체 하나는 한 명만 — 몸으로 막는 탱 플레이가 성립한다
```

> "**게임 디자인 결정**입니다. 탄 하나가 여러 명을 관통하지 않으므로,
> 앞사람이 몸으로 막아주는 플레이가 성립합니다.
> 반대로 **곡사탄 착지 스플래시는 `break`가 없습니다** — 범위 폭발이라 겹친 인원이 모두 맞아야 합니다."

### ⑤ 왜 `HitRadius`가 60인가? — 주석을 유도식으로 바꾼 이야기 (R-07, #141)

**60은 상수가 아니라 유도값이다.** `REBulletGeometry.h`가 단일 출처다:

```cpp
namespace REBulletGeometry
{
    /** /Engine/BasicShapes/Sphere 원본 반경(cm). 지름 100cm 구. */
    inline constexpr float EngineSphereRadius = 50.f;

    inline constexpr float BulletScale = 0.5f;

    /** 탄환 시각 반경(cm) — 유도값. 25. */
    inline constexpr float BulletVisualRadius = EngineSphereRadius * BulletScale;

    /**
     *  플레이어 쪽 판정 여유(cm).
     *  ARECharacterBase 는 캡슐 크기를 지정하지 않아 ACharacter 기본값을 그대로 쓴다 —
     *  반경 34 / 반높이 88 (Character.cpp: InitCapsuleSize(34.f, 88.f)).
     *  이 값은 그 34 를 35 로 **올림**한 것이다. 34 로 내리지 마라 — HitRadius 가 59 가 되어
     *  판정이 1uu 좁아지고, 그건 게임플레이 변경이다.
     */
    inline constexpr float PlayerCapsuleAllowance = 35.f;

    /** 탄환 히트 반경(cm) — 유도값. 60. BulletScale 을 바꾸면 여기가 자동으로 따라간다. */
    inline constexpr float HitRadius = BulletVisualRadius + PlayerCapsuleAllowance;
}
```

**⚠️ 35는 캡슐 반경이 아니다.** 캡슐 반경은 `ACharacter` 기본값 **34**고,
35는 거기에 올림을 준 **판정 여유**다. (리팩터 계획서가 "35가 캡슐 반경"이라고 잘못 적었고,
실행 중 실물과 어긋난 4건 중 하나로 정정해 남겼다.)

> "**보이는 것과 맞는 것이 일치해야 공정합니다.** 회피 게임에서 이 어긋남은 공정성 버그입니다.
> 전에는 `BulletScale`(렌더) / `HitRadius`(판정) / `ActorBulletScale`(Actor 비교군)이
> **세 파일에 흩어져 주석으로만** 묶여 있었습니다. **주석은 컴파일러가 검사하지 않습니다** —
> 실제로 `REBulletActor.cpp`가 가리키던 줄번호가 이미 밀려 있었습니다. 값이 아직 맞았을 뿐
> **참조는 썩어 있었습니다.** 그래서 R-07에서 주석이 아니라 **유도식**으로 묶었고,
> 게이트는 '히트 반경 수치 동일성'이었습니다 — 리팩터 전후 60 그대로."

**덤 — 네임스페이스를 쓴 이유(유니티 빌드 함정과 연결):**
> "익명 네임스페이스로 두면 유니티 빌드에서 동명 변수가 **C4459**로 충돌합니다.
> 이 프로젝트에 전례가 있어서 Baseline 쪽 상수를 `ActorBulletScale`로 비틀어 뒀었는데,
> **이름 하나로 합치니 원인부터 사라졌습니다.**"

### ⑥ 대쉬 무적 — 빼지 않고 "표시"한다 (M5 계약을 M6에서 뒤집은 이야기)

```cpp
struct FREHitTarget
{
    ARECharacterBase* Player;
    FVector           Location;
    bool              bInvulnerable = false;   // ← M6에서 추가
};
```

**M5 계약**: 대쉬 중이면 목록에서 **뺐다**. → 거리 비교 자체가 없으니 **탄환도 안 사라진다**(의도된 계약).

**M6 요구 변경**: "대쉬로 통과하면 데미지는 없되 **탄은 사라져야 한다**."

**해법**: 목록에서 빼지 말고 **플래그로 표시**하고, **데미지 면제 책임을 수집부에서 소비부로 옮긴다.**

- `REBulletHitProcessor`: `bInvulnerable`이면 **`TakeDamage`만 건너뛰고 소멸·폭발은 그대로**
- `REArcHitProcessor`: **같은 목록을 쓰므로 여기에도 가드가 없으면 곡사탄 스플래시가 대쉬 무적을 뚫는다**

> "**같은 데이터를 두 소비자가 쓰는데 한쪽만 고치면 조용히 뚫립니다.**
> 그래서 수집부를 공유 함수(`GatherHitTargets`)로 뽑아두고 소비부 양쪽을 같이 고쳤습니다."

## 6-4. `GatherHitTargets` — 왜 컨트롤러가 아니라 액터를 순회하나

```cpp
// 컨트롤러가 아니라 캐릭터를 순회한다 — 클라에서 GetPlayerControllerIterator 는
// 로컬 컨트롤러 하나만 돌려주므로, 그대로 두면 클라 판정(#98)이 동료 피격을 놓친다.
for (TActorIterator<ARECharacterBase> It(World); It; ++It)
{
    ARECharacterBase* Player = *It;
    if (!Player || !Player->IsAlive()) { continue; }   // 시체가 탄을 흡수하지 않게

    const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
    const bool bDashing = ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing);
    Out.Add(FREHitTarget{ Player, Player->GetActorLocation(), bDashing });
}
```

**핵심 함정 (물어보면 만점):**
> "**클라이언트에서 `GetPlayerControllerIterator`는 로컬 컨트롤러 하나만 돌려줍니다.**
> 서버에서는 전원이 나오지만요. 판정을 클라에도 열었기 때문에(#98),
> 컨트롤러로 순회하면 **클라에서 동료 피격을 놓칩니다.**
> `TActorIterator<ARECharacterBase>`는 복제된 다른 플레이어 폰까지 전부 잡습니다."

### 측정 전용 노브 — `re.Debug.FakeHitTargets` (이 발상이 좋다)

**문제**: 인원에 비례하는 유일한 항이 히트 판정 내부 루프(탄 × 인원)인데,
실제로 재려면 플레이어를 여럿 붙여야 하고 그러면 **한 머신에서 UE 프로세스가 여러 개 돌아 CPU 경합이 측정을 덮는다.**
(실측: 같은 탄막을 그리는 두 클라가 **12ms vs 25ms**로 갈렸다.)

**해법**: 프로세스 하나로 그 항만 격리한다 — 대상 목록을 N명으로 부풀린다.

```cpp
const int32 Fake = CVarFakeHitTargets.GetValueOnGameThread();
if (Fake > 1 && Out.Num() > 0)
{
    // 복제본을 반경 400uu 링으로 벌려 놓는다
    // 같은 자리에 겹치면 한 탄이 N번 맞아 폭발이 N배로 튄다 — 있지도 않은 비용을 재게 된다
    // 복제본은 bInvulnerable=true — 탄 소멸·폭발(=재려는 비용)은 발생시키되 데미지는 원본 한 명분만
}
```

> "**측정 노이즈의 원인을 없애는 대신, 재려는 항만 격리하는 노브를 만들었습니다.**
> 복제본을 겹쳐 두면 안 되는 이유까지 따져야 했습니다 — 실제 2인은 서로 떨어져 있어서
> 한 탄이 한 명에게만 맞으니까요."

## 6-5. 판정을 클라에도 여는 설계 (#98)

```cpp
UREBulletHitProcessor::UREBulletHitProcessor()
{
    // 판정을 클라에도 연다 — 클라가 자기 시뮬로 탄을 지우고 폭발을 띄운다.
    // 데미지는 HasAuthority 가드로 서버에만 남는다. 복제 RPC 는 쓰지 않는다:
    // 클라는 이미 ServerTime 으로 탄 위치를 자체 계산하므로 판정 근거가 있다.
    ExecutionFlags = (int32)EProcessorExecutionFlags::AllNetModes;
    bRequiresGameThreadExecution = true;   // TakeDamage 는 액터 호출
}
```

**왜 이렇게 하나:**

> "곡사탄이 수천 개일 때 **폭발 하나하나를 서버가 Multicast로 알리면 그 자체가 병목**입니다.
> 그래서 **소멸과 폭발은 클라가 로컬로 판정**하고, **데미지만 서버 권위**로 남겼습니다.
> 정당화 근거는 **클라가 이미 `ServerTime`으로 탄 위치를 자체 계산하고 있다**는 점입니다 —
> 판정할 데이터를 이미 갖고 있으니 굳이 서버가 알려줄 필요가 없습니다."

**책임 분리 표:**

| 무엇 | 누가 |
|---|---|
| 데미지 (게임 상태) | **서버만** (`HasAuthority()`) |
| 탄 소멸 (시각) | 서버 + 클라 각자 |
| 폭발 FX (시각) | 서버 + 클라 각자 |

**로그에 netmode를 같이 찍는 이유 (디테일):**
```cpp
UE_LOG(..., TEXT("[RE] BulletHit: Applied=%.0f netmode=%d"), Applied, (int32)World->GetNetMode());
```
> "**클라 프로세스는 접속 전에 로컬 월드를 잠깐 돌립니다.**
> 그래서 클라 로그에 데미지가 보인다고 곧 '클라가 데미지를 줬다'가 아닙니다.
> 판정하려면 netmode가 같이 찍혀야 합니다."

---
---

# 7부. 네트워크 — 5만 발을 복제 없이 맞추기

## 7-1. 근본 문제

> **Mass 엔티티는 복제되지 않는다. 그리고 복제해서도 안 된다 — 수만 발이다.**

액터라면 `bReplicates = true` 한 줄이면 되지만:
- 5만 발 × (위치 12바이트 + 속도 12바이트) = **초당 수 MB**. 대역폭이 즉사한다
- Mass 엔티티는 애초에 `UObject`가 아니라 UE 복제 시스템에 태울 수도 없다

## 7-2. 해법 — "총알이 아니라 총알 만드는 법을 보낸다"

```
서버: 보스가 발사 결정
      → Multicast RPC "패턴 P, 원점 O, 각도 A, 개수 N, 발사시각 T"
      → 서버도 로컬 스폰 → 서버가 판정 (권위)
클라: RPC 수신
      → 같은 파라미터로 같은 함수 실행 → 같은 탄이 생김
      → 클라가 그린다 (코스메틱) + 로컬 소멸 판정
```

**수천 발이 파라미터 5개로 압축된다.**

실제 시그니처:
```cpp
Multicast_FireDirect(CurrentPhasePattern, GetActorLocation(), AngleDeg, Count, GetServerNow());
//                   ↑패턴              ↑원점(NetQuantize)  ↑각   ↑발수  ↑서버시각
```

**`FVector_NetQuantize`** — 위치를 압축해서 보낸다(정밀도 1cm). 탄막 시각 표현에는 충분하고 대역폭이 준다.

## 7-3. 설계 논점 ① — 왜 시드만으로는 안 되나 (좋은 질문거리)

**초안**: "시드 S를 보내면 클라가 같은 난수로 재현한다."

**부족했다.** 시드가 결정하는 것은:
- 페이즈 로테이션 순서
- Artillery 착지 모양 선택

**하지만 이건 시드로 유도할 수 없다:**
- Fan의 중심각 = **그 순간 플레이어 위치**
- Artillery의 착지점 = **그 순간 플레이어 위치**

**플레이어 위치는 난수가 아니다.** 그리고 클라의 복제된 플레이어 위치는 서버와 미세하게 다르다.

> "**그래서 시드가 아니라 계산된 발사 파라미터 자체를 보냅니다.**
> 결정론적으로 재현 가능한 것과 아닌 것을 구분하는 게 이 설계의 핵심이었습니다."

## 7-4. 설계 논점 ② — 왜 발사 시각을 같이 보내나 (지연 보정)

**문제**: 클라는 RPC를 늦게 받는다(핑 만큼). 받은 순간 탄을 0에서 시작하면 **서버보다 뒤처진 탄막**이 된다.

**해법**: `ServerTime`을 같이 보내고, 클라는 "이 탄은 이미 0.08초 날아간 상태"로 스폰한다.

```cpp
const float Elapsed = GetElapsedSince(ServerTime);
if (Elapsed > 0.f)
{
    for (int32 i = Params.Num() - 1; i >= 0; --i)
    {
        if (Elapsed >= Params[i].Lifetime)
        {
            Params.RemoveAtSwap(i);   // 이미 수명이 다한 탄 — 스폰하지 않는다
            continue;
        }
        Params[i].Location += Params[i].Velocity * Elapsed;   // ★ 등속 직선이라 정확하다
        Params[i].Lifetime -= Elapsed;
    }
}
```

**이게 정확한 이유**: 직선탄이 **등속 직선**이라 `위치 += 속도 × 경과`가 **오차 없이 맞는다.**
곡사탄은 파라미터 곡선이라 `Elapsed`만 넣으면 역시 정확하다.

> "**시뮬레이션 모델을 단순하게 유지한 것이 네트워크 보정을 정확하게 만들었습니다.**
> 가속도나 유도가 붙었으면 적분해야 하고 오차가 쌓입니다."

**서버에서도 같은 코드가 돈다:**
> "서버는 발사 시각이 곧 현재라 `Elapsed ≈ 0`이 됩니다.
> **같은 코드가 무보정으로 동작하므로 서버/클라 분기가 필요 없습니다.**"

## 7-5. `GetElapsedSince` — 상·하한을 둘 다 막는 이유가 각각 다르다 (디테일 만점)

```cpp
float AREBossCharacter::GetElapsedSince(float ServerTime) const
{
    return FMath::Clamp(GetServerNow() - ServerTime, 0.f, 1.f);
}
```

| 경계 | 왜 필요한가 | 안 막으면 |
|---|---|---|
| **하한 0** | 접속 직후 GameState 복제 전이면 `GetServerWorldTimeSeconds()`가 **0을 반환** | `0 - 1000 = -1000` → 탄이 **미래에서 뒤로** 스폰 |
| **상한 1초** | 클라 시각 추정치가 아직 EMA 수렴 중이거나 서버 재시작 직후면 **큰 양수** | 볼리 전체가 "이미 수명 다함"으로 스킵 → **조용히 빈 화면** |

> "**상한이 없으면 화면이 그냥 비고, 에러도 안 납니다.**
> 이 프로젝트에서 조용한 실패가 측정을 두 번 망친 전례가 있어서(#50, #88),
> 경계 조건마다 '막지 않으면 어떻게 조용히 실패하는가'를 주석에 남깁니다."

## 7-6. 설계 논점 ③ — 유도할 수 있는 건 안 보낸다

**RPC 페이로드에 자리가 유한하다.** 그리고 **보낼수록 유실에 약해진다.**

```cpp
// ① 탄 색 교차 — ServerTime에서 유도
SP.ShotParity = (FMath::FloorToInt(ServerTime / Interval) & 1);

// ② 장미 로브 위상 — ServerTime에서 유도
RP.PhaseDeg = RoseSpinDegPerSec * ServerTime;

// ③ 심장형 링 회전 — ServerTime에서 유도 (페이로드의 각 자리는 '조준'이 쓰고 있다)
CdP.RingBaseDeg = CardioidRingSpinDegPerSec * ServerTime;

// ④ 소용돌이 링 반경 톱니 팽창 — ServerTime에서 유도
const float VPhase  = FMath::Frac(ServerTime / VortexExpandSec);
const float VRadius = FMath::Lerp(VortexMinRadius, VortexMaxRadius, VPhase);
```

> "**카운터를 따로 두면 멀티캐스트 유실 시 클라마다 값이 어긋납니다.**
> `ServerTime`은 GameState가 이미 동기화하고 있으니 **상수와 서버 시각만으로 순수 유도**되는 값은
> 페이로드에 실을 이유가 없습니다."

### ⚠️ 하지만 유도하면 안 되는 것도 있다 — MicroMissile 사례 (아주 좋은 반례)

```cpp
// 마이크로 미사일에서 ServerTime 파생을 안 쓰는 이유: 타이머가 ±5ms 흔들려
// floor(ServerTime/Interval) 이 인덱스를 건너뛰거나 반복한다(실측 간격 104/98/103/97/105ms).
// 그러면 동→서→북→남 순환이 깨져 연속한 두 발이 인접 방향에서 오기도 한다.
// 카운터는 지터와 무관하게 정확히 1씩 는다.
SweepIdx = PhaseVolleyIdx++;
```

> "**연속성이 필요한 값은 시간에서 유도하면 안 됩니다.**
> 타이머 지터가 ±5ms만 있어도 `floor(t/interval)`이 인덱스를 건너뜁니다.
> 실측으로 발사 간격이 104/98/103/97/105ms로 흔들리는 걸 확인하고,
> **볼리 카운터를 페이로드로 보내는 쪽으로 바꿨습니다.**"

**이 답변이 좋은 이유: "유도가 항상 옳다"가 아니라 "언제 유도가 깨지는지 안다"를 보여준다.**

## 7-7. 곡사탄 결정론 — `CallSeed`

```cpp
// Random 모양 전용 시드. 서버 스트림에서 1회 뽑아 넘긴다 —
// 클라는 PhaseRng가 없으므로 이 시드로 로컬 스트림을 만들어 같은 착지점을 얻는다.
const int32 CallSeed = (int32)PhaseRng.GetUnsignedInt();
...
FRandomStream CallRng(CallSeed);   // 양쪽이 같은 난수열을 본다
```

> "난수가 필요한 부분은 **시드를 보내서 양쪽이 같은 스트림을 만듭니다.**
> `FRandomStream`은 결정론적이라 **같은 시드 → 같은 수열**이 보장됩니다.
> 그래서 리팩터할 때도 **난수 소비 순서와 횟수를 바꾸면 서버/클라가 갈립니다** —
> 하드닝 리팩터(R-05)에서 이 함수를 분해할 때 그걸 최상위 제약으로 걸었고,
> **곡사 8종 착지점 좌표가 바이트 단위로 동일한지**를 게이트로 잡았습니다."

## 7-8. 데디서버에서 뭐가 안 도나 (정리)

| 시스템 | 데디서버 | 이유 |
|---|---|---|
| `UREBulletSimProcessor` | ✅ 돈다 | 판정 근거가 필요 |
| `UREBulletHitProcessor` | ✅ 돈다 | 데미지는 서버 권위 |
| `UREBulletRenderProcessor` | ❌ skip | ExecutionFlags=5 |
| `UREBulletRenderSubsystem` | ❌ 조기 반환 | ISM 자체를 안 만든다 |
| `UREArcFxProcessor` | ❌ skip | 화면이 없으니 폭발도 없다 |
| 보스 MID·애님 | ❌ 생략 | 코스메틱 |

> "**서버에는 ISM이 아예 없습니다.** 그래서 렌더 프로세서가 실수로 돌아도 `GetISM()`이 null이라 no-op입니다.
> 방어가 두 겹입니다 — ExecutionFlags로 안 돌게 하고, 돌아도 안전하게."

### 이게 Niagara 교체를 막는 근거가 된다 (중요한 아키텍처 논증)

> "**Niagara는 이동을 소유할 수 없습니다.**
> `REBulletSimProcessor`는 AllNetModes고, `REBulletHitProcessor`는 서버에서도 돌고,
> `REBulletRenderSubsystem`은 데디서버에서 조기 반환합니다.
> **서버에는 렌더가 없으므로 Niagara 파티클도 없고, 위치를 Niagara가 가지면 서버 권위 피격 판정이 성립하지 않습니다.**
> 따라서 Niagara는 **순수 렌더러로만** 가능하고, 얻는 건 GT 전달 비용 하나뿐인데
> 그 비용은 #95에서 이미 73% 줄었습니다."

---
---

# 8부. 성능 측정 — 숫자와, 그 숫자를 의심한 이야기

## 8-1. 먼저 알아야 할 3개의 시간 — 5살 버전

게임 한 프레임이 만들어지는 과정은 **공장 3단계 라인**이다:

```
[게임 스레드 GT]  →  [렌더 스레드 RT]  →  [GPU]
 "무엇을 그릴지 결정"    "그리는 명령서 작성"    "실제로 그림"
   게임 로직, 판정,        드로우콜 준비,        픽셀 칠하기
   Mass 프로세서          상태 설정
```

**중요: 세 단계는 파이프라인이라 병렬로 돈다.**
그래서 **프레임 시간 = 셋 중 가장 느린 것**이다. 이걸 "**~에 바운드됐다**"고 한다.

| 상황 | 판정 |
|---|---|
| GT 16.99, Frame 17.08 | **게임 스레드 바운드** — CPU 로직이 병목 |
| GT 4.03, GPU 5.28, Frame 6.18 | 대체로 균형, 약간 GPU 쪽 |

> **5살 비유**: 김밥 만드는 데 밥 짓기 5분, 재료 썰기 3분, 말기 2분.
> 세 사람이 나눠서 동시에 하면 **가장 오래 걸리는 5분**이 라인 속도다.
> 재료 썰기를 1분으로 줄여봐야 라인은 여전히 5분이다.

**여기서 나오는 결론이 이 프로젝트 전체를 관통한다:**
> "**병목이 아닌 걸 최적화하면 아무 일도 안 일어납니다.**
> 그래서 최적화 전에 먼저 어디가 병목인지 재야 합니다."

## 8-2. p99가 뭐고 왜 평균보다 중요한가

**p99 = 프레임 시간을 정렬했을 때 위에서 1%에 해당하는 값.**

> 100프레임 중 **가장 느린 1프레임**이 어느 정도인가.

**왜 중요한가:**
> "평균 10ms여도 **가끔 30ms짜리 프레임이 섞이면 사람은 '끊긴다'고 느낍니다.**
> 부드러움은 평균이 아니라 **최악에서 결정**됩니다.
> 그래서 이 프로젝트의 게이트는 **p99 ≤ 16.6ms**입니다."

**16.6ms의 정체**: `1초 / 60프레임 = 16.67ms`. **60fps 한 프레임 예산.**

**실제로 이 구분이 판정을 갈랐다:**
```
Actor 5,000발:  평균 17.08 ms,  p99 23.24 ms
                       ↑ 평균도 넘지만,     ↑ 분포 꼬리에서 더 크게 실패
```

## 8-3. 정본 성능 표 — 이걸 외워라

**측정 조건**: UE 5.8.1 소스 빌드, `-game` Development, 실 RHI `-windowed 1280x720`,
**720프레임 캡처, 탄환 채움 완료 시점부터**(`-csvStartOnEvent=REBulletsFilled`)

### 5,000발 (스트레스)

| | Frame mean | **Frame p99** | GT | RT | GPU | Instances | **DrawCalls** |
|---|---:|---:|---:|---:|---:|---:|---:|
| **Mass** | 6.18 | **8.80** | **4.03** | 6.17 | 5.28 | 4,887 | **96** |
| **Actor** | 17.08 | **23.24** | **16.99** | 15.42 | 5.12 | 5,102 | **3,358** |

### 1,600발 (측정 당시 실제 게임플레이 부하)

| | Frame mean | Frame p99 | GT | GPU | DrawCalls |
|---|---:|---:|---:|---:|---:|
| **Mass** | 5.13 | 6.93 | 3.24 | 4.26 | 97 |
| **Actor** | 8.02 | 12.00 | 7.54 | 4.37 | 1,153 |

### ★ 스케일 곡선 — 이게 핵심 그림이다 (반드시 외울 것)

```
탄환 수 →      100     1000     5000
Mass GT       2.71     3.17     4.03  ms   ← 거의 평평 (+1.3ms)
Actor GT      2.84     5.22    16.99  ms   ← 선형 붕괴 (+14.2ms)
```

**한 문장 정리:**
> "**탄이 50배 늘어도 Mass는 게임 스레드가 1.3ms 오르고, Actor는 14.2ms 오릅니다.**
> Actor 5,000은 프레임이 GT에 완전히 묶여서(GT 16.99 ≈ Frame 17.08) **60fps를 자력으로 못 넘습니다.**
> 반면 Mass 5,000은 GT가 4.03ms로 비어 있습니다."

### ★ 프로세서 3분해 — 스레드 배치가 CSV 컬럼 이름에 박혀 있다

| CSV 컬럼 | 스레드 | Mass 5,000 mean | p99 |
|---|---|---:|---:|
| `REBullet/AllWorkers/BulletSim` | **워커** | **0.04** | 0.08 |
| `REBullet/GameThread/BulletRender` | 게임 | 0.74 | 1.33 |
| `REBullet/GameThread/BulletHit` | 게임 | 0.10 | 0.23 |

**총합 0.88ms.** 5,000발 부하에서 **Mass 탄막 시뮬 자체는 프레임의 약 2%다.**

> "**컬럼 이름 자체가 증거입니다.** `AllWorkers`가 붙었다는 건 그 일이 워커 스레드에서 돌았다는 뜻이고,
> **이동(Sim)이 게임 스레드에서 완전히 빠졌다**는 게 GT가 비어 있는 이유입니다."

### 60fps 투사체 상한 이력

| 시점 | 상한 | 이유 |
|---|---:|---|
| 초기 | 30,000 | 탄환 그림자 + 인스턴스당 개별 트랜스폼 갱신 |
| #95 이후 | **50,000** | 그림자 제거 + 배치 API (두 줄 변경) |
| #98 이후 | **45,000** | **Niagara 플러그인 고정비 +2.5ms GT** (되돌릴 수 있는 교환) |

> "**상한이 내려간 이력이 있다는 게 오히려 자산이라고 생각합니다.**
> 폭발 이펙트를 얻는 대신 헤드라인 숫자가 50,000 → 45,000이 됐고,
> **그게 어떤 교환인지 알고 받아들였습니다.** 플러그인을 끄면 복귀합니다."

## 8-4. 계측 코드 — 어떻게 이 숫자가 나오나

```cpp
// REBulletSimProcessor.cpp — 카테고리는 이 TU 한 곳에서만 정의
CSV_DEFINE_CATEGORY(REBullet, true);

// REBulletRenderProcessor.cpp / REBulletHitProcessor.cpp — EXTERN으로 공유
CSV_DECLARE_CATEGORY_EXTERN(REBullet);

void Execute(...)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(RE_BulletSim);     // Unreal Insights 타임라인
    CSV_SCOPED_TIMING_STAT(REBullet, BulletSim);     // CSV 컬럼 (frames.csv)
    ...
}
```

**두 가지를 동시에 건다:**

| 매크로 | 산출물 | 용도 |
|---|---|---|
| `TRACE_CPUPROFILER_EVENT_SCOPE` | `trace.utrace` | Unreal Insights에서 **프레임별 타임라인 시각화** |
| `CSV_SCOPED_TIMING_STAT` | `frames.csv` | **프레임별 ms 숫자** → 평균/p99 자동 계산 |

**검증 자동화 스크립트 3종:**

| 스크립트 | 하는 일 |
|---|---|
| `profile.ps1` | 실 RHI `-windowed 1280x720`, 워밍업 컷, CSV 캡처, `trace.utrace` |
| `profile-stats.ps1` | `frames.csv` → mean/p99 표 (**사람 눈 개입 없음**) |
| `dedi-verify.ps1` | 서버+클라 2프로세스 자동 기동·판정, N인 판정, **빌드 신선도 게이트** |

## 8-5. 🏆 측정이 거짓말한 다섯 번 — 최고 차별점

> "성능을 쟀다"는 흔하다. **"잰 값을 의심해서 도구부터 고쳤다"** 는 드물다.

> **표가 6행인데 왜 "다섯 번"인가**: #88이 두 가지를 동시에 고쳐서 두 줄이다(캡처 시작 시점 + 리밋사이클).
> **이슈 기준 5건, 결함 기준 6건.** 물어보면 이렇게 답하면 된다.

| # | 이슈 | 거짓말한 방법 | 어떻게 잡았나 |
|---|---|---|---|
| 1 | **#88** | 캡처가 **엔진 부팅 시점**에 시작 — 17.9초 창 중 **13.3초가 레벨 로드·셰이더 컴파일** | `-csvStartOnEvent=REBulletsFilled`로 정상상태 진입 후부터만 캡처 |
| 2 | **#88** | 클로즈드루프 스폰이 **리밋사이클**(388~1,802 진동) — 프로파일 전체가 무의미 | 루프게인을 N²로 무차원화 (9부) |
| 3 | **#50** | 진단 이슈의 근거 "GPU 27~38ms"가 **어떤 조건에서도 재현 안 됨** | 재측정 후 **코드 변경 없이 닫음**. bloom은 GPU에 0.2% 기여 |
| 4 | **#103** | `dedi-verify`가 **하루 반 낡은 스테이징 서버**를 검증하고 16항목 전부 PASS | **빌드 신선도 게이트** 추가 |
| 5 | **#113** | 버그 리포트가 **오진** — 설계대로 동작하고 있었다 | 재현 시도에서 판명, 정정을 적고 닫음 |
| 6 | **#139** | `re.Profiling.KeepFiring`이 앞단에서 Spiral 고정 + early-return → **BossPattern을 뭘로 줘도 Spiral이 측정됨** | 패턴 고정 시 우회를 안 타게 수정 |

### 🥇 최고의 이야기 — M3 리포트에 스스로 무효 배너를 붙였다

**상황**: M3에서 잰 Mass 5,000발 GPU가 **38.61ms**였다. "GPU 병목"이라는 이슈(#50)의 근거였다.
Niagara 전면 교체라는 **수 주짜리 작업**이 여기서 시작될 참이었다.

**의심한 계기 (이게 핵심):**
> "**분해가 합과 안 맞았습니다.** Mass 5,000에서 프로세서 총합이 0.88ms인데 프레임이 39.5ms였습니다.
> 둘 중 하나가 틀린 겁니다."

**재측정 결과:**

| | M3 (2026-07-16) | 재측정 (2026-08-14) | 배율 |
|---|---:|---:|---:|
| Frame | 39.54 | **6.18** | 6.4배 |
| GPU | 38.61 | **5.28** | 7.3배 |
| **GT** | 4.45 | **4.03** | **1.1배** ← ★ |
| DrawCalls | 386 | 96 | 4.0배 |

**★ GT만 안 변한 게 결정적 단서였다.**

가설 두 개를 세우고 하나를 실험으로 죽였다:

**가설 A — 밸런스 변경(수명 3→15)이 탄을 흩어 오버드로우가 사라졌다.**
검증: `BulletLifetime`만 3.0으로 되돌려 재측정 → **GPU 5.95ms.** 38.61 근처에도 못 갔다. **반증.**

**가설 B — #88이 고친 하네스 결함(부팅 구간을 쟀다).**
> "**로딩·셰이더 컴파일 구간에서는 GPU와 렌더스레드가 PSO 컴파일로 얻어맞는 동안
> 게임 스레드는 대기하며 놉니다.** 그래서 오염된 창에서도 GT 평균은 거의 그대로고 GPU/RT만 부풉니다.
> 관측된 패턴이 정확히 그것이었습니다."

**조치:**
- M3 리포트 상단에 **"⚠️ 무효 갱신: 아래 표의 절대 수치는 쓰지 마라"** 배너를 직접 달았다
- **결론은 유지하고 숫자만 갈아 끼웠다** — 재측정에서 "Mass가 낫다"는 방향은 **오히려 강해졌다**
- #50을 **코드 변경 없이** 닫았다

**#50 소거 표 (실측):**

| 끈 것 | GPU | base 대비 | 비중 |
|---|---:|---:|---:|
| (base) | 5.28 | — | — |
| **bloom** (`r.BloomQuality 0`) | 5.27 | **−0.01** | **0.2%** |
| 그림자 (`r.ShadowQuality 0`) | 3.24 | −2.04 | 38.6% |
| 해상도 1/2 | 3.76 | −1.52 | 28.8% |

> "**#50이 지목한 bloom은 범인이 아니었습니다.**
> 감소분이 **런간 노이즈 바닥(±3%) 아래**라, 정확히는 '0.2% 기여한다'가 아니라
> **'이 측정의 분해능으로는 0과 구별되지 않는다'**입니다."

### 🥈 노브가 안 먹은 것과 원인이 아닌 것을 구분했다 (측정 신뢰성의 정수)

**질문**: "bloom을 껐는데 GPU가 안 줄었다"는 두 가지로 읽힌다.
1. bloom이 원인이 아니다
2. **`r.BloomQuality 0`이 애초에 안 먹었다**

**어떻게 구분했나 — 렌더스레드 패스별 컬럼을 교차 검증:**

| 구성(5,000) | BasePass | Shadows | **PostFX** | UpdateGPUScene | UpdatePrimitiveInstances |
|---|---:|---:|---:|---:|---:|
| Mass base | 0.02 | 0.17 | **0.31** | 0.07 | **0.00** |
| Mass nobloom | 0.02 | 0.15 | **0.22** | 0.05 | 0.00 |
| Actor base | 0.02 | 0.16 | 0.28 | **0.39** | 0.00 |

> "**bloom 소거가 `PostFX`를 0.31 → 0.22로 줄였습니다. 노브는 확실히 작동했습니다.**
> 그런데 GPU는 0.01ms만 줄었습니다 — **'원인이 아니어서'지 '노브가 안 먹어서'가 아님을 확정**한 겁니다.
> 그리고 6개 소거 런 전부 **로그에서 CVar가 런타임에 실제로 적용됐는지** 확인했습니다."

**같은 표에서 나온 부수 결론 두 개:**
- `UpdatePrimitiveInstances`가 **전 구성에서 0.00** → "ISM 인스턴스 버퍼 갱신이 병목"이라는 가설은 성립하지 않는다
- `UpdateGPUScene`이 **Actor(0.39)에서 Mass(0.07)의 5.6배** → **인스턴스 처리는 ISM 쪽이 오히려 5배 이상 싸다**

### 🥉 캡처 창을 프레임 수로 잡아서 오진한 이야기 (#106/#107)

> "`-csvCaptureFrames=600`은 **프레임 수이지 시간이 아닙니다.**
> 헤드리스 `-nullrhi`는 **1,800fps**로 돌아서 600프레임이 **0.33초**뿐이었고,
> `t=2.0s`에 일어나는 대쉬가 **캡처 밖**이었습니다.
> 그걸 보고 '히치 없음'이라고 단정했습니다.
> **창을 맞추자 이상 런에만 290ms 프레임이 정확히 하나 있었습니다.**"

**교훈**: 프레임 수로 창을 잡을 때는 **fps를 곱해 실제 시간을 확인해라.**

### 그 290ms의 정체 — 게임 중 블로킹 로드 (#107)

```
LogStreaming: FlushAsyncLoading(191): 1 QueuedPackages
  AddPackage: /Game/FX/NS_REBulletExplosion
    -> /Niagara/DefaultAssets/DefaultSpriteMaterial
    -> /Niagara/DynamicInputs/...  (수십 개)
```

첫 폭발에서 `LoadObject`가 **동기 로드**를 걸었다. Niagara 시스템 하나가 의존 패키지 수십 개를 끈다.
나머지 프레임이 0.5~1.1ms이므로 **300배**.

**해결**: `REBulletRenderSubsystem::OnWorldBeginPlay`에서 **선로드**로 옮겼다.

| | 게임 시작 후 `FlushAsyncLoading` | 게임 중 최장 프레임 |
|---|---|---|
| 전 | 1회 | **290ms** |
| 후 | **0회** | 105~122ms (부팅 직후 첫 프레임) |

**이 히치가 만든 2차 피해 (여기가 진짜 무섭다):**
> "**대쉬 구간에 히치가 겹치면 RootMotion이 지속시간을 넘겨 적용돼 이동거리가 2배가 됐습니다**(#106).
> `ApplyRootMotionConstantForce`는 힘을 매 이동 틱에 적용하는데, CMC가 **긴 프레임 하나를 통째로 시뮬레이션**하니까
> 소스의 남은 지속시간을 넘겨서도 그 프레임 전체에 힘이 적용됩니다.
> 실측: 정상 0.200s / 678uu → 히치 겹침 0.376s / **1,274uu.**
> **히치 하나가 전혀 무관해 보이는 시스템을 망가뜨린 사례**입니다."

### 스크린샷 게이트 (#97) — 자동화할 수 없는 것을 다루는 법

```cpp
static TAutoConsoleVariable<int32> CVarDebugShotFrame(
    TEXT("re.Debug.ScreenshotFrame"), 0,
    TEXT("N번째 렌더 프로세서 실행에서 스크린샷 저장 (0=끔). 시각 검증용."), ECVF_Cheat);
```

> "**색 교차가 읽히는지, 밝기가 블룸에 씻기는지는 수치 게이트로 판정할 수 없습니다.**
> 이 CVar가 없으면 매번 PIE를 띄우고 사람이 눈으로 봐야 하고,
> **그 왕복이 렌더 작업의 실제 병목이었습니다.**
>
> **판정 자체는 자동화 못 합니다. 대신 판정에 드는 왕복을 없앴습니다** —
> 커맨드라인 한 줄로 PNG가 떨어집니다."

**부수 함정도 문서화했다:**
- 콘솔 `HighResShot`은 `-game` 뷰포트에서 **조용히 무시된다**(로그도 PNG도 안 남음) → `FScreenshotRequest::RequestScreenshot()` 직접 호출
- `re.Debug.ScreenshotUI 0`이 기본 → **화면공간 위젯(HUD)은 안 찍힌다.** 그런데 **월드스페이스 위젯(보스 체력바)은 찍힌다** → "UI는 나오는데 HUD만 없다"로 오해하기 쉽다
- `-unattended` 헤드리스로는 못 찍는다 — 렌더 프로세서가 안 도니까 훅이 안 걸린다

## 8-6. Niagara 고정비 — 켜는 것만으로 +2.5ms (#98)

**측정**: `re.Fx.Explosions 0`으로 **폭발을 완전히 끈 상태**에서 50,000발 3회 반복

| 런 | Frame mean | Frame p99 | GT |
|---|---:|---:|---:|
| #98 이전 | 11.12 | **15.18** | 11.11 |
| fxoff 1 | 13.94 | 17.52 | 13.94 |
| fxoff 2 | 13.78 | 17.97 | 13.77 |
| fxoff 3 | 13.42 | 17.61 | 13.42 |

> "**세 번 모두 일관되게 나빠졌습니다 — 런간 노이즈(±3%)의 8배입니다. 변동이 아닙니다.**
> `frames.csv`에 `Ticks/FNiagaraWorldManagerTickFunction`이 매 프레임 잡힙니다.
> **스폰이 0이어도 Niagara 월드 매니저가 돕니다.**
> **GPU는 오히려 내려갔습니다(7.46 → 6.64) — 비용은 전적으로 게임 스레드입니다.**"

**설계 문서가 놓친 것:**
> "폭발 스펙 §7은 **폭발 스폰 비용**만 따졌습니다(동시 12발, AutoRelease 풀링).
> **플러그인 고정비는 계산에 없었습니다.**
> 교훈: **서브시스템을 켜는 것 자체가 비용입니다. '쓰지 않으면 공짜'가 아닙니다.**"

## 8-7. 측정 신뢰성 체크리스트 (이 표 자체가 어필거리)

| 확인 항목 | 결과 |
|---|---|
| 탄환 수 목표 유지 | 10회 전부 ±5% 안 |
| 캡처 시작 시점 | 전 런이 **채움 완료 이벤트**에서 시작 — 빈 씬 구간 0 |
| 캡처 길이 | 전 런 720프레임 |
| **CVar 실제 적용** | 6개 소거 런 전부 로그 확인 — **커맨드라인에 있는 것과 런타임에 먹은 것을 구분** |
| 런간 분산 | Mass 5,000 base **3회 반복 → ±3%** — 소거 판정을 이 바닥 기준으로 읽는다 |
| 분포 꼬리 | 평균뿐 아니라 **p99도 함께** — 60fps 판정은 평균만으로 못 한다 |

> "**마지막 항목이 없으면 'bloom 소거 시 GPU 변화 0.2%'를
> 원인이 아니어서인지 노브가 안 먹어서인지 구분할 수 없습니다.**
> 이 리포트의 핵심 반증이 거기 걸려 있습니다."

---
---

# 9부. 클로즈드루프 스폰 컨트롤러 (제어이론)

## 9-1. 문제 — 측정하려는데 목표 탄 수가 안 찬다

측정 하네스가 **"동시 탄환 5,000발 유지"** 를 요구하는데, 오픈루프 스폰은 **3,352발**까지만 찼다.

**왜?** 정상상태 탄 수는 이론적으로:
```
동시 탄 수 = 발사당_탄수 × (수명 / 발사주기)
```
그런데 실제로는 소멸이 이론값과 다르다(프레임 경계, 피격 소멸, 화면 밖 등).
**이론값으로 열고 던지면(오픈루프) 오차가 그대로 남는다.**

## 9-2. 오픈루프 vs 클로즈드루프 — 5살 버전

> **오픈루프** = 샤워기 손잡이를 **"이 정도면 따뜻하겠지"** 하고 딱 맞춰 놓고 그냥 둔다.
> 물이 미지근해도 모른다.
>
> **클로즈드루프(피드백)** = 손을 대보고 **차가우면 조금 더 돌린다.** 계속 반복.
> 결국 원하는 온도에 맞는다.

우리가 "손을 대보는" 방법:
```cpp
// 라이브 탄환 수 = ISM 인스턴스 수 (렌더 프로세서가 매 프레임 엔티티 수로 동기화한다)
CurrentLive = ISM->GetInstanceCount();
```

> **왜 ISM 인스턴스 수인가**: 렌더 프로세서가 이미 매 프레임 **live 엔티티 수에 맞춰** 인스턴스를 조절한다.
> 그러니 이 값이 곧 살아있는 탄 수다. **별도 카운터를 안 만들어도 된다.**
> (관측 불가하면 — 데디서버엔 ISM이 없다 — `-1`이 되고 오픈루프로 폴백한다.)

## 9-3. 실제 코드

```cpp
int32 AREBossCharacter::ResolveSpiralCount()
{
    const int32 CVarCount = CVarBulletCount.GetValueOnGameThread();
    if (CVarCount < 0)
    {
        // 게임플레이 경로 — 발사당 탄수 고정(오픈루프). 동시 탄수는 자연 결정
        return GetDefault<UREStatsSettings>()->BulletsPerShot;
    }

    const int32 TargetLive = CVarCount;
    int32 CurrentLive = -1;
    if (const UREBulletRenderSubsystem* RS = GetWorld()->GetSubsystem<UREBulletRenderSubsystem>())
        if (const UInstancedStaticMeshComponent* ISM = RS->GetISM())
            CurrentLive = ISM->GetInstanceCount();

    const float FeedFwd = TargetLive * FireIntervalSec() / BulletLifetimeSec();

    if (CurrentLive >= 0 && TargetLive > 0)
    {
        const float ShotsPerLife = BulletLifetimeSec() / FireIntervalSec();   // N
        const int32 FillShots    = FMath::CeilToInt(ShotsPerLife);

        if (SpiralShotCount < FillShots)
        {
            SpiralSpawnRate = FeedFwd;                     // ① 채움 구간: 피드포워드만
        }
        else
        {
            if (SpiralShotCount == FillShots)
                CSV_EVENT_GLOBAL(TEXT("REBulletsFilled"));  // ② 캡처 시작 신호

            //  ★ 적분 제어 + N² 무차원화
            SpiralSpawnRate += CVarSpawnKi.GetValueOnGameThread()
                             / (ShotsPerLife * ShotsPerLife)
                             * (TargetLive - CurrentLive);
            SpiralSpawnRate = FMath::Clamp(SpiralSpawnRate, 0.f, (float)TargetLive);  // ③ 안티와인드업
        }

        // ④ 소수부 이월
        SpiralSpawnAccum += SpiralSpawnRate;
        Count = FMath::FloorToInt(SpiralSpawnAccum);
        SpiralSpawnAccum -= Count;
    }
    else
    {
        Count = FMath::RoundToInt(FeedFwd);   // 관측 불가 → 오픈루프 폴백
    }
    // ★ 여기까지가 측정 경로(CVarCount >= 0)다. 위 게임플레이 경로는 이미 return 했으므로
    //    SpiralShotCount 는 클로즈드루프에서만 증가한다 — 그래서 FillShots 비교가 유효하다.
    ++SpiralShotCount;
    return Count;
}
```

## 9-4. 네 가지 장치, 각각의 이유

### ① 피드포워드 채움 구간 — 와인드업 방지

**와인드업(windup)이 뭔가 — 5살 버전:**
> 욕조에 물을 받는데 **아직 차오르는 중**이다. "목표보다 부족하네!" 하고 계속 수도꼭지를 더 연다.
> 물이 다 차고 나면? **수도꼭지가 이미 활짝 열려 있어서 넘친다.**

기술적으로: 적분항이 **큰 오차를 계속 누적**해서, 목표에 도달했을 때 이미 과도하게 커져 있는 현상.

**우리 상황**: **첫 1수명 동안은 아직 탄이 채워지는 중**이라 오차가 크게 양수다.
그 구간에서 적분하면 **대폭 오버슈트**한다.

**해법**: 채움 구간은 **피드포워드(이론값)로만** 채우고, 채워진 뒤부터 적분으로 소멸분을 보정한다.

### ② 채움 완료 이벤트 — 측정 하네스와 연결

```cpp
if (SpiralShotCount == FillShots) { CSV_EVENT_GLOBAL(TEXT("REBulletsFilled")); }
```

> "**채움 완료 = 정상상태 진입**입니다.
> 프로파일 캡처는 이 이벤트에서 시작합니다(`-csvStartOnEvent=REBulletsFilled`).
> `SpiralShotCount`는 매 호출 증가하므로 **정확히 한 번만 발화**합니다.
> **#88이 고친 '부팅 구간을 재던' 문제의 해결책이 바로 이 한 줄입니다.**"

### ③ 안티와인드업 상한

```cpp
SpiralSpawnRate = FMath::Clamp(SpiralSpawnRate, 0.f, (float)TargetLive);
```
적분항이 무한정 커지는 것을 막는다. 하한 0은 음수 발사 방지.

### ④ 소수부 이월 누산

```cpp
SpiralSpawnAccum += SpiralSpawnRate;      // 예: 3.3
Count = FMath::FloorToInt(SpiralSpawnAccum);  // 3
SpiralSpawnAccum -= Count;                // 0.3 남김 → 다음 발사로 이월
```

> "**소형 타깃(rate≈3.3)에서 `round()`를 쓰면 매 발사 4로 올려서 +20% 오버슛합니다.**
> 내림 + 소수부 이월이면 **장기 평균이 정확히 3.3**이 됩니다."

## 9-5. ★ 게인 무차원화 — 이 항목의 핵심 (반드시 이해할 것)

### 문제 상황 (#88)

`3.0/0.1` 설정(수명 3초, 간격 0.1초)에서 게인 `0.004`로 튜닝해서 잘 돌았다.
설정을 `15.0/0.15`(수명 15초, 간격 0.15초)로 바꿨더니 **388~1,802 사이로 진동(리밋사이클)** 했다.
**프로파일 측정이 통째로 무의미해졌다.**

### 왜 그런가 — 제어이론 용어로

```
N = 수명 / 발사간격 = 1수명당 발사 수
```

`N`은 이 루프에서 **두 가지 역할을 동시에 한다:**

| 역할 | 뜻 |
|---|---|
| **데드타임(dead time)** | 지금 쏜 탄이 **소멸로 되돌아오기까지 걸리는 샷 수** — 피드백이 늦게 온다 |
| **정상상태 이득(steady-state gain)** | `live = rate × N` — 스폰율을 1 올리면 라이브가 N만큼 오른다 |

**그래서 루프게인은 `N²`에 비례한다.**
```
설정 3.0/0.1  → N = 30  → 루프게인 = 30² × 0.004 = 3.6   ← 안정
설정 15.0/0.15→ N = 100 → 루프게인 = 100² × 0.004 = 40   ← 진동!
```

### 해법 — 게인을 무차원화

```cpp
SpiralSpawnRate += Ki / (ShotsPerLife * ShotsPerLife) * (TargetLive - CurrentLive);
//                      ↑ N² 으로 나눈다 → 설정이 바뀌어도 루프게인이 고정
```

`Ki`의 기본값 **3.6** = 실측 확정값 0.004를 당시 설정(N=30)에서 환산한 값 (`30² × 0.004 = 3.6`).

> "**무차원화하지 않으면 설정을 바꾸는 순간 조용히 진동합니다.**
> 그리고 그게 실제로 일어났습니다. **CVar로 스윕 가능하게 남겨뒀고**,
> 배증(7.2)하면 여전히 진동합니다."

## 9-6. 왜 PID가 아니라 I만 썼나 (물어보면 만점 답변)

> "**목표가 정상상태 오차 0**이고 플랜트가 **순수 적분기 + 데드타임**입니다.
>
> - **P(비례)** 는 **정상상태 오차를 남깁니다** — 오차가 0이 되면 출력도 0이 되니까, 소멸분을 계속 보충할 수 없습니다
> - **D(미분)** 는 **라이브 카운트 관측 노이즈를 증폭**합니다 — 라이브 카운트는 프레임 단위로 소멸이 튀는 이산 신호입니다
> - **데드타임이 지배적**이라 게인 마진이 좁고, **그래서 무차원화가 실제 문제**였습니다
>
> 즉 D를 붙일 이유가 없고 P는 목표와 안 맞습니다. **I만 남는 게 자연스러운 선택이었습니다.**"

## 9-7. 결과

| | 이전 | 이후 |
|---|---|---|
| 목표 5,000 | 실제 3,352 (−33%) | **실제 4,999** |
| 유지 정확도 | — | 10회 런 전부 **±5% 안** (Mass −1.9%~+0.1%) |

> "**이 컨트롤러 덕분에 Mass와 Actor를 같은 실제 탄 수에서 공정 비교**할 수 있게 됐습니다.
> 그전 M3 측정은 Mass 3,352 vs Actor 5,000이라 비교 자체가 성립하지 않았습니다."

**게다가 이게 있어야 가능했던 실험이 있다:**
> "가설 A(수명 변경이 원인)를 반증할 때 `BulletLifetime`만 3.0으로 되돌려야 했는데,
> **수명을 바꿔도 클로즈드루프가 목표를 유지해 주므로 그 실험이 가능했습니다.**
> 오픈루프였으면 수명을 바꾸는 순간 탄 수가 달라져서 비교가 안 됩니다."

---
---

# 10부. 패턴 수학

**보스 패턴 15종. 전부 720프레임 p99 실측, 전부 60fps 게이트 통과.**

> **"enum은 16개던데요?"** — `EBulletPattern`에는 `Homing`이 있지만 **#67 백로그 스텁**이라
> 페이즈 로테이션 풀에 없고 M7 측정 대상도 아니다(테이블에서 Spiral 동작으로 폴백).
> 맨 뒤 `Count`는 `static_assert` 전용 센티널이다. **실플레이 패턴은 15종이 맞다.**

| 최악 3종 | mean | p99 |
|---|---:|---:|
| LissajousStorm | 8.76 | **14.95** |
| BezierVortex | 10.53 | 13.40 |
| ArtilleryStorm | 8.75 | 12.92 |

## 10-1. 기본 — Spiral / Fan

```cpp
// 나선: 각도 = BaseAngle + i × AngleStep
TArray<FBulletSpawnParams> GenerateSpiral(const FVector& Origin, const FSpiralParams& P)
{
    for (int32 i = 0; i < P.Count; ++i)
    {
        const float Angle = P.BaseAngleDeg + i * P.AngleStepDeg;
        const float ColorSel = float((P.ShotParity + i) & 1);   // 체커보드
        Out.Add({ Origin, DirFromDeg(Angle) * P.Speed, P.Lifetime, ColorSel });
    }
}
```

**보스가 매 발사마다 `BaseAngleDeg`를 `SpiralRotationStepDeg`(15°)씩 누적** → 링이 돌면서 나선이 그려진다.

> **제너레이터는 무상태(stateless) 순수 함수다.** 회전 상태는 호출자(보스)가 소유한다.
> **그래서 엔진 없이 headless로 단위 검증이 가능하다.**

## 10-2. 속력 변조 — Rose / Cardioid

**핵심 아이디어: 각도는 균등하게 두고, 속력만 각도의 함수로 만든다.**

```cpp
// Rose (장미 포락선)
θᵢ = BaseAngle + i·(360/Count)
sᵢ = Speed · (1 + Amp·cos(Lobes·θᵢ + PhaseDeg))
```

**왜 꽃 모양이 되나:**
> 발사 T초 뒤 이 볼리의 **파면**은 `r(θ) = Speed·T·(1 + Amp·cos(k·θ + φ))` —
> **k장 로브의 극좌표 곡선이 자기닮음으로 확대**됩니다.
> **탄 하나하나는 완전한 직선이고, 곡선인 것은 집합의 파면뿐입니다.**
> `PhaseDeg`가 볼리마다 달라서 로브가 회전하니, 화면에는 크기와 위상이 다른 꽃이 여러 겹 겹쳐 보입니다.

```cpp
// Cardioid (심장형/리마송) — 로브가 하나뿐이고 그 하나가 플레이어를 따라온다
s(θ) = Speed · (1 + Amp·cos(θ − AimAngle))
```

**게임 디자인적 의미 (이게 좋은 답변):**
> "**안전지대가 보스 뒤편 하나로 고정**되므로 플레이어가 **보스를 끼고 돌아야** 합니다.
> Rose와 식은 같은 꼴이지만 로브가 하나고 그게 사람을 따라온다는 게 다릅니다."

**미묘한 디테일:**
```cpp
// 변조는 절대 각의 함수다 — 링이 회전해도 로브는 제자리다.
const float Mod = FMath::Cos(FMath::DegreesToRadians(P.Lobes * Angle + P.PhaseDeg));
```
> "로브를 돌리는 축은 `PhaseDeg` 하나뿐이라 **두 회전(링 회전 / 로브 회전)이 서로 상쇄되지 않습니다.**"

## 10-3. CurveBloom — 극좌표의 한계를 넘는 방법 (아주 좋은 소재)

**속력 변조의 한계:**
> "파면이 `r(θ)`인 극좌표 곡선이라 **한 각도에 한 반경**인 모양(별 모양 영역)만 만들 수 있습니다.
> **∞처럼 자기교차하는 곡선은 원리상 못 만듭니다.**"

**블룸의 해법 — 모양을 스폰 위치로 직접 그리고, 속도를 위치에 비례시킨다:**

```
V = P·k   →   위치(T) = P + P·k·T = P·(1 + kT)
```

**→ 도형이 완전한 자기닮음으로 부푼다. 어떤 닫힌 곡선이든 된다.**

```cpp
TArray<FBulletSpawnParams> GenerateCurveBloom(const FVector& Origin, TConstArrayView<FVector2D> Curve,
                                              const FCurveBloomParams& P)
{
    for (int32 i = 0; i < Curve.Num(); ++i)
    {
        const FVector Off(Curve[i].X, Curve[i].Y, 0.f);
        // 속도가 위치에 비례해야 도형이 안 일그러진다 — 균일 속력을 주면 모든 점이
        // 같은 거리를 나아가 모양이 바깥으로 갈수록 둥글게 뭉개진다.
        Out.Add({ Origin + Off, Off * P.ScaleRate, P.Lifetime, float(i & 1) });
    }
}
```

**부수 이득:**
> "`FBulletSpawnParams::Location`이 원래 탄별 필드라 **스폰 인프라를 그대로 씁니다.**
> 그리고 지연 보정(`Location += Velocity·Elapsed`)도 **등속 직선이라 정확히 맞습니다.**"

**곡선 생성기 3종 (전부 순수 함수):**

| 함수 | 곡선 | 특징 |
|---|---|---|
| `GenStarPolygon(N, Skip, ...)` | 별 다각형 {N/Skip} | 변마다 표본을 잘라 **직선 변**을 낸다. `gcd(N,Skip)=1`이라야 한붓그리기로 닫힌다 |
| `GenLemniscate(N, A, ...)` | 베르누이 렘니스케이트(∞) | **원점에서 자기교차** → 극좌표로는 불가능. 블룸이라야 나온다 |
| `GenSuperformula(N, M, n1,n2,n3, ...)` | 기엘리스 초공식 | `m`을 연속으로 움직이면 **꽃↔별↔다각형으로 변태**. 정수가 아니어도 정의된다 |

**초공식 디테일 (수치 안정성):**
```cpp
// T1+T2 는 0 이 될 수 있다(두 항이 동시에 0인 각). 음의 지수라 0 나눗셈이 된다.
const float Sum = FMath::Max(T1 + T2, KINDA_SMALL_NUMBER);
// 반경을 그때그때 최대값으로 정규화 — m/n 이 변하면 raw 반경이 몇 배씩 뛰어서
// 정규화 없이는 morph 중에 도형 크기가 요동친다.
const float R = Radius * Raw[i] / MaxR;
```

## 10-4. 곡사탄 — 3차 베지어 "차수 상승" (최고 소재)

### 무엇을 했나

원래 곡사탄은 **단순 포물선**이었다(XY 선형보간 + `4H·t(1-t)`).
궤적을 성형하고 싶은데(S자, 나선 기둥, 돔), **회귀 없이** 하고 싶었다.

**해법: 2차 베지어를 3차로 "차수 상승(degree elevation)"시킨다.**

```cpp
// C 는 2차 제어점. 아래 ⅔ 식이 그 2차 곡선을 **같은 곡선인 채로** 3차로 올린다.
const FVector C = (Start + Target) * 0.5f + FVector(0.f, 0.f, 2.f * MaxHeight) + CtrlOffset;
Arc.Ctrl1 = Start  + (C - Start)  * (2.f / 3.f) + Ctrl1Offset;
Arc.Ctrl2 = Target + (C - Target) * (2.f / 3.f) + Ctrl2Offset;
```

**수학적 사실:**
```
2차 제어점 C 에 대해
    Ctrl1 = Start  + ⅔(C − Start)
    Ctrl2 = Target + ⅔(C − Target)
이면 두 곡선이 완전히 같다.

그리고 C = 중점 + (0,0,2·MaxHeight) 면 그 2차 곡선이
다시 기존 포물선(XY 선형보간 + 4H·t(1-t))과 같다.
```

> "**오프셋이 전부 0이면 결과가 기존 포물선과 대수적으로 같습니다.**
> 즉 **기존 궤적을 한 픽셀도 안 바꾸고** 성형 능력만 추가한 겁니다.
> 회귀 위험이 0인 확장이었습니다."

### 왜 3차라야 하나

> "**2차 베지어는 제어점이 하나뿐이라 '한 번 휘는 것'밖에 못 합니다.**
> S자(한 번 왼쪽, 한 번 오른쪽)는 원리상 불가능합니다.
> 3차는 제어점이 둘이라 **출발 쪽 굽힘과 착지 쪽 굽힘을 따로 쥘 수 있습니다.**"

### 끝점 불변이 만드는 게임 디자인 이점 (핵심!)

```
끝점은 t=0/1 에서 Start/Target 그대로다
   → 착지 시각·착지점·마커는 제어점과 무관하다
   → 바뀌는 건 가는 길뿐이다
```

> "**궤적을 아무리 화려하게 성형해도 회피 규칙이 안 변합니다.**
> 마커가 예고하는 착지점과 착지 시각이 그대로니까요.
> **시각적 다양성을 늘리면서 게임플레이 계약은 고정**한 겁니다."

### 성형 프리셋 4종

| 프리셋 | 어떻게 미나 | 결과 |
|---|---|---|
| `ArcSpiralColumn` | 두 제어점을 **같은** 접선 방향, 뒤쪽을 1.6배 더 멀리 | 솟았다가 축을 크게 감아 돌아 착지 → **회전하는 기둥** |
| `ArcDomeShell` | 제어점을 각자 자기 끝점 쪽으로 당기고 위로 | 급상승·고공 수평·급강하 → **반구 껍질의 자오선** |
| `ArcSCurve` | 두 제어점을 **반대** 접선 방향 | S자. 부호를 탄마다 뒤집으면 **공중에 리본이 짜인다** |
| `ArcCompassLob` | 출발 제어점을 **목표와 무관한 고정 방향**(동/서/남/북)으로 | **유도가 아니라 고정 발사방향 + 고정 착지점** — 궤적이 발사 순간 완전 결정 |

**`ArcCompassLob`의 디테일 (이해도 어필):**
```cpp
// 차수 상승 기본값은 두 제어점이 중점 쪽으로 ⅔ 당겨져 있다. 그 당김을 되돌린 뒤
// 나침반 방향으로 밀어야 초기 접선이 목표 방향에 오염되지 않는다.
O.Ctrl1 = (Start - Mid) * (2.f/3.f) + Dir * OutDist + FVector(0,0,Rise1);
```
> "**3차 베지어의 초기 접선은 `(Ctrl1 − Start)`입니다.**
> 그래서 목표 쪽 성분을 먼저 상쇄해야 '먼저 동쪽으로 뻗었다가 꺾여 들어가는' 인상이 나옵니다."

## 10-5. 어긋내기(Stagger) — 그리고 그게 실패한 이야기

**문제**: 볼리 하나가 한 프레임에 통째로 나가면 **덩어리**로 보인다.
게다가 **같은 프레임에 죽는다** — 속력이 제각각인 패턴에서 파면 한 줄이 통째로 증발한다.

**해법**: `Elapsed`를 인덱스에 비례해 밀어 순차 발사처럼 보이게 한다.

```cpp
const int32 Slot = i / StormArms;
P.Elapsed += StormFireInterval * (float)(Count - 1 - Slot) / Count;
if (P.Elapsed >= FlightTime) { continue; }   // 이미 착지했을 서브샷은 버린다
```

**직선탄은 위치까지 미리 보낸다:**
```cpp
const float Dt = Interval * Frac;
Params[i].Location += Params[i].Velocity * Dt;   // 등속이라 정확
Params[i].Lifetime -= Dt;
// 소멸 시각도 벌린다 — 위치만 어긋내면 폭이 발사 주기(0.15s ≈ 9프레임)뿐이라
// 여전히 한꺼번에 사라지는 것처럼 보인다
Params[i].Lifetime = FMath::Max(Params[i].Lifetime - VolleyDeathSpreadSec * Frac, 0.1f);
```

### 🔴 실패 사례 1 — 어긋내기가 비행의 79%를 먹었다

> "장미밭·스피로그래프는 겹수를 1로 맞추려고 발사 간격을 1.2초까지 올렸습니다.
> 그랬더니 **어긋냄이 비행의 79%(1.19/1.5)를 먹어서**
> **탄이 보스에서 출발하지 않고 착지점 근처에서 튀어나왔습니다.**
> → 어긋내기를 **돔에만** 걸도록 좁혔습니다."

### 🔴 실패 사례 2 — 블룸에서는 어긋내면 안 된다

> "**블룸은 그 번짐이 곧 도형의 축척 차이**입니다(복사본 간 반경 간격 = R₀·ScaleRate·간격).
> 위치를 어긋내면 **모양이 뭉갭니다.**
> → 블룸은 위치를 안 건드리고 **수명만 흩뜨립니다.**"

**교훈:**
> "**같은 기법이 패턴마다 다르게 작동합니다.**
> '어긋내기'라는 일반 해법을 만들고 전부 적용했더니 두 패턴에서 깨졌습니다.
> **일반화가 항상 옳은 게 아니라는 걸 배웠습니다.**"

## 10-6. 곡사 착지 지오메트리 생성기

전부 **월드 착지점 배열을 반환하는 순수 함수**(FRandomStream 쓰는 것 제외).

| 함수 | 모양 | 디테일 |
|---|---|---|
| `GenRing` | 원형 링 | `BaseAngleDeg`로 링을 통째로 회전 |
| `GenSweepSpiral` | **단일** 나선을 한 발씩 이어 그림 | `T0..T1`을 N등분해 **한 볼리가 실어 나르는 서브샷** 슬롯을 만든다. `Arms`로 겹수 조절. **슬롯 우선 순서**로 반환 |
| `GenLine` | 보스→플레이어 수직 벽 | 진행 방향 수직 벡터 `(-Dir.Y, Dir.X, 0)` |
| `GenGrid` | 격자 | |
| `GenArcSpiral` | 아르키메데스 나선 | 각 `i·137.5°`(황금각), 반경 `√((i+1)/N)` |
| `GenPlayerCluster` | 조준 클러스터 | 중심 1점 + 링 |
| `GenRandom` | 원판 균등 분포 | **`√` 보정** — 안 하면 중앙에 몰린다 |
| `GenLissajous` | 리사주 매듭 | `FreqX·FreqY`가 서로소면 닫힌 매듭, δ를 돌리면 매듭이 꿈틀 |
| `GenRoseCurve` | 장미 곡선 | **r이 음수인 구간은 반대쪽 꽃잎** — 부호를 살려야 꽃이 완성된다. 홀수면 π에서 이미 전체를 그린다 |
| `GenHypotrochoid` | 스피로그래프 | R,r 서로소면 `t`를 `2π·r`까지 돌려야 닫힌다 |

**`GenSweepSpiral`이 특히 영리하다 (RPC 절약과 연결):**
> "보스는 **발사 주기마다 RPC를 한 번** 보내되, 그 안의 N슬롯은
> **지난 주기 동안 한 발씩 나간 것으로 취급**합니다(어긋내기).
> **초당 N/주기 발을 단발로 쏘면서 RPC는 주기당 1회로 묶는 것**이 목적입니다."

**`GenRandom`의 `√` 보정 (기초 수학, 물어보면 좋음):**
```cpp
const float Rad = ArenaRadius * FMath::Sqrt(Rng.FRand());  // √ 보정 = 원판 균등 면적
```
> "반지름을 균등 난수로 뽑으면 **중앙에 몰립니다.**
> 원판에서 반지름 r인 얇은 고리의 면적이 `2πr·dr`이라 **r에 비례**하기 때문입니다.
> `√`를 씌우면 면적 기준 균등이 됩니다."

---
---

# 11부. 이 프로젝트에서 밟은 함정 모음

**면접에서 "어려웠던 점"을 물으면 여기서 고른다. 전부 실제로 겪은 것이다.**

| # | 함정 | 왜 안 잡히나 | 해결 |
|---|---|---|---|
| 1 | **MassGameplay 플러그인 없으면 프로세서 Execute 0회** | 크래시도 로그도 없음. 조용히 죽어 있음 | `.uproject`에서 활성화. M0에서는 CDO 플래그로 구조만 검증 |
| 2 | **씬 컴포넌트 변형 프로세서를 워커에서 돌리면 크래시** | `AddInstance`가 물리 바디를 만듦 | `bRequiresGameThreadExecution = true` |
| 3 | **`ExecuteBefore`로 등록한 FX가 영원히 안 뜸** | 조건이 항상 거짓. 로그·크래시 없음 | `ExecuteAfter` + Defer 이해. **착지점 16배 늘려도 폭발 수 불변**이 결정타 |
| 4 | **머티리얼이 조용히 기본으로 폴백** | `GetMaterial()`은 정상 반환. 코드로 못 잡음 | `bUsedWithInstancedStaticMeshes` 플래그. **스크린샷 CVar를 먼저 만듦** |
| 5 | **ISM 극단적 비등방 스케일 → 인스턴스가 화면에서 사라짐** | 렌더 안 됨. 에러 없음 | 실RHI 스크린샷 이진탐색으로 Z=1.0 안전선 확인 → **Plane 메시로 교체해 원천 해결** |
| 6 | **탄 색을 매 프레임 나이에서 파생 → 화면 전체 깜빡임** | 논리상 맞아 보임 | 스폰 시 프래그먼트에 고정 |
| 7 | **캡처 창을 프레임 수로 잡음** | `-nullrhi`가 1,800fps라 600프레임 = 0.33초 | fps 곱해서 실제 시간 확인 |
| 8 | **첫 폭발에서 290ms 동기 로드** | 다른 시스템(대쉬 거리)을 망가뜨림 | `OnWorldBeginPlay`에서 선로드 |
| 9 | **낡은 스테이징 서버로 검증하고 16항목 PASS** | 클라만 최신, 서버는 쿡 필요 | **빌드 신선도 게이트**(mtime 비교) |
| 10 | **클라에서 `GetPlayerControllerIterator`가 로컬 하나만 반환** | 서버에선 정상 동작 | `TActorIterator<ARECharacterBase>` |
| 11 | **유니티 빌드 C4459** (익명 네임스페이스 동명 변수) | **adaptive non-unity PR 게이트는 통과** | 상수 이름을 파일마다 구분 → 이후 R-07에서 `REBulletGeometry` **네임스페이스로 합쳐 원인 제거** |
| 12 | **`KeepFiring`이 앞단에서 Spiral 고정 + early-return** | BossPattern을 뭘로 줘도 Spiral이 측정됨 | 패턴 고정 시 우회를 안 타게 수정 |
| 13 | **GAS 쿨다운 태그가 5.8에서 폐기된 API** | 겉으로 멀쩡. 프로브도 통과 | `UTargetTagsGameplayEffectComponent`로 이관 (`1.97 → 0.00` 확인) |
| 14 | **데디에서 우클릭 이동이 1/10 속도** | 리슨서버·스탠드얼론은 정상 | 이동목표 복제 + 클라 예측 (`14.9 → 234.3`) |

## 11-1. "조용한 실패"가 이 프로젝트의 주제다

> "이 프로젝트에서 **가장 자주 만난 실패 유형은 크래시가 아니라 '조용히 아무 일도 안 일어남'** 이었습니다.
> - 프로세서가 안 돔 (플러그인 미활성)
> - 조건이 영원히 거짓 (실행 순서)
> - 머티리얼이 폴백 (플래그 누락)
> - 측정 창이 엉뚱한 구간 (하네스 결함)
>
> 그래서 **코딩 규칙으로 '로그 없는 early-return 금지'** 를 세웠습니다.
> 하드닝 리팩터의 절대 규칙 4번이 그겁니다."

---
---

# 12부. 예상 질문 40개 + 답변 스크립트

## A. Mass 기본 (반드시 나온다)

**Q1. Mass Entity가 뭔가요?**
> UE5에 내장된 **데이터 지향 ECS 프레임워크**입니다. 엔티티는 데이터 없는 핸들이고, 데이터는 **Fragment**로 조합합니다. 같은 조합끼리 **Archetype**으로 묶이고, 그 안에서 **Chunk**라는 연속 메모리 블록으로 관리됩니다. **Processor**가 청크 단위로 순회하므로 캐시 친화적이고 워커 스레드로 잘 퍼집니다.

**Q2. 왜 Mass를 썼나요?**
> 문제의 모양이 Mass가 겨냥한 모양과 같았습니다 — **개체가 수만 개, 개체당 로직은 단순, 개별성 거의 없음.**
> 그리고 실측으로 정당화했습니다: 5,000발 동일 조건에서 **게임 스레드 4.03 vs 16.99ms, 드로우콜 96 vs 3,358.**
> 특히 스케일 곡선이 결정적입니다 — 탄이 50배 늘 때 Mass GT는 1.3ms 오르는데 Actor는 14.2ms 오릅니다.

**Q3. Fragment와 Component 차이는?**
> **Mass의 Fragment가 곧 ECS의 Component**입니다. UE에는 이미 `UActorComponent`가 있어 이름이 겹치므로 Fragment라고 부릅니다.
> 차이는 `UActorComponent`가 **객체(가상함수·틱·생명주기)** 인 반면, Fragment는 **순수 데이터 구조체**라는 점입니다.

**Q4. Tag는 왜 있나요? Fragment로 하면 안 되나요?**
> 크기 0인 필터 전용입니다. 직선탄과 곡사탄이 둘 다 `FTransformFragment`를 가지므로 **쿼리로 구분하려면 표식이 필요**합니다.
> 빈 Fragment로 해도 되지만, Tag는 **아키타입 메모리에 자리를 차지하지 않습니다.**

**Q5. Archetype이 뭔가요? 왜 필요한가요?**
> **같은 Fragment/Tag 조합을 가진 엔티티들의 그룹**입니다. 조합이 같으면 메모리 레이아웃이 같아서 **배열로 쭉 붙일 수 있습니다** — SoA가 성립하는 근거입니다.
> 저희는 아키타입이 2개입니다: 직선탄, 곡사탄. **스폰 시점에 조합을 확정하고 절대 안 바꿉니다** — 아키타입 이동이 메모리 재배치라 비싸기 때문입니다.

**Q6. Chunk가 뭔가요?**
> 아키타입 안에서 엔티티를 담는 **고정 크기 연속 메모리 블록**입니다. 청크 하나 안에서는 완벽한 SoA입니다.
> 청크끼리 완전 독립이라 **다른 스레드가 다른 청크를 동시에 처리해도 안전**합니다 — 이게 Mass 병렬화의 단위입니다.

**Q7. 왜 `ForEachEntityChunk`인가요? 엔티티 하나씩이 아니라?**
> **함수 호출 비용을 없애고 안쪽을 평범한 배열 for문으로 만들기 위해서**입니다.
> 5,000발이면 람다 호출이 5,000번 → **6번 정도**로 줍니다(기본 청크 128KB, 저희 탄환은 엔티티당 ~136B라 청크당 900개 안팎).
> 안쪽 for문은 연속 메모리 순차 접근이라 **하드웨어 프리페처가 먹고, 컴파일러 SIMD 자동 벡터화도 가능**합니다.

**Q8. AoS와 SoA 차이를 설명해 주세요.**
> AoS는 객체 하나에 모든 필드를 묶어 배열로 만든 것, SoA는 필드별로 배열을 따로 만든 것입니다.
> 이동 계산은 위치와 속도만 씁니다. AoS면 캐시 라인에 **안 쓰는 필드가 딸려 와서 낭비**되고, SoA면 캐시 라인이 전부 쓸 데이터입니다.
> Mass는 청크 안에서 SoA로 저장합니다.

**Q9. `EMassFragmentAccess::ReadOnly`와 `ReadWrite`의 차이가 성능에 영향이 있나요?**
> 단순 힌트가 아니라 **Mass의 스레드 스케줄링 근거**입니다. Mass가 이 선언을 보고 "두 프로세서가 같은 프래그먼트를 안 건드리니 동시에 돌려도 된다"를 판단합니다.
> 저희 렌더 프로세서는 트랜스폼을 **ReadOnly**로 잡습니다 — **"렌더는 게임 로직에 영향을 주지 않는다"는 계약이 코드에 박혀 있는 셈**입니다.

## B. 아키텍처 판단

**Q10. Mass 대신 그냥 Actor 풀링을 쓰면 안 되나요?**
> 풀링은 **Spawn/Destroy 비용**만 없앱니다. 남는 세 가지가 그대로입니다:
> ① 액터당 개별 `Tick()` 가상 호출, ② **드로우콜이 액터 수만큼**, ③ 액터 데이터가 힙에 흩어져 캐시 미스.
> 실측에서 Actor 5,000의 GT 16.99ms 중 대부분이 이 세 가지입니다.

**Q11. Niagara로 하면 더 빠르지 않나요? (반드시 나온다)**
> 성능 이유로는 정당화되지 않는다고 **측정으로 결론냈습니다.** GPU 예산의 1/3만 쓰고 있고, 그중 탄환 몫은 더 작습니다.
> 그리고 구조적 제약이 있습니다 — **Niagara는 이동을 소유할 수 없습니다.**
> 데디서버에는 렌더가 없어서 Niagara 파티클도 없는데, 위치를 Niagara가 가지면 **서버 권위 피격 판정이 성립하지 않습니다.**
> 순수 렌더러로만 가능하고, 그러면 얻는 건 GT 전달 비용 하나뿐인데 그건 #95에서 이미 73% 줄였습니다.
> **Niagara를 쓴다면 근거는 성능이 아니라 시각 품질(트레일·페이드)이어야 하고, 그러면 완료 조건도 그 축으로 다시 써야 합니다.**

**Q12. 렌더 프로세서는 왜 게임 스레드인가요? (핵심 질문)**
> **ISM은 씬 컴포넌트라 트랜스폼 갱신이 게임 스레드 전용**입니다. `AddInstance`가 내부에서 물리 BodyInstance를 만들어서, 워커 스레드에서 부르면 어서션 크래시가 납니다. 그래서 `bRequiresGameThreadExecution = true`를 겁니다.
> **그리고 이 제약이 곧 남은 병목의 위치를 정합니다** — 50,000발에서 GT 12.08 vs GPU 7.69로 게임 스레드 바운드입니다.

**Q13. Mass 세계와 UE 액터 세계는 어떻게 연결되나요?**
> 저희는 **의도적으로 연결을 최소화**했습니다. `MassActors` 모듈(엔티티↔액터 자동 연동)을 안 씁니다 — 탄환은 액터가 될 이유가 없기 때문입니다.
> 연결 지점은 딱 세 곳입니다: ① 렌더 프로세서가 ISM에 트랜스폼 복사, ② 히트 프로세서가 `TakeDamage` 호출, ③ FX 프로세서가 Niagara 스폰.
> **그 세 곳이 정확히 `bRequiresGameThreadExecution`을 거는 지점**입니다.

**Q14. 서버와 클라 역할 분리는 어떻게 했나요?**
> `if (HasAuthority())` 분기가 아니라 **프로세서 등록 단계**에서 처리했습니다.
> `ExecutionFlags`로 Sim=7(AllNetModes), Render=5(Standalone|Client), ArcHit=3(Standalone|Server)를 줍니다.
> **데디서버는 렌더 프로세서를 아예 실행하지 않습니다.** 조건문이 없으니 실수로 빠뜨릴 수도 없고, 서버 렌더 비용이 정확히 0입니다.

**Q15. `Defer()`를 왜 쓰나요?**
> 순회 중 엔티티를 파괴하면 **청크 배열이 재배치**돼서 잡아둔 `TArrayView`가 무효화됩니다. Mass는 명령을 버퍼에 모았다가 **처리 페이즈 끝에 flush**합니다.
> **그리고 그 지연이 저희 설계의 전제입니다** — 같은 프레임에 Sim이 착지를 감지하고 Defer로 파괴 예약을 걸어도, Hit과 FX가 **여전히 살아있는 엔티티를 읽어** 데미지와 폭발을 처리할 수 있습니다.
> 그래서 FX에 중복 스폰 가드를 안 뒀습니다 — **두 번 볼 경로가 구조적으로 없습니다.**

## C. 성능·측정

**Q16. 45,000이라는 숫자는 어떻게 나왔나요?**
> 게이트가 **프레임 p99 ≤ 16.6ms**(60fps 예산)입니다. 720프레임 캡처, **탄환 채움 완료 시점부터** 잽니다.
> 40,000이 p99 14.95, 45,000이 16.53, 50,000이 17.52라 **45,000이 상한**입니다.
> 이력이 있습니다: 30,000 → #95에서 50,000 → #98에서 Niagara 플러그인 고정비로 45,000.

**Q17. 왜 평균이 아니라 p99인가요?**
> **부드러움은 평균이 아니라 최악에서 결정**되기 때문입니다. 평균 10ms여도 가끔 30ms가 섞이면 사람은 끊긴다고 느낍니다.
> 실제로 Actor 5,000은 평균 17.08도 넘지만 **p99 23.24로 분포 꼬리에서 더 크게 실패**합니다.

**Q18. GPU 병목이었다가 아니라고 결론난 이야기를 해주세요. (최고 소재)**
> M3에서 Mass 5,000 GPU가 38.61ms로 나왔고 그게 Niagara 전면 교체의 근거였습니다.
> 그런데 **분해가 합과 안 맞았습니다** — 프로세서 총합이 0.88ms인데 프레임이 39.5ms였습니다. 둘 중 하나가 틀린 겁니다.
> 재측정하니 GPU 5.28ms. **7.3배 차이인데 GT만 4.45→4.03으로 안 변했습니다.** 그게 결정적 단서였습니다 —
> **로딩·셰이더 컴파일 구간에서는 GPU/RT가 PSO 컴파일로 얻어맞는 동안 게임 스레드는 대기하며 놉니다.**
> 실제로 17.9초 창 중 13.3초가 부팅 구간이었습니다.
> M3 리포트에 **무효 배너를 직접 달고 숫자만 갈아 끼웠습니다.** 결론(Mass가 낫다)은 재측정에서 오히려 강해졌습니다.
> 그리고 **#50을 코드 변경 없이 닫았습니다.**

**Q19. bloom이 원인이 아니라는 걸 어떻게 확신했나요?**
> 두 가지를 구분해야 했습니다 — **원인이 아니어서인가, 노브가 안 먹어서인가.**
> `r.BloomQuality 0` 소거 시 GPU는 0.01ms만 줄었지만, **렌더스레드 `PostFX` 컬럼은 0.31 → 0.22로 줄었습니다.** 노브는 확실히 작동한 겁니다.
> 그리고 6개 소거 런 전부 **로그에서 CVar가 런타임에 실제 적용됐는지** 확인했습니다.
> 참고로 감소분 0.01ms는 **런간 노이즈 바닥(±3%) 아래**라, 정확히는 "0.2% 기여"가 아니라 **"이 측정의 분해능으로 0과 구별 불가"** 입니다.

**Q20. 두 줄로 상한을 30,000→50,000으로 올렸다는 게 뭔가요?**
> ① 탄환 ISM 3개에 `SetCastShadow(false)` — 40,000발에서 **GPU 17.16 → 6.13ms.** 탄막에서 탄환 그림자는 시각 기여가 사실상 없는데 GPU 최대 소비처였습니다.
> ② 그림자를 끄니 병목이 게임 스레드로 넘어갔고, 분해해보니 **`BulletRender`가 GT의 52%**였습니다. 인스턴스당 개별 `UpdateInstanceTransform`을 **`BatchUpdateInstancesTransforms`** 로 바꿨습니다 — `TArray<FTransform>`은 이미 만들어져 있었으니까요.
> 결과: `BulletRender` −73%, GPU −60%, Frame −49%.

**Q21. 렌더 결과가 안 망가진 건 어떻게 확인했나요?**
> 당시 스크린샷 수단이 없어서 **지표로 확정**했습니다.
> `RHI/PrimitivesDrawn` 81,087 → 80,997, `RenderBasePass` 0.02 → 0.03으로 **메인 뷰 지오메트리가 그대로**인데 GPU만 떨어졌습니다.
> **그림자 뎁스 패스만 사라지고 베이스 패스는 불변이라는 지문**입니다. `RenderShadows`가 0.19ms 남은 것도 정합적입니다(캐릭터·레벨 그림자는 유지).

**Q22. 측정 도구를 어떻게 만들었나요?**
> PowerShell 3종입니다. `profile.ps1`이 실 RHI로 띄우고 CSV 캡처, `profile-stats.ps1`이 mean/p99 표를 냅니다 — **사람 눈이 개입하지 않습니다.**
> 프로세서마다 `CSV_SCOPED_TIMING_STAT`을 걸어서 `frames.csv`에 프로세서별 컬럼이 나오게 했고, `TRACE_CPUPROFILER_EVENT_SCOPE`로 Insights 타임라인도 같이 남깁니다.
> 캡처는 `-csvStartOnEvent=REBulletsFilled`로 **정상상태 진입 후부터만** 합니다.

## D. 네트워크

**Q23. 5만 발을 어떻게 복제하나요?**
> **복제하지 않습니다.** Mass 엔티티는 복제되지 않고, 복제해서도 안 됩니다.
> **총알이 아니라 "총알을 만드는 법"을 보냅니다** — Multicast RPC로 `(패턴, 원점, 각도, 개수, 서버시각)`을 보내면 서버와 클라가 **같은 순수 함수를 실행**해서 같은 탄을 만듭니다.
> 수천 발이 파라미터 5개로 압축됩니다.

**Q24. 시드만 보내면 되지 않나요?**
> 부족합니다. 시드가 결정하는 건 **페이즈 로테이션과 Artillery 모양 선택**뿐입니다.
> Fan의 중심각과 Artillery 착지점은 **그 순간 플레이어 위치**에 달렸는데, **플레이어 위치는 난수가 아니라 시드에서 유도할 수 없습니다.**
> 그래서 시드가 아니라 **계산된 발사 파라미터 자체**를 보냅니다. 난수가 필요한 부분(Random 착지 모양)만 `CallSeed`를 따로 실어서 양쪽이 같은 `FRandomStream`을 만듭니다.

**Q25. 클라의 지연은 어떻게 보정하나요?**
> `ServerTime`을 같이 보내고, 클라는 `Elapsed = 현재서버시각 − 발사시각`만큼 **미리 진행된 상태로 스폰**합니다.
> 직선탄은 등속 직선이라 `위치 += 속도 × Elapsed`가 **오차 없이 정확**하고, 곡사탄은 파라미터 곡선이라 `Elapsed`만 넣으면 됩니다.
> **시뮬레이션 모델을 단순하게 유지한 것이 네트워크 보정을 정확하게 만든 사례**입니다.
> 서버에서는 `Elapsed ≈ 0`이라 **같은 코드가 무보정으로 동작합니다** — 분기가 필요 없습니다.

**Q26. `GetElapsedSince`의 Clamp 상하한 이유가 각각 다르다던데요?**
> 네. **하한 0**은 접속 직후 GameState 복제 전이면 `GetServerWorldTimeSeconds()`가 0을 반환해서 큰 음수가 나오는 걸 막습니다.
> **상한 1초**는 클라 시각이 EMA 수렴 중이거나 서버 재시작 직후에 큰 양수가 나오는 걸 막습니다 — 안 막으면 **볼리 전체가 "이미 착지함"으로 스킵돼서 조용히 빈 화면**이 됩니다.
> 이 프로젝트에서 조용한 실패가 측정을 두 번 망친 전례가 있어서, 경계마다 "안 막으면 어떻게 조용히 실패하는가"를 주석에 남깁니다.

**Q27. 클라마다 탄 색이 어긋나지 않나요?**
> `ShotParity`를 **`ServerTime`에서 순수 유도**합니다: `floor(ServerTime / Interval) & 1`.
> 이 함수는 서버와 클라가 **같은 `ServerTime`으로 실행**하므로 별도 복제 없이 일치합니다.
> **카운터를 따로 두면 멀티캐스트 유실 시 클라마다 어긋납니다.** 로브 위상, 링 회전도 같은 원리입니다.

**Q28. 그럼 전부 시간에서 유도하면 되나요? (좋은 반례)**
> 아닙니다. **연속성이 필요한 값은 시간에서 유도하면 안 됩니다.**
> MicroMissile은 발사 방향을 동→서→북→남으로 순환시키는데, `floor(ServerTime/Interval)`을 쓰니 **타이머 지터로 인덱스를 건너뛰었습니다.**
> 실측 발사 간격이 104/98/103/97/105ms로 ±5ms 흔들려서 순환이 깨지고 연속한 두 발이 인접 방향에서 오기도 했습니다.
> **볼리 카운터를 페이로드로 보내는 쪽으로 바꿨습니다** — 카운터는 지터와 무관하게 정확히 1씩 늡니다.

**Q29. 피격 판정은 어디서 하나요?**
> **데미지는 서버 권위, 소멸과 폭발은 양쪽 각자**입니다.
> 판정 프로세서 자체는 `AllNetModes`로 열어두고, `TakeDamage`만 `HasAuthority()` 가드로 막습니다.
> 정당화 근거는 **클라가 이미 `ServerTime`으로 탄 위치를 자체 계산하고 있다**는 점입니다 — 판정할 데이터를 이미 갖고 있으니 서버가 알려줄 필요가 없습니다.
> 반대로 하면 곡사탄 수천 개의 폭발을 Multicast로 알려야 하고 **그 자체가 병목**이 됩니다.

**Q30. 5,000발 판정을 어떻게 하나요? 물리 오버랩인가요?**
> 아닙니다. **탄환은 액터가 아니고 콜리전도 없습니다**(`NoCollision`) — 물리 오버랩을 쓸 수 없습니다.
> 프로세서가 청크 단위로 순회하면서 **XY 평면 제곱거리**를 비교합니다. 탑다운이라 Z는 의미가 없고, 오히려 3D로 재면 탄환 Z와 캡슐 중심 Z가 안 맞아 오판합니다.
> 대상은 **살아있는 플레이어만** 먼저 모으고, 비어 있으면 조기 반환합니다. `bRequiresGameThreadExecution`인 이유는 `TakeDamage`가 액터 호출이라서입니다.

## E. 제어이론·수학

**Q31. 클로즈드루프 스폰 컨트롤러가 뭔가요?**
> 측정 하네스가 "동시 탄환 5,000발 유지"를 요구하는데 오픈루프로는 3,352발까지만 찼습니다.
> **ISM 인스턴스 수를 라이브 카운트로 피드백**해서 스폰율을 오차만큼 램프하는 **적분 제어기**를 넣었습니다.
> 세 가지 장치가 붙습니다: **채움 구간 피드포워드**(와인드업 방지), **안티와인드업 상한**, **소수부 이월 누산**.
> 결과: 목표 5,000에 실제 4,999, 10회 런 전부 ±5% 안.

**Q32. 왜 PID가 아니라 I만?**
> 목표가 **정상상태 오차 0**이고 플랜트가 **순수 적분기 + 데드타임**입니다.
> **P는 정상상태 오차를 남깁니다** — 오차가 0이면 출력도 0이라 소멸분을 계속 보충할 수 없습니다.
> **D는 관측 노이즈를 증폭합니다** — 라이브 카운트는 프레임 단위로 소멸이 튀는 이산 신호입니다.
> 데드타임이 지배적이라 게인 마진이 좁고, **그래서 실제 문제는 무차원화였습니다.**

**Q33. 게인 무차원화가 뭔가요?**
> `N = 수명/발사간격`이 이 루프에서 **두 역할을 동시에** 합니다 — **데드타임**(쏜 탄이 소멸로 되돌아오기까지의 샷 수)이자 **정상상태 이득**(`live = rate × N`)입니다.
> 그래서 **루프게인이 N²에 비례**합니다.
> `3.0/0.1`(N=30)에서 튜닝한 게인이 `15.0/0.15`(N=100)로 바뀌며 **루프게인이 3.6에서 40이 되어 388~1,802 리밋사이클**에 빠졌고, 프로파일 측정이 통째로 무의미해졌습니다.
> `Ki / N²`로 나눠서 **설정과 무관하게 안정성이 고정**되게 했습니다.

**Q34. 곡사 궤적의 3차 베지어 승격이 뭔가요?**
> 기존 궤적은 단순 포물선이었는데, 궤적을 성형하고 싶었습니다. 그런데 **회귀 없이** 하고 싶었습니다.
> 2차 베지어 제어점 C를 `Ctrl1 = Start + ⅔(C−Start)`, `Ctrl2 = Target + ⅔(C−Target)`로 **차수 상승**시키면 **완전히 같은 곡선**이 3차로 표현됩니다.
> 그리고 `C = 중점 + (0,0,2H)`면 그 2차 곡선이 기존 포물선과 같습니다.
> **즉 오프셋이 전부 0이면 결과가 대수적으로 기존 궤적과 동일**하고, 오프셋을 주면 2차로는 못 만드는 S자·깊은 감김이 나옵니다.
> 그리고 **끝점은 불변**이라 착지 시각·착지점·마커가 안 변합니다 — **시각적 다양성을 늘리면서 게임플레이 계약은 고정**한 겁니다.

**Q35. Rose 패턴은 어떻게 만드나요?**
> **각도는 균등 링이고 속력만 각도의 함수**입니다: `s(θ) = Speed·(1 + Amp·cos(k·θ + φ))`.
> 발사 T초 뒤 파면이 `r(θ) = Speed·T·(1 + Amp·cos(k·θ+φ))` — **k장 로브의 극좌표 곡선이 자기닮음으로 확대**됩니다.
> **탄 하나하나는 완전한 직선이고, 곡선인 것은 집합의 파면뿐**입니다.

**Q36. 그럼 ∞ 모양도 속력 변조로 되나요? (심화)**
> 안 됩니다. 극좌표 `r(θ)`는 **한 각도에 한 반경**이라 자기교차하는 곡선을 원리상 못 만듭니다.
> 그래서 **CurveBloom**을 따로 만들었습니다 — **모양을 스폰 위치로 직접 그리고 속도를 위치에 비례**시킵니다.
> `V = P·k`면 `위치(T) = P·(1 + kT)`가 되어 **어떤 닫힌 곡선이든 자기닮음으로 부풉니다.**
> 그리고 지연 보정도 등속 직선이라 정확히 맞습니다.

## F. 태도·과정

**Q37. 이 프로젝트에서 가장 어려웠던 건?**
> **조용히 실패하는 것들**이었습니다. 크래시나 에러 로그는 오히려 쉽습니다.
> 프로세서가 아예 안 돌거나(플러그인 미활성), 조건이 영원히 거짓이거나(실행 순서), 머티리얼이 조용히 폴백하거나(용도 플래그), 측정 창이 엉뚱한 구간이거나.
> 곡사탄 폭발이 한 번도 안 뜬 버그는 **착지점을 16배로 늘려도 폭발 수가 안 움직이는 걸** 보고 잡았습니다.
> 그래서 코딩 규칙으로 **"로그 없는 early-return 금지"** 를 세웠습니다.

**Q38. 게임플레이가 얕지 않나요?**
> 의도적입니다. **성능과 네트워크 축에 집중했다**고 문서 첫 줄에 선언했습니다.
> 보스 1종·승패 2상태인 건 **측정 대상을 고정하기 위해서**입니다 — 게임 시스템이 늘면 프로파일이 무엇을 재는지 흐려집니다.
> 대신 **보스 패턴 15종은 전부 p99를 실측**했고 전부 게이트를 통과합니다.

**Q39. 혼자 만든 게 맞나요?**
> **PR 71개가 전부 이슈 단위 측정 근거를 달고 있습니다.** 커밋 히스토리와 프로파일 산출물이 답입니다.
> 특히 판단이 바뀐 기록이 남아 있습니다 — M3 리포트에 무효 배너를 달고, #50을 코드 변경 없이 닫고, 성능이 좋아졌는데 되돌린(BulletScale 0.2) 기록까지요.

**Q40. 다시 만든다면 뭘 다르게 하겠어요?**
> 세 가지입니다.
> ① **측정 하네스를 처음부터 제대로.** #88에서 하네스 결함 3개를 고쳤는데, 그전 측정이 전부 무효가 됐습니다. **도구가 조용히 틀린 값을 주면 그 위에 쌓는 모든 판단이 틀립니다.**
> ② **시각 검증 수단을 먼저.** 스크린샷 CVar를 #97에서야 만들었는데, 그게 없던 동안 육안 확인 왕복이 렌더 작업의 실제 병목이었습니다.
> ③ **패턴 지식이 7곳에 병렬로 나열**돼 있었습니다. 패턴을 추가할 때 7곳을 고쳐야 하고 하나 빠져도 컴파일이 통과합니다 — 조용한 오동작 위험입니다. 처음부터 단일 테이블로 갔어야 했습니다.
> **지금은 고쳤습니다**(R-04, #141) — `REBossPatternTable.h`가 단일 출처고, `static_assert` 두 개가 강제합니다: 테이블 순서가 `EBulletPattern`과 일치하는지, 그리고 행 수가 센티널 `EBulletPattern::Count`와 같은지. **이제 테이블 한 줄을 빠뜨리면 빌드가 깨집니다.** 센티널을 enum 맨 뒤에 넣었기 때문에 기존 값이 하나도 안 바뀌어 **네트워크 페이로드 호환도 유지**됩니다.

---
---

# 13부. 용어 사전

## Mass / ECS

| 용어 | 뜻 |
|---|---|
| **Entity** | 데이터 없는 핸들. `Index` + `SerialNumber` 두 개뿐. UObject가 아니다 |
| **Fragment** | ECS의 Component. `USTRUCT` + `FMassFragment`. 순수 데이터 |
| **Tag** | 크기 0인 필터 전용 표식. `FMassTag` 상속 |
| **Archetype** | 같은 Fragment/Tag 조합을 가진 엔티티들의 그룹. 메모리 레이아웃이 같다 |
| **Chunk** | 아키타입 안의 고정 크기 연속 메모리 블록. 병렬화 단위. 안에서 SoA |
| **Processor** | ECS의 System. `ConfigureQueries` + `Execute` |
| **Query** | "이런 Fragment/Tag 가진 애들" 선언. Access(RO/RW)와 Presence(All/Any/None) |
| **`ForEachEntityChunk`** | 청크 단위 순회. 안쪽은 평범한 배열 for문 |
| **`Defer()`** | 지연 명령 버퍼. 처리 페이즈 **끝에** flush |
| **`ExecutionFlags`** | 어느 넷모드에서 돌지. Standalone=1, Server=2, Client=4, All=7 |
| **`bRequiresGameThreadExecution`** | 이 프로세서를 게임 스레드에 고정 |
| **`ExecuteBefore/After`** | 프로세서 실행 순서 |
| **아키타입 이동** | Fragment 추가/제거 시 다른 아키타입으로 메모리 재배치. 비싸다 |

## 성능

| 용어 | 뜻 |
|---|---|
| **캐시 라인** | CPU가 메모리에서 한 번에 퍼오는 단위(보통 64B). 이웃 데이터가 공짜로 딸려온다 |
| **AoS / SoA** | 구조체의 배열 / 배열의 구조체 |
| **메모리 지역성** | 다음에 쓸 데이터가 방금 쓴 것 근처에 있는 성질 |
| **GT / RT / GPU** | 게임 스레드 / 렌더 스레드 / GPU. 파이프라인이라 **가장 느린 것이 프레임 시간** |
| **~바운드** | 그 단계가 병목이라는 뜻 (예: GT 16.99 ≈ Frame 17.08 → GT 바운드) |
| **Frame p99** | 프레임 시간 상위 1% 값. **부드러움은 평균이 아니라 여기서 결정** |
| **60fps 예산** | 16.67ms |
| **드로우콜** | CPU가 GPU에 "이거 그려" 하는 명령. 개수가 CPU 부담 |
| **오버드로우** | 같은 픽셀을 여러 번 칠하는 것. 반투명에서 심각 |
| **early-Z** | 가려질 픽셀을 미리 버리는 최적화. **불투명에서만 작동** |
| **필레이트** | 픽셀을 칠하는 처리량. 해상도·오버드로우에 좌우 |
| **PSO 컴파일** | 파이프라인 상태 객체(셰이더 조합) 컴파일. 첫 등장 시 히치 |
| **리밋사이클** | 제어 루프가 목표 주변에서 계속 진동하는 상태 |
| **와인드업** | 적분항이 과도하게 누적돼 오버슛하는 현상 |

## 렌더

| 용어 | 뜻 |
|---|---|
| **ISM** | `InstancedStaticMeshComponent`. 같은 메시 수천 개를 한 드로우콜로 |
| **퍼인스턴스 커스텀데이터** | ISM 인스턴스마다 실어 보내는 float. 머티리얼이 읽는다. 인스턴싱 안에서 개별성을 만드는 유일한 통로 |
| **`bUsedWithInstancedStaticMeshes`** | 머티리얼 용도 플래그. 없으면 **조용히 기본 머티리얼 폴백** |
| **언릿(Unlit)** | 라이팅 계산을 안 하는 셰이딩 모델. 우리 경우 **프레넬을 더해도 GPU가 내려갔다** |
| **프레넬 림** | 시선과 표면 각도에 따라 가장자리를 강조 |
| **Lumen 서피스 캐시** | 동적 GI 캐시. 움직이는 인스턴스 수천 개가 매 프레임 무효화하면 GPU가 튄다 |
| **GPUScene** | 엔진의 인스턴스 데이터 GPU 저장소 |

## 네트워크

| 용어 | 뜻 |
|---|---|
| **Multicast RPC** | 서버가 모든 클라에게 호출. 저희는 여기에 발사 파라미터를 싣는다 |
| **`FVector_NetQuantize`** | 위치를 압축 전송(정밀도 1cm) |
| **`GetServerWorldTimeSeconds`** | GameState가 동기화하는 서버 시각. 파생 값의 공통 기준 |
| **`ROLE_Authority` / `SimulatedProxy`** | 서버 권위 / 클라의 복제본 |
| **넷모드** | Standalone(싱글) / DedicatedServer / Client / ListenServer |
| **스테이징(Staging)** | 쿡된 산출물을 실행 형태로 배치. **서버 exe는 여기서 뜨므로 재빌드만으론 안 바뀐다** |

---
---

# 14부. 치트시트 — 한 장 암기용

## 숫자 (틀리면 안 되는 것)

```
■ Mass vs Actor (5,000발, 동일 조건)
  GameThread   4.03 ms   vs   16.99 ms      (4.2배)
  DrawCalls        96    vs    3,358        (35배)
  Frame p99      8.80 ms vs    23.24 ms

■ 스케일 곡선 (게임 스레드 ms)
  탄환 →      100    1000    5000
  Mass       2.71    3.17    4.03   ← 평평
  Actor      2.84    5.22   16.99   ← 붕괴

■ 프로세서 3분해 (5,000발)
  BulletSim    0.04 ms  [워커 스레드]
  BulletRender 0.74 ms  [게임 스레드]
  BulletHit    0.10 ms  [게임 스레드]
  총합         0.88 ms  = 프레임의 약 2%

■ 60fps 투사체 상한
  30,000 → 50,000 (#95 그림자+배치) → 45,000 (#98 Niagara 고정비)
  게이트: Frame p99 ≤ 16.6 ms

■ #95 두 줄 변경 (40,000발)
  BulletRender  6.762 → 1.826  (−73%)
  GPU          17.156 → 6.874  (−60%)
  Frame        18.011 → 9.134  (−49%)

■ GPU 소거 (5,000발, base 5.28)
  bloom  −0.01 (노이즈 이하)  /  그림자 −2.04 (38.6%)  /  해상도½ −1.52 (28.8%)

■ 탄환 지오메트리
  탄 간격 = 속도200 × 간격0.15 = 30uu  <  탄 지름 = 100 × 0.5 = 50uu  → 겹친다
  HitRadius 60 = 탄 시각반경 25 + 판정여유 35 (캡슐 반경은 34, 올림값이 35)
  BulletDamage 10, 플레이어 100HP → 10발 사망

■ 제어기
  N = 수명/간격,  루프게인 ∝ N²,  Ki 기본 3.6 (= 30² × 0.004)
  실패 사례: N=30(게인 3.6) 튜닝값이 N=100에서 게인 40 → 388~1,802 진동

■ Niagara 플러그인 고정비: 게임 스레드 +2.5 ms (폭발 0개여도)
■ 첫 폭발 동기 로드: 290 ms (나머지 프레임의 300배)
■ 패턴 15종 최악: LissajousStorm p99 14.95 ms
```

## ExecutionFlags 표

```
Standalone=1  Server=2  Client=4   AllNetModes=7   Standalone|Client=5

Sim(직선/곡사)  7   ← 시뮬은 어디서나 (판정 근거)
Render(직선/곡사) 5   ← 데디서버 skip
Hit(직선)       7   ← 판정은 양쪽, 데미지만 HasAuthority
ArcHit         3   ← 곡사 스플래시는 서버만
ArcFx          5   ← 폭발은 화면 있는 곳만
```

## 한 문장 답변 카드

| 질문 | 한 문장 |
|---|---|
| 왜 Mass? | 개체 많고·로직 단순하고·개별성 없는 문제라 Mass가 겨냥한 모양과 같았고, **GT 4.03 vs 16.99, 드로우콜 96 vs 3,358**로 확인했다 |
| 왜 렌더는 GT? | **ISM은 씬 컴포넌트라 트랜스폼 갱신이 GT 전용**이고, **그 제약이 곧 남은 병목의 위치를 정한다** |
| 왜 청크 순회? | 함수 호출을 없애고 **안쪽을 평범한 배열 for문**으로 만들어 프리페처·SIMD가 먹게 |
| 왜 Defer? | 순회 중 파괴하면 뷰가 무효화된다. **그리고 그 지연 덕에 Sim/Hit/FX가 같은 프레임에 착지탄을 정확히 한 번씩 본다** |
| 5만 발 복제? | **안 한다. 총알이 아니라 총알 만드는 법(파라미터 5개)을 Multicast로 보낸다** |
| 지연 보정? | `ServerTime`을 실어서 `Elapsed`만큼 진행된 상태로 스폰. **등속 직선이라 오차 없이 정확** |
| 판정 위치? | **데미지는 서버 권위, 소멸·폭발은 양쪽 각자.** 클라는 이미 위치를 자체 계산하니 판정 근거가 있다 |
| 왜 Niagara 안 씀? | **측정이 필요 없다고 했다.** 그리고 서버엔 렌더가 없어서 **Niagara가 위치를 소유하면 서버 권위 판정이 깨진다** |
| 가장 어려웠던 것? | **조용한 실패.** 크래시가 아니라 "아무 일도 안 일어남" — 그래서 로그 없는 early-return을 금지 규칙으로 |

## 3분 발표 스크립트 (막힐 때 이대로)

> "UE5 Mass Entity로 만든 탑뷰 탄막 보스전입니다. **60fps에서 투사체 45,000개**를 유지합니다.
>
> Mass를 고른 이유는 문제의 모양 때문입니다 — 개체가 수만 개인데 개체당 로직은 등속 직선 이동뿐이고 개별성이 없습니다. 정확히 ECS가 잘하는 모양입니다.
>
> 근거는 실측입니다. Actor 방식 대조군을 같은 메시·같은 스케일·같은 머티리얼로 만들어서 동일 5,000발을 재봤습니다. **게임 스레드 4.03 vs 16.99ms, 드로우콜 96 vs 3,358.** 특히 스케일 곡선이 결정적인데, 탄이 50배 늘 때 Mass GT는 1.3ms 오르고 Actor는 14.2ms 오릅니다.
>
> 구조는 프로세서 3개입니다. **이동은 워커 스레드**(5,000발에 0.04ms), **렌더와 판정은 게임 스레드**입니다 — ISM 트랜스폼 갱신과 `TakeDamage`가 게임 스레드 전용이기 때문입니다. **그 제약이 곧 남은 병목의 위치를 정합니다.**
>
> 네트워크는 복제하지 않습니다. **총알이 아니라 총알 만드는 법을 보냅니다** — 패턴·원점·각·개수·서버시각 다섯 개를 Multicast로 보내면 양쪽이 같은 순수 함수를 실행합니다. 지연은 `ServerTime`으로 보정하는데, 등속 직선이라 `위치 += 속도×경과`가 오차 없이 맞습니다.
>
> 그런데 이 프로젝트에서 제일 배운 건 성능이 아니라 **측정을 의심하는 태도**였습니다. GPU 병목이라고 잰 값이 알고 보니 엔진 부팅 구간을 잰 거였고, 그걸 근거로 잡혀 있던 Niagara 전면 교체를 **코드 한 줄 안 고치고 취소**했습니다. 잡은 방법은 **분해가 합과 안 맞는다**는 겁니다 — 프로세서 총합이 0.88ms인데 프레임이 39.5ms였거든요."

---

## 마지막 조언

**이 문서에서 딱 3개만 확실히 하고 가면 된다:**

1. **§2 용어 8개** (Entity/Fragment/Tag/Archetype/Chunk/Processor/Query/ForEachEntityChunk)
2. **§14 숫자 치트시트** (특히 4.03 vs 16.99, 96 vs 3,358, 45,000, 16.6ms)
3. **"ISM이 씬 컴포넌트라 GT 전용이고, 그 제약이 남은 병목의 위치를 정한다"** 한 문장

**그리고 면접에서 가장 강한 카드는 성능 숫자가 아니라 이거다:**

> **"최적화 이슈를 최적화 없이 닫았습니다. 측정해보니 병목이 없었거든요."**

