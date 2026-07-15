# Actor 기반 탄환 베이스라인 (Mass vs Actor 비교군) — 설계

이슈: #45 (M3: 스케일 업 + 프로파일링)
브랜치: `feature/M3-actor-baseline`
작성일: 2026-07-15

## 목적

M3 마일스톤은 "CPU 시간 수치 기록 (Mass vs Actor 비교)"이다. 비교군인 Actor 기반 탄환이 코드에 없다.
**최적화하지 않은, 정직하게 순진한 액터 구현**을 만들어 Mass 경로와 나란히 세운다. 최적화하면 비교가 무의미해진다.

## 기준선: Mass 경로 실측 파라미터

| 항목 | 값 | 출처 |
|---|---|---|
| 발사당 탄 수 | 16 | `Mass/REBulletPatternGenerator.h` `FSpiralParams::Count` |
| 각 간격 | 22.5° | 동 (360/16 균등 링) |
| 속도 | 300 uu/s | 동 `Speed` |
| 수명 | 3 s | 동 `Lifetime` |
| 발사 주기 | 0.1 s | `Core/REGameMode.cpp:67` `DemoFireTimer` (#17 데모) |
| Spiral 회전 | 호출당 +15° | `Core/REBossCharacter.h:58` `SpiralRotationStepDeg` |
| 스폰 원점 | (0, 0, 90) — Boss | `Core/REGameMode.cpp:53` |
| 메시 / 스케일 | `/Engine/BasicShapes/Sphere` / 0.5 | `Mass/REBulletRenderProcessor.cpp:14` |
| 머티리얼 | `BasicShapeMaterial` + MID Color=Red | `Mass/REBulletRenderSubsystem.cpp:37-43` |
| 콜리전 | NoCollision | `Mass/REBulletRenderSubsystem.cpp:30` (#34) |

정상 상태 탄환 수 = 16 × (3 s / 0.1 s) = **480발**.

## 이슈 본문과 코드의 불일치 (브레인스토밍에서 확인)

이슈는 "Mass 쪽 `re.Bullets.Count`와 대칭"이라 썼지만 **그 CVar는 코드에 없다.** 콘솔 변수 자체가 0개다.
Mass 탄 수는 `FSpiralParams::Count = 16` 하드코딩이고 발사 주기는 GameMode 데모 타이머다.
→ `re.Bullets.Count`(= Mass off 스위치)는 #44 소유 영역이다. 이 이슈에서는 만들지 않는다.

## 결정 사항

### D1. `re.ActorBullets.Count` = **동시 유지 목표 탄환 수** (발사당 탄 수 아님)

완료 조건 "Count 100 → 액터 탄환 100발 ±10% 안정 유지"와 일치시킨다.
발사당 탄 수는 스포너가 역산한다:

```
PerShotFloat = Count / (Lifetime / Interval) = Count / 30
Accum += PerShotFloat;  N = floor(Accum);  Accum -= N;   // float 누산 → 반올림 오차 누적 방지
```

Count = 480 을 넣으면 Mass 기본값과 동일한 부하가 된다.

### D2. 스폰 원점 = `FVector(0, 0, 90)` 하드코딩

Boss는 GameMode가 (0,0,90)에 스폰하고 움직이지 않는다. 상수로 박아도 시각적으로 동일하다.
`Core/REBossCharacter.h` 의존을 만들지 않는다 — #43 세션이 `Core/`를 뜯는 중이라 빌드 충돌을 피한다.

### D3. Mass 배타 실행은 이 이슈 범위 밖

Mass 데모 타이머는 `REGameMode::BeginPlay`에서 무조건 돈다. 끄려면 GameMode를 수정해야 하는데
그 파일은 #43 소유이고 수정 금지다. 런타임에 `ClearAllTimersForObject(GameMode)`로 몰래 끄는 방법도
있으나 숨은 결합이라 채택하지 않는다.

→ Mass off 스위치(`re.Bullets.Count 0`)는 #44가 만든다. 실제 수치 비교(#46)는 그 뒤에 한다.
이 이슈의 완료 조건은 Mass가 함께 돌아도 전부 검증 가능하다 (액터 카운트는 별개 관측).

### D4. 스포너 구동 = `UWorldSubsystem` + 0.1s 반복 타이머

`REBulletRenderSubsystem`이 `OnWorldBeginPlay`로 자립하는 패턴과 동일. GameMode에 의존하지 않는다.
`UTickableWorldSubsystem` 대신 타이머를 쓰는 이유:
- `GetStatId()` / `IsTickable()` 보일러플레이트 없음
- Mass 데모도 0.1s 타이머다 → 발사 메커니즘까지 대칭이라 비교가 더 공정
- Tick accumulator 방식은 저 fps(5000발 시나리오)에서 한 프레임에 여러 발 몰아 쏴 발사 주기를 왜곡한다

### D5. 머티리얼 MID는 스포너가 1개 만들어 전 탄환이 공유

액터마다 `CreateDynamicMaterialInstance`를 부르면 5000개 MID가 생긴다. 콜리전과 같은 부류의
**불공정한 추가 비용**이라 비교를 오염시킨다. 스포너가 `UMaterialInstanceDynamic::Create(Base, this)`로
1개 만들고 각 탄환 메시에 `SetMaterial(0, SharedMID)`. 시각은 Mass와 동일(빨강), 비용은 ~0.

## 구성

### `Source/Project_RE/Baseline/REBulletActor.h` / `.cpp`

```
AREBulletActor : public AActor
    UStaticMeshComponent* Mesh;   // Root. Sphere, Scale 0.5, NoCollision
    FVector Velocity;
    float   Lifetime;

    ctor:  PrimaryActorTick.bCanEverTick = true;   // 액터 경로의 본질적 비용 — 이게 정상이다
    Init(FVector V, float L, UMaterialInterface* M);
    Tick(dt):
        AddActorWorldOffset(Velocity * dt);
        Lifetime -= dt;
        if (Lifetime <= 0.f) Destroy();
```

Mass의 `REBulletSimProcessor`(이동·수명) + `REBulletRenderProcessor`(인스턴스 1개)를 액터 하나로 접은 것.
동작 대응은 1:1이다.

### `Source/Project_RE/Baseline/REActorBulletSpawner.h` / `.cpp`

```
UREActorBulletSpawner : public UWorldSubsystem

    static TAutoConsoleVariable<int32> CVarActorBulletCount;   // "re.ActorBullets.Count", 기본 0

    OnWorldBeginPlay(UWorld& W):
        if (W.GetNetMode() == NM_DedicatedServer || !W.IsGameWorld()) return;
        SharedMID = UMaterialInstanceDynamic::Create(BasicShapeMaterial, this);
        SharedMID->SetVectorParameterValue("Color", FLinearColor::Red);
        W.GetTimerManager().SetTimer(FireTimer, this, &Fire, 0.1f, /*bLoop=*/true);

    Fire():
        Target = CVarActorBulletCount.GetValueOnGameThread();
        if (Target <= 0) return;                       // 0 = 비활성. 평상시 게임 영향 0.

        Accum += Target / 30.f;                        // 30 = Lifetime(3s) / Interval(0.1s)
        N = FMath::FloorToInt(Accum);
        Accum -= N;
        if (N <= 0) return;

        FSpiralParams SP;                              // Speed 300 / Lifetime 3 = 기본값 = Mass와 동일
        SP.Count        = N;
        SP.AngleStepDeg = 360.f / N;                   // 균등 링
        SP.BaseAngleDeg = BaseAngleDeg;

        for (const FBulletSpawnParams& P : REBulletPattern::GenerateSpiral(FVector(0,0,90), SP))
            SpawnActor<AREBulletActor>(...)->Init(P.Velocity, P.Lifetime, SharedMID);

        BaseAngleDeg += 15.f;                          // Boss의 SpiralRotationStepDeg와 동일

        if ((FireCount++ % 10) == 0)                   // 1초에 1회 — 로그가 측정을 오염시키지 않게
            UE_LOG(... "[RE] ActorBulletProbe: live=%d target=%d", <TActorIterator 카운트>, Target);
```

발사 수학은 `Mass/REBulletPatternGenerator.h`의 `GenerateSpiral`을 **재사용**한다 (순수 함수, Mass 의존 없음).
패턴 코드를 복붙하지 않는다. 이 헤더는 읽기 전용 참조다.

### `Source/Project_RE/Project_RE.Build.cs`

`PublicIncludePaths`에 `"Project_RE/Baseline"` 한 줄 추가. 이 파일은 #45만 건드린다.

## 데이터 흐름

```
re.ActorBullets.Count (CVar)
      │
      ▼
UREActorBulletSpawner::Fire()  ← 0.1s 반복 타이머 (자립, GameMode 무관)
      │  Count → PerShot 역산 (float 누산)
      ▼
REBulletPattern::GenerateSpiral()  ← 순수 함수, Mass와 공유
      │  TArray<FBulletSpawnParams>
      ▼
World->SpawnActor<AREBulletActor>() × N
      │
      ▼
각 AREBulletActor::Tick()  → 이동, 수명 소진 시 self-Destroy
```

## 에러 처리

- 게임 월드 아님 / 데디서버 → `OnWorldBeginPlay`에서 즉시 return. 타이머도 안 걸린다.
- `Count <= 0` → `Fire()` 즉시 return. 스폰 0, 로그 0.
- 메시/머티리얼 로드 실패 → 로그 경고 후 계속 (Mass `REBulletRenderSubsystem`과 동일한 관대함).
- 액터 스폰은 `SpawnCollisionHandlingOverride = AlwaysSpawn` — 원점 겹침으로 스폰 실패하지 않게.

## 검증

| 완료 조건 | 검증 방법 |
|---|---|
| `Count 0` → 액터 0, 기존 게임 무변화 | headless 프로브: `ActorBulletProbe` 로그가 아예 안 뜸 |
| `Count 100` → 100발 ±10% 유지 | headless 프로브: `live=` 값이 ~100 수렴 (수명 3s 지난 뒤) |
| Mass와 동일 속도·수명·시각 | 실 RHI 스크린샷 (`-windowed` + `FScreenshotRequest`) — 빨간 구체 나선 |
| `Count 1000` / `5000` → 안 죽음 | headless 실행, 크래시 없이 종료 (느린 건 정상 — 그게 결론) |

## 범위 밖 (의도적)

- **액터 경로 최적화** — 오브젝트 풀링, Tick 그룹 튜닝, HISM 전환. 최적화하면 비교가 무의미해진다.
- 액터 탄환 피격 판정 — 비교 대상은 스폰/이동/렌더 비용.
- 액터 경로 네트워크 복제 — 싱글 측정 전용.
- 실제 수치 수집·비교표 — #46.
- Mass off 스위치 (`re.Bullets.Count`) — #44.
