# M2 #27 보스 탄막 피격 판정 (Mass↔플레이어) Design

**이슈:** #27 ([M2] 보스 탄막 피격 판정 (Mass↔플레이어))
**날짜:** 2026-07-13
**선행:** M1 탄막 파이프라인(#14~#17) 완료 — 탄환은 `FTransformFragment` + `FBulletSimFragment` + `FBulletTag` 엔티티, `UREBulletSimProcessor`가 이동/수명, `UREBulletRenderProcessor`가 ISM 렌더.

## 목표

Mass 탄환이 플레이어 반경에 들어오면 서버에서 `ARECharacterBase::TakeDamage`로 Health 차감 + 해당 엔티티 파괴. 탄환은 액터가 아니므로(ISM 인스턴스) 물리 오버랩 불가 — 거리 비교로 판정.

## 배경 (현재 상태)

- `ARECharacterBase::TakeDamage` (RECharacterBase.cpp:73) — `HasAuthority()` 가드 내장, `Health = Clamp(Health - Applied, 0, MaxHealth)`, Health는 Replicated.
- `UREBulletSimProcessor` — `AllNetModes`, 매 틱 위치 전진 + 수명 파괴.
- `UREBulletRenderProcessor` — 매 프레임 live 탄환 엔티티 전체를 다시 수집해 ISM 재구성 → 엔티티 파괴만 하면 렌더 제거 자동.
- 탄환 시각 반경 ~25cm (`BulletScale = 0.5` × 엔진 Sphere 반경 50cm).

## 구현 형태 (결정)

**신규 Mass Processor** `UREBulletHitProcessor` — 기존 Sim/Render 프로세서 패턴과 일치. 서브시스템 Tick 대안은 프로세서 페이즈 밖이라 실행 순서 제어 불가로 기각.

## 변경 범위

**파일 2개 신규:** `Source/Project_RE/Mass/REBulletHitProcessor.h` / `.cpp`. 기존 파일 변경 없음.

### 1. 생성자

```cpp
UREBulletHitProcessor::UREBulletHitProcessor()
	: EntityQuery(*this)
{
	// 서버 권위 판정만 — 싱글(Standalone) + 데디서버(Server). 클라 실행 없음.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);

	// TakeDamage는 액터 호출 — 게임 스레드 전용.
	bRequiresGameThreadExecution = true;

	// 이동 후 판정 — Sim이 위치를 전진시킨 뒤 같은 프레임에 히트 체크.
	ExecutionOrder.ExecuteAfter.Add(UREBulletSimProcessor::StaticClass()->GetFName());
}
```

### 2. ConfigureQueries

```cpp
EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
EntityQuery.AddTagRequirement<FBulletTag>(EMassFragmentPresence::All);
```

위치만 읽으면 됨 — `FBulletSimFragment` 불요.

### 3. Execute

```cpp
namespace
{
	/** 히트 반경(cm) — 탄환 시각 반경 25 + 플레이어 캡슐 반경 ~35. */
	constexpr float HitRadius = 60.f;
	/** 탄환 1발 데미지 — 100 HP 기준 10발 사망. */
	constexpr float BulletDamage = 10.f;
}

void UREBulletHitProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;
	ARECharacterBase* Player = Cast<ARECharacterBase>(Pawn);
	if (!Player)
	{
		return;  // 플레이어 없으면 no-op (레벨 전환 등)
	}

	const FVector PlayerLoc = Player->GetActorLocation();

	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> Transforms = Ctx.GetFragmentView<FTransformFragment>();

		for (int32 i = 0; i < Num; ++i)
		{
			// 탑다운 — XY 평면 거리만 비교 (탄환 Z와 캡슐 중심 Z 불일치 함정 회피)
			if (FVector::DistSquaredXY(Transforms[i].GetTransform().GetLocation(), PlayerLoc)
				<= HitRadius * HitRadius)
			{
				Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
				Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
			}
		}
	});
}
```

**로직:**
- 플레이어 위치 1회 캐시 → 탄환별 `DistSquaredXY` vs `HitRadius²` (sqrt 회피).
- 히트: `TakeDamage` 호출 + 지연 파괴. 파괴된 탄환은 다음 프레임 Render 재구성에서 자동 제거.
- 같은 프레임 다중 탄환 히트 = 다중 데미지 — 의도된 동작 (i-frame은 스코프 밖).
- `EventInstigator`/`DamageCauser` = nullptr — 탄환은 액터 아님, 보스 컨트롤러 귀속은 현재 불요.

## 설계 결정

- **`Standalone | Server`** — 판정/데미지는 서버 권위. `TakeDamage` 내부 `HasAuthority()` 가드가 이중 안전망. 클라는 복제된 Health만 소비.
- **XY 평면 거리** — 탑다운 탄막. 3D 구체 판정은 탄환 스폰 Z(보스 기준)와 플레이어 캡슐 중심 Z 차이로 히트 누락 위험.
- **`ExecuteAfter(SimProcessor)`** — 미지정 시 같은 페이즈 내 순서 비보장 → 1프레임 지연 판정 가능. `StaticClass()->GetFName()`으로 이름 오타 원천 차단 (REBulletSimProcessor.h include 필요).
- **단일 플레이어(`GetPlayerPawn(0)`)** — M2 산출물은 싱글 게임루프. 멀티 순회는 YAGNI.
- **O(N) 전수 거리비교** — 플레이어 1명 × 탄환 수천 발 = 트리비얼. 공간 분할 그리드는 YAGNI.
- **상수 익명 namespace** — Render 프로세서 `BulletScale` 스타일 일치.

## 검증 (완료 기준)

이슈 완료 기준: 탄환이 플레이어 반경 진입 시 서버에서 Health 감소 + 탄환 제거. PIE에서 회피/피격 동작 확인.

1. **빌드 통과.**
2. **PIE 피격:** 탄막에 접촉 → Health 감소 (로그 또는 디테일 패널) + 해당 탄환 시각적으로 소멸.
3. **PIE 회피:** 우클릭 이동으로 탄막 회피 → Health 무변화. (대쉬 #25는 dev 미머지 — 검증에 사용 불가.)
4. (선택) Headless 프로브 `[[headless-runtime-probe]]`: 플레이어 위치로 탄환 스폰 → N틱 후 Health < 100 + Alive 감소 로그.

## 스코프 밖 (YAGNI)

- 사망 처리 / 피격 이펙트 → M5 (TakeDamage 내 TODO 주석 존재)
- HP바 위젯 → M2 별도 이슈
- i-frame(피격 무적) / 대쉬 무적 → 미요청
- 멀티 플레이어 순회 → 후속 마일스톤
- 공간 분할(그리드/쿼드트리) → 프로파일링에서 필요 시
- 보스 귀속 데미지(EventInstigator) → 킬크레딧 필요해질 때
