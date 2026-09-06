# 구현 목표: M3.5 ① — 좌클릭 방향 공격 (자동사격 대체)

## 컨텍스트
UE 5.8 C++ 탑뷰 탄막(bullet-hell) 프로젝트 `Project_RE`. M3(프로파일링)까지 완료, M4 데디 서버 전 중간점검 3건 중 첫 번째.
이 goal: 자동사격(`UREAutoFireComponent` — 서버 타이머, 최근접 보스 자동조준)을 제거하고 로스트아크 평타 스타일 좌클릭 방향 공격으로 교체한다.
스코프 밖 후속 이슈(손대지 말 것): M3.5 ② 애니메이션(발사/대쉬 모션, 보스 메시), M3.5 ③ 스탯 DeveloperSettings 이관.
설계 스펙: docs/superpowers/specs/2026-07-17-leftclick-attack-design.md
상세 플랜: docs/superpowers/plans/2026-07-17-leftclick-attack.md
(참고 가능. 단 아래 코드가 최종 정본.)

## 브랜치
`dev`에서 분기: `feature/M3.5-leftclick-attack`

## 전역 제약
- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 자동 테스트 인프라 없음 → 검증 게이트 = **에디터 빌드 성공(에러 0)** + **headless 런타임 프로브 로그 관측**.
- 스탯 이번 이슈 하드코딩 유지 (③에서 Settings 이관): `Damage = 10.f`, `AttackInterval = 0.25f`, `AttackRange = 2000.f`.
- 홀드 연사 + 발사 순간 정지 + 커서 방향 회전 (스펙 확정). 판정 = 히트스캔. GAS 아님(plain RPC — 스펙 합의).
- 한글 주석 스타일 유지. uasset 없이 코드 정의 (프로젝트 규약).
- 커밋: Conventional Commits, 태스크당 1커밋. 푸터 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Git Bash에서 UE 실행 시 `MSYS_NO_PATHCONV=1` 필수.
- 빌드 명령:
  ```
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
  기대: `Result: Succeeded`, 에러 0. (에디터 열려 있으면 종료 후 실행 — 파일락 회피.)
- GitHub 이슈 미생성 상태 — 커밋 제목에 `(#N)` 생략. 이슈가 생기면 미러링.

## 검증된 API (실물 확인됨)
- `AREPlayerController::GetHitResultUnderCursor(ECC_Visibility, false, Hit)` — 기존 OnClickMove/OnDash가 동일 사용 (`REPlayerController.cpp`).
- `Server_Dash(FVector)` UFUNCTION(Server, Reliable) 패턴 — 동일 파일에 실물, `_Implementation` 접미 구현.
- `AController::StopMovement()` — 패스팔로잉(SimpleMoveToLocation) 중단. `GameFramework/Controller.h`.
- `UPawnMovementComponent::StopMovementImmediately()` — `GameFramework/PawnMovementComponent.h`.
- `LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params)` + `SCENE_QUERY_STAT(...)` — 구 `REAutoFireComponent::Fire()`에 실물.
- `TActorIterator<AREBossCharacter>` — `EngineUtils.h`, 구 Fire()에 실물.
- `AREBossCharacter::TakeDamage(...)` override 존재 — 서버 권위 HP 차감 (`REBossCharacter.cpp`).
- `FApp::IsUnattended()` — headless 프로브 가드, `BeginPlay`에 실물.
- Enhanced Input transient 생성: `NewObject<UInputAction>` + `TopDownMappingContext->MapKey(Action, EKeys::...)` + `EIC->BindAction(...)` — `SetupInputComponent`에 실물. Boolean IA + 트리거 없음 = 홀드 동안 매 프레임 `ETriggerEvent::Triggered`.

## 기존 파일 현황 (변경 대상)
- `Source/Project_RE/Core/REAutoFireComponent.h/.cpp`: `UREAutoFireComponent` — BeginPlay 서버 타이머(0.25s) → `Fire()`(최근접 보스 탐색+트레이스+TakeDamage), `StopFiring()`. 이 파일 쌍을 리네임+개조한다.
- `Source/Project_RE/Core/REPlayerController.h/.cpp`: `ClickMoveAction`(RMB)/`DashAction`(Space) transient IA, `Server_RequestMove`/`Server_Dash` RPC, `RunHeadlessMoveProbe`(t=1.0s, +Y 500)/`RunHeadlessDashProbe`(t=2.0s, +X, 4.1s에 `RequestExit`) — BeginPlay에서 `HasAuthority() && FApp::IsUnattended()`일 때 발동.
- `Source/Project_RE/Core/RECharacterBase.h/.cpp`: `UREAutoFireComponent* AutoFireComponent` 멤버 + 생성자 `CreateDefaultSubobject<UREAutoFireComponent>(TEXT("AutoFire"))`.
- `Source/Project_RE/Core/REGameMode.h/.cpp`: `EndGame(bool)`에서 `AutoFire->StopFiring()` 호출 블록 존재. `bGameOver`는 private, 액세서 없음. 보스는 BeginPlay에서 (600,0,90) 스폰.

================================================================
## TASK 1: REAutoFireComponent → REAttackComponent 리네임 + 개조
================================================================

### 1-1. 리네임 (git mv)

```bash
git mv Source/Project_RE/Core/REAutoFireComponent.h Source/Project_RE/Core/REAttackComponent.h
git mv Source/Project_RE/Core/REAutoFireComponent.cpp Source/Project_RE/Core/REAttackComponent.cpp
```

### 1-2. Source/Project_RE/Core/REAttackComponent.h (전체 교체)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "REAttackComponent.generated.h"

/**
 *  플레이어 수동공격 컴포넌트 (M3.5 ①, 구 REAutoFireComponent #26).
 *  서버에서 FireInDirection(Dir) 호출 → Dir 방향 히트스캔 1발 → 보스 히트 시 TakeDamage.
 *  조준은 클라(커서 방향), 판정·데미지는 서버 — 방향만 RPC로 받는다(REPlayerController).
 *  rate limit(AttackInterval)은 서버가 재검증 — 클라 스팸/치팅 방어.
 */
UCLASS()
class UREAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UREAttackComponent();

	/**
	 *  서버: Dir(수평 단위벡터) 방향 히트스캔 1발.
	 *  rate limit 통과 시 발사하고 true, 간격 미달이면 발사 없이 false.
	 */
	bool FireInDirection(const FVector& Dir);

	/** 발사 간격 조회 — 컨트롤러가 클라 로컬 페이싱에 사용. */
	float GetAttackInterval() const { return AttackInterval; }

private:
	/** 발사당 데미지. (③ 스탯 이슈에서 Settings 이관 예정) */
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	float Damage = 10.f;

	/** 발사 간격(s). 서버 rate limit + 클라 페이싱 공용. */
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	float AttackInterval = 0.25f;

	/** 히트스캔 사거리(uu). */
	UPROPERTY(EditDefaultsOnly, Category = "Attack")
	float AttackRange = 2000.f;

	/** 서버 마지막 발사 시각(월드초). rate limit 기준. -1 = 미발사. */
	double LastFireTime = -1.0;
};
```

### 1-3. Source/Project_RE/Core/REAttackComponent.cpp (전체 교체)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REAttackComponent.h"
#include "REBossCharacter.h"
#include "Engine/DamageEvents.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"

UREAttackComponent::UREAttackComponent()
{
	// 호출 구동(Server RPC 경유) — 틱/타이머 불필요.
	PrimaryComponentTick.bCanEverTick = false;
}

bool UREAttackComponent::FireInDirection(const FVector& Dir)
{
	// 서버 전용 — 판정·데미지는 서버 권위 (호출자가 Server RPC지만 방어적 재가드).
	if (!GetOwner()->HasAuthority())
	{
		return false;
	}

	// rate limit — 클라 페이싱과 별개로 서버가 재검증. 0.9배: 프레임/네트워크 지터 허용 오차.
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastFireTime >= 0.0 && Now - LastFireTime < AttackInterval * 0.9)
	{
		UE_LOG(LogTemp, Log, TEXT("[Attack] rate-limited (dt=%.2f)"), Now - LastFireTime);
		return false;
	}
	LastFireTime = Now;

	// 총구 높이(Z+50)에서 Dir 방향으로 사거리만큼 수평 트레이스. 자기 자신 무시.
	const FVector Start = GetOwner()->GetActorLocation() + FVector(0.f, 0.f, 50.f);
	const FVector End = Start + Dir * AttackRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(REAttack), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	const bool bBlockingHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Pawn, Params);

	AREBossCharacter* Boss = bBlockingHit ? Cast<AREBossCharacter>(Hit.GetActor()) : nullptr;
	if (Boss)
	{
		// 서버 권위 데미지 — 보스 HP 차감은 AREBossCharacter::TakeDamage override가 수신.
		APawn* OwnerPawn = Cast<APawn>(GetOwner());
		AController* InstigatorController = OwnerPawn ? OwnerPawn->GetController() : nullptr;
		const float Applied = Boss->TakeDamage(Damage, FDamageEvent(), InstigatorController, GetOwner());
		UE_LOG(LogTemp, Log, TEXT("[Attack] hit boss, applied=%.1f"), Applied);
	}
	else
	{
		// 방향이 빗나감(0) 또는 다른 것에 막힘(1) — 유저 조준 실패는 정상 케이스.
		UE_LOG(LogTemp, Log, TEXT("[Attack] miss (blocked=%d)"), bBlockingHit ? 1 : 0);
	}

#if ENABLE_DRAW_DEBUG
	// 개발 확인용 트레이스 라인 — 히트=빨강, 미스=초록. Shipping 자동 제외.
	DrawDebugLine(GetWorld(), Start, bBlockingHit ? Hit.ImpactPoint : End,
		Boss ? FColor::Red : FColor::Green, false, 0.2f, 0, 1.f);
#endif

	return true;
}
```

(삭제되는 것: `BeginPlay`/`EndPlay`/`StopFiring`/`Fire`/`FireTimer`/`FireInterval`, `EngineUtils.h`·`TimerManager.h` include, 최근접 보스 `TActorIterator` 탐색.)

### 1-4. Source/Project_RE/Core/RECharacterBase.h (수정)

전방선언 `class UREAutoFireComponent;` → `class UREAttackComponent;`

`AutoFireComponent` UPROPERTY 블록을 다음으로 교체:
```cpp
	/** 수동공격 컴포넌트 (M3.5 ①). 서버에서 Server_RequestFire 경유로만 발사. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attack", meta = (AllowPrivateAccess = "true"))
	UREAttackComponent* AttackComponent;
```

### 1-5. Source/Project_RE/Core/RECharacterBase.cpp (수정)

include: `#include "REAutoFireComponent.h"` → `#include "REAttackComponent.h"`

생성자의 AutoFire 블록을 다음으로 교체:
```cpp
	// 수동공격 부착 — 발사는 컨트롤러 Server_RequestFire → FireInDirection 경유.
	AttackComponent = CreateDefaultSubobject<UREAttackComponent>(TEXT("Attack"));
```

### 1-6. Source/Project_RE/Core/REGameMode.h (수정)

`EndGame` 선언 아래 public에 추가:
```cpp
	/** 승패 확정 여부 — 게임오버 후 잔여 발사 RPC 무시용 (REPlayerController가 조회). */
	bool IsGameOver() const { return bGameOver; }
```

### 1-7. Source/Project_RE/Core/REGameMode.cpp (수정)

`#include "REAutoFireComponent.h"` 삭제.

`EndGame`에서 아래 블록 전체 삭제 (자동 타이머가 사라져 정지 대상 없음 — 잔여 RPC는 `Server_RequestFire`의 `IsGameOver` 가드가 무시):
```cpp
	// 2) 자동사격 중지. 서버 타이머 구동이라 입력 차단으로는 안 멈춘다.
	//    AutoFireComponent는 캐릭터의 protected 멤버 — accessor 추가 대신 컴포넌트 조회.
	if (ARECharacterBase* Player = PC ? Cast<ARECharacterBase>(PC->GetPawn()) : nullptr)
	{
		if (UREAutoFireComponent* AutoFire = Player->FindComponentByClass<UREAutoFireComponent>())
		{
			AutoFire->StopFiring();
		}
	}
```
그 위의 `APlayerController* PC = ...` 줄은 아래 결과화면 블록이 계속 사용 — 유지. 남은 주석 번호 `1)`/`3)`은 `1)`/`2)`로 갱신.

### 1-8. 빌드/검증 게이트

```bash
grep -rn "AutoFire" Source/Project_RE/ --include="*.h" --include="*.cpp"
```
기대: `REBulletRenderSubsystem.cpp`의 과거 이력 주석 1건만 (수정 금지 — Surgical). 그 외 0건.

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 1-9. 커밋

```bash
git add -A Source/Project_RE/Core/
git commit -m "feat(M3.5): REAutoFireComponent를 수동공격 REAttackComponent로 개조

자동 타이머·최근접 보스 탐색 삭제. FireInDirection(Dir) 서버 히트스캔 +
rate limit. GameMode StopFiring 의존 제거(IsGameOver 가드로 대체 예정).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 2: REPlayerController — LMB 입력 + Server_RequestFire + headless 프로브
================================================================

### 2-1. Source/Project_RE/Core/REPlayerController.h (수정)

`OnDash` 선언 아래 protected에 추가:
```cpp
	/** 좌클릭 핸들러: 홀드 연사 — 커서 방향을 로컬 페이싱 후 서버로 발사 요청 */
	void OnFire(const FInputActionValue& Value);

	/** 발사 요청 서버 RPC. 서버가 rate limit 재검증 후 정지·회전·히트스캔. */
	UFUNCTION(Server, Reliable)
	void Server_RequestFire(FVector Dir);
```

`DashAction` UPROPERTY 아래에 추가:
```cpp
	UPROPERTY()
	UInputAction* FireAction;
```

private 섹션에 추가:
```cpp
	/** 클라 발사 페이싱 — 마지막 발사 요청 시각(월드초). 홀드 시 Triggered가 매 프레임 오는 것 억제. */
	double LastFireRequestTime = -1.0;

	/** 헤드리스(-unattended) 발사 프로브. 서버 권위에서만 발동. */
	void RunHeadlessFireProbe();

	FTimerHandle ProbeFireTimer;
```

### 2-2. Source/Project_RE/Core/REPlayerController.cpp (수정)

include 블록에 추가:
```cpp
#include "Core/REAttackComponent.h"
#include "Core/REBossCharacter.h"
#include "Core/REGameMode.h"
#include "GameFramework/PawnMovementComponent.h"
#include "EngineUtils.h"
```

`SetupInputComponent`의 `DashAction` 매핑 블록 뒤에 추가:
```cpp
	// 좌클릭 공격 IA (코드생성, transient). Boolean+트리거 없음 = 홀드 동안 매 프레임 Triggered.
	FireAction = NewObject<UInputAction>(this, TEXT("IA_Fire"));
	FireAction->ValueType = EInputActionValueType::Boolean;
	TopDownMappingContext->MapKey(FireAction, EKeys::LeftMouseButton);
```

`BindAction` 블록에 추가:
```cpp
		EIC->BindAction(FireAction, ETriggerEvent::Triggered, this, &AREPlayerController::OnFire);
```

`BeginPlay`의 프로브 호출부를 다음으로 (RunHeadlessFireProbe 추가):
```cpp
		RunHeadlessMoveProbe();
		RunHeadlessFireProbe();
		RunHeadlessDashProbe();
```

`OnDash` 구현 뒤에 추가:
```cpp
void AREPlayerController::OnFire(const FInputActionValue& Value)
{
	// 홀드 연사 — Triggered가 매 프레임 오므로 로컬에서 AttackInterval 주기로 페이싱.
	// 서버도 rate limit을 재검증하므로 이 페이싱은 RPC 트래픽 절약용.
	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	UREAttackComponent* Attack = P->FindComponentByClass<UREAttackComponent>();
	if (!Attack)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (LastFireRequestTime >= 0.0 && Now - LastFireRequestTime < Attack->GetAttackInterval())
	{
		return;
	}

	// 커서 방향 계산은 로컬(커서/카메라는 로컬 전용) — 방향만 서버로 (OnDash 동일 패턴).
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit) || !Hit.bBlockingHit)
	{
		return;
	}
	const FVector Dir = (Hit.ImpactPoint - P->GetActorLocation()).GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		return;
	}
	LastFireRequestTime = Now;
	Server_RequestFire(Dir);
}
```

`Server_Dash_Implementation` 뒤에 추가:
```cpp
void AREPlayerController::Server_RequestFire_Implementation(FVector Dir)
{
	// 게임오버 후 잔여 RPC 무시 — 구 AutoFire StopFiring의 대체.
	AREGameMode* GM = GetWorld()->GetAuthGameMode<AREGameMode>();
	if (GM && GM->IsGameOver())
	{
		return;
	}

	APawn* P = GetPawn();
	if (!P)
	{
		return;
	}
	// 클라 입력 신뢰 금지 — 서버에서 재정규화.
	const FVector Dir2D = Dir.GetSafeNormal2D();
	if (Dir2D.IsNearlyZero())
	{
		return;
	}
	UREAttackComponent* Attack = P->FindComponentByClass<UREAttackComponent>();
	if (!Attack)
	{
		return;
	}

	if (Attack->FireInDirection(Dir2D))
	{
		// 발사 성공 시에만 정지+회전 — rate limit에 걸린 스팸 RPC가 이동을 끊지 못하게.
		StopMovement();                                        // 우클릭 이동 패스팔로잉 중단
		if (UPawnMovementComponent* Move = P->GetMovementComponent())
		{
			Move->StopMovementImmediately();                   // 잔여 속도 제거 (로아 평타 정지)
		}
		P->SetActorRotation(FRotator(0.f, Dir2D.Rotation().Yaw, 0.f));  // 커서 방향 회전
	}
}
```

`RunHeadlessMoveProbe` 구현 뒤에 추가:
```cpp
void AREPlayerController::RunHeadlessFireProbe()
{
	// t=1.5s: 보스 방향 발사 1회(hit 기대) + 즉시 재발사(rate limit 차단 기대).
	// 이동 프로브(1.0s, +Y 이동)와 대쉬 프로브(2.0s, +X) 사이 — 서로 간섭 없음.
	// 발사 성공 시 StopMovement가 이동 프로브를 끊지만 dist 로그는 계속 나옴(수렴만 중단) — 게이트 아님.
	FTimerDelegate FireDel = FTimerDelegate::CreateLambda([this]()
	{
		APawn* P = GetPawn();
		if (!P)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Attack] probe: no pawn"));
			return;
		}
		AREBossCharacter* Boss = nullptr;
		for (TActorIterator<AREBossCharacter> It(GetWorld()); It; ++It)
		{
			Boss = *It;
			break;
		}
		if (!Boss)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Attack] probe: no boss"));
			return;
		}
		const FVector Dir = (Boss->GetActorLocation() - P->GetActorLocation()).GetSafeNormal2D();
		UE_LOG(LogTemp, Log, TEXT("[Attack] probe fire dir=%s"), *Dir.ToString());
		Server_RequestFire(Dir);   // 1발 — hit boss 기대
		Server_RequestFire(Dir);   // 즉시 재발사 — rate-limited 기대
	});
	GetWorld()->GetTimerManager().SetTimer(ProbeFireTimer, FireDel, 1.5f, false);
}
```

### 2-3. 빌드/검증 게이트

```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
```
기대: `Result: Succeeded`, 에러 0.

### 2-4. 커밋

```bash
git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp
git commit -m "feat(M3.5): 좌클릭 홀드 연사 입력 + Server_RequestFire RPC

LMB 홀드 → 클라 페이싱 → 방향 RPC → 서버 rate limit·정지·회전·히트스캔.
게임오버 후 RPC는 IsGameOver 가드로 무시. headless 발사 프로브 포함.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

================================================================
## TASK 3: Headless 프로브 게이트 (신규 코드 없음)
================================================================

### 3-1. headless 실행 + 로그 관측

```bash
MSYS_NO_PATHCONV=1 timeout 30 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[Attack\]|\[AutoFire\]"
```
기대 로그:
```
[Attack] probe fire dir=X=... Y=... Z=0.000
[Attack] hit boss, applied=10.0
[Attack] rate-limited (dt=0.00)
```
판정:
- `hit boss, applied=10.0` 1회 + `rate-limited` 1회 → **게이트 통과.**
- `[AutoFire]` 로그 1건이라도 관측 → 리네임 누락. TASK 1 재확인.
- `miss (blocked=0)` → 방향/높이 문제. 트레이스 Z 오프셋(50)·보스 위치 확인 후 수정 커밋(`fix(M3.5): ...`).
- `[Attack]` 로그 전무 → 프로브 타이머/BeginPlay 배선 재확인.

### 3-2. 기존 프로브 회귀 확인

```bash
MSYS_NO_PATHCONV=1 timeout 30 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" /Game/Level/Main -game -unattended -nullrhi -stdout -AllowStdOutLogVerbosity 2>&1 | grep -E "\[Move\]|\[Dash\]"
```
기대: `[Move] probe start` + dist 로그, `[Dash] activate ok=1` / `re-activate ok=1` (기존과 동일). 발사 프로브의 StopMovement로 Move dist가 목표에 수렴 안 할 수 있음 — dist 로그 존재만 확인, 수렴은 게이트 아님.

### 3-3. 커밋

수정 없었으면 커밋 없음 — TASK 3은 관측 게이트.

## 완료 후
- 최종 확인: `git diff dev --stat` — `Core/` 5개 파일 쌍(REAttackComponent.h/.cpp 리네임 포함) 외 변경 없음 (Mass/·Abilities/·UI/ 미접촉).
- PR: base=`dev`, 제목 `feat(M3.5): 좌클릭 방향 공격 (자동사격 대체)`. 이슈를 만들었으면 메타(label, milestone, assignee, project) 미러링. 본문: 목적/변경/검증(프로브 로그)/스코프 경계(애니메이션 ②·스탯 ③ 별도 이슈). 푸터 `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- 남은 의도된 TODO: 없음 (M4 Multicast TODO는 ② 몫).

## 하지 말 것 (스코프 밖)
- 발사/대쉬 애니메이션, 보스 메시 — M3.5 ②.
- 스탯 Settings/ini 이관 — M3.5 ③ (이번엔 UPROPERTY 하드코딩 유지).
- GAS 어빌리티화, 클라 예측, Multicast — M4 이후.
- `REBulletRenderSubsystem.cpp`의 AutoFire 언급 주석 수정 금지 (과거 이력).
- 기존 `[Move]`/`[Dash]` 프로브 로직 변경 금지 (BeginPlay 호출 1줄 추가만).
