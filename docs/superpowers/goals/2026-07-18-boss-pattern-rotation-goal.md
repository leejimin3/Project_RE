# 구현 목표: M5 #64 — 보스 탄막 패턴 로테이션

## 컨텍스트

UE5.8 C++ 탑다운 불릿헬. Mass Entity 기반 대량 탄막. 이슈 **#64** (마일스톤 M5: 협동 멀티 + 시드 탄막 + 서버권위 피격).

이 goal이 하는 것: 보스가 Spiral 단일 패턴만 반복하던 것을 **랜덤 패턴(Spiral/Fan) 페이즈 로테이션**으로 바꾼다. 발사 주체를 GameMode 타이머에서 `AREBossCharacter` 내부 페이즈 상태머신으로 이관(M5 RPC 확장 대비), `FRandomStream(Seed)` 시드 결정성, Fan 플레이어 조준, Spiral 회전스텝 조정(직선 방사 → 나선).

스코프 밖(손대지 말 것): Homing 구현, 패턴 파라미터 Settings(ini) 노출, M5 RPC 실배선, 패턴 3종+ 추가.

설계 스펙: `docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md`
상세 플랜: `docs/superpowers/plans/2026-07-18-boss-pattern-rotation.md`
(참고 가능. 단 **아래 코드가 최종 정본.**)

## 브랜치

`feature/M5-pattern-rotation` 브랜치에서 진행 (이미 분기됨, spec+plan 커밋 존재). dev로 PR.

## 전역 제약

- **테스트 하네스 없음.** 검증은 ① `Build.bat Project_REEditor` 게이트, ② headless `-game -nullrhi` 로그 프로브, ③ 실RHI 창모드 스크린샷.
- **빌드 커맨드:** `"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex`
- **headless 프로브 (Git Bash):** `export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"` 필수. 안 하면 `/Game/...` 경로 깨져 엉뚱한 맵 로드.
- **Mass 프로세서 안 건드림** — 스폰 경로(`SpawnBulletBatch`)만 사용.
- **측정 하네스 보존** — `scripts/profile.ps1`은 `re.Profiling.KeepFiring 1` + `re.Bullets.Count N`으로 Spiral 클로즈드루프 연속 발사 전제. 회귀 금지.
- **Gitflow** — 커밋 메시지 끝: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

## 검증된 API (실물 확인됨)

- `void AREBossCharacter::TriggerBulletPattern(EBulletPattern Pattern, int32 Seed, float StartTime)` — 기존 발사 진입점. switch로 패턴별 스폰. Spiral case에 측정용 클로즈드루프(`re.Bullets.Count`) 분기 내장. **Spiral case 무변경.**
- `EBulletPattern::{Spiral, Fan, Homing}` — `REBulletPattern.h` enum.
- `REBulletPattern::FireIntervalSec()` — Settings(BossFireInterval=0.1s) 단일 출처.
- `REBulletPattern::FFanParams { int32 Count=16; float CenterAngleDeg=0; float SpreadDeg=90; float Speed; float Lifetime; }` + `REBulletPattern::GenerateFan(const FVector& Origin, const FFanParams&)`.
- `re.Profiling.KeepFiring` — `ECVF_Cheat` int CVar. 기존 `TakeDamage`/`EndGame`이 `IConsoleManager::Get().FindConsoleVariable(TEXT("re.Profiling.KeepFiring"))`로 조회.
- `REBossCharacter.cpp` 기존 include: `HAL/IConsoleManager.h` 있음(IConsoleVariable 가시). `TimerManager.h` **없음** → 추가 필요. `GameFramework/PlayerController.h` **없음** → Task 2에서 추가.
- `AREBossCharacter`는 `ACharacter` 상속 → `GetWorldTimerManager()` 사용 가능.

## 기존 파일 현황 (변경 대상)

- `REBossCharacter.h`: `public:` TriggerBulletPattern/TakeDamage/GetLifetimeReplicatedProps. `private:` `SpiralBaseAngleDeg`, `SpiralRotationStepDeg=15.f`, `SpiralSpawnRate`, `SpiralSpawnAccum`, `SpiralShotCount`, `bIsDead=false`.
- `REBossCharacter.cpp`: `TriggerBulletPattern`에 Spiral(클로즈드루프)/Fan/Homing switch. Fan case는 `Center` 조준 없이 `FFanParams FP;` 기본만.
- `REGameMode.h`: `private:` `FTimerHandle DemoFireTimer;` + `#include "Engine/TimerHandle.h"`, `TObjectPtr<AREBossCharacter> DemoBoss`.
- `REGameMode.cpp`: BeginPlay 보스 스폰 블록에서 `TriggerBulletPattern(Spiral)` 2회 + `DemoFireTimer` 람다 루프. EndGame에서 `ClearTimer(DemoFireTimer)`. 하단에 `#16 프로브(SpiralProbe/FanProbe)` 순수함수 검증 블록(유지).

================================================================
## TASK 1: 보스 페이즈 스케줄러 + GameMode 발사 위임
================================================================

### 1-1. `Source/Project_RE/Core/REBossCharacter.h` (수정)

`public:` 블록, `TriggerBulletPattern` 선언 아래에 추가:

```cpp
	/** 페이즈 로테이션 발사 시작. Seed로 패턴 순서 결정(M5 시드 동기화 선행). */
	void StartFiring(int32 Seed);
	/** 발사 정지. 이미 뜬 탄은 수명까지 유지(일괄 소멸 안 함). */
	void StopFiring();
```

`private:` 블록, `bool bIsDead = false;` 아래에 추가:

```cpp
	//~ 패턴 로테이션 페이즈 스케줄러 (#64). 발사 주체 = Boss (M5 RPC 확장 대비).
	void BeginPhase();          // 다음 패턴 선택 + 발사 타이머 세팅 + 페이즈 종료 예약
	void FireCurrentPattern();  // 현재 페이즈 패턴 1회 발사 (FireTimer 콜백)
	void EndPhase();            // 발사 정지 + RestSec 뒤 BeginPhase 예약

	FRandomStream PhaseRng;
	EBulletPattern CurrentPhasePattern = EBulletPattern::Spiral;
	bool bFirstPhase = true;    // 첫 페이즈 Spiral 고정 (오프닝 + profiling 오염 창 차단)
	FTimerHandle FireTimer;     // 페이즈 내 발사 반복
	FTimerHandle PhaseTimer;    // 페이즈 종료/대기 전환

	static constexpr float SpiralPhaseSec     = 5.f;
	static constexpr float FanPhaseSec        = 3.f;
	static constexpr float RestSec            = 1.f;
	static constexpr float FanFireIntervalSec = 0.5f;
```

### 1-2. `Source/Project_RE/Core/REBossCharacter.cpp` (수정)

상단 include에 추가:

```cpp
#include "TimerManager.h"
```

`TriggerBulletPattern` 함수 **위**에 추가:

```cpp
void AREBossCharacter::StartFiring(int32 Seed)
{
	PhaseRng.Initialize(Seed);
	bFirstPhase = true;
	BeginPhase();   // 즉시 1회, 이후 타이머로 재진입
}

void AREBossCharacter::StopFiring()
{
	GetWorldTimerManager().ClearTimer(FireTimer);
	GetWorldTimerManager().ClearTimer(PhaseTimer);
}

void AREBossCharacter::BeginPhase()
{
	// KeepFiring 측정 모드: 로테이션/대기 우회, Spiral 연속 발사.
	// StartFiring 시점(BeginPlay)엔 ExecCmds가 아직 CVar를 안 세팅했을 수 있어
	// 여기(타이머 재진입 콜백)에서 매번 조회한다. 첫 페이즈 Spiral 고정이
	// profiling 시작 오염 창을 닫는다.
	static IConsoleVariable* KeepFiring = IConsoleManager::Get().FindConsoleVariable(TEXT("re.Profiling.KeepFiring"));
	if (KeepFiring && KeepFiring->GetInt() != 0)
	{
		CurrentPhasePattern = EBulletPattern::Spiral;
		GetWorldTimerManager().SetTimer(FireTimer, this,
			&AREBossCharacter::FireCurrentPattern, REBulletPattern::FireIntervalSec(), /*bLoop=*/true);
		return;   // PhaseTimer 예약 안 함 → 페이즈 종료/대기 없음
	}

	if (bFirstPhase)
	{
		CurrentPhasePattern = EBulletPattern::Spiral;   // 오프닝 시그니처 + 오염 창 차단
		bFirstPhase = false;
	}
	else
	{
		CurrentPhasePattern = (PhaseRng.RandRange(0, 1) == 0)
			? EBulletPattern::Spiral : EBulletPattern::Fan;
	}

	const bool bSpiral = (CurrentPhasePattern == EBulletPattern::Spiral);
	const float PhaseSec  = bSpiral ? SpiralPhaseSec : FanPhaseSec;
	const float FireInterval = bSpiral ? REBulletPattern::FireIntervalSec() : FanFireIntervalSec;

	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: %s %.1fs"),
		bSpiral ? TEXT("Spiral") : TEXT("Fan"), PhaseSec);

	GetWorldTimerManager().SetTimer(FireTimer, this,
		&AREBossCharacter::FireCurrentPattern, FireInterval, /*bLoop=*/true);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::EndPhase, PhaseSec, /*bLoop=*/false);
}

void AREBossCharacter::FireCurrentPattern()
{
	TriggerBulletPattern(CurrentPhasePattern, /*Seed=*/12345, /*StartTime=*/0.f);
}

void AREBossCharacter::EndPhase()
{
	GetWorldTimerManager().ClearTimer(FireTimer);
	UE_LOG(LogTemp, Log, TEXT("[RE] Boss Phase: Rest %.1fs"), RestSec);
	GetWorldTimerManager().SetTimer(PhaseTimer, this,
		&AREBossCharacter::BeginPhase, RestSec, /*bLoop=*/false);
}
```

### 1-3. `Source/Project_RE/Core/REGameMode.h` (수정)

삭제:

```cpp
	/** 데모: 주기적 Spiral 발사로 지속 탄막(영상 소스). */
	FTimerHandle DemoFireTimer;
```

`#include "Engine/TimerHandle.h"`도 삭제 (이 헤더에서 `FTimerHandle` 다른 사용 없음). `DemoBoss` 멤버는 유지.

### 1-4. `Source/Project_RE/Core/REGameMode.cpp` (수정)

BeginPlay 보스 스폰 `if` 블록 본문 교체. 기존:

```cpp
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
		Boss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);  // #16 프로브: BaseAngle 누적 확인

		// #17 데모: 0.1초마다 Spiral 발사 → 회전 나선 탄막 지속(영상 소스 + ISM 카운트 추종 검증).
		DemoBoss = Boss;
		FTimerDelegate FireDel = FTimerDelegate::CreateLambda([this]()
		{
			if (DemoBoss)
			{
				DemoBoss->TriggerBulletPattern(EBulletPattern::Spiral, 12345, 0.f);
			}
		});
		GetWorld()->GetTimerManager().SetTimer(DemoFireTimer, FireDel, REBulletPattern::FireIntervalSec(), /*bLoop=*/true);
```

교체 후:

```cpp
		// #64: 발사 주체를 Boss로 이관 — 랜덤 패턴 페이즈 로테이션(M5 RPC 확장 대비).
		DemoBoss = Boss;
		Boss->StartFiring(/*Seed=*/12345);
```

EndGame 기존:

```cpp
	// 1) 탄막 발사 중지. 이미 뜬 탄환은 Lifetime 다할 때까지 계속 난다 (설계 합의 — 일괄 소멸 안 함).
	GetWorld()->GetTimerManager().ClearTimer(DemoFireTimer);
```

교체 후:

```cpp
	// 1) 탄막 발사 중지. 이미 뜬 탄환은 Lifetime 다할 때까지 계속 난다 (설계 합의 — 일괄 소멸 안 함).
	if (DemoBoss)
	{
		DemoBoss->StopFiring();
	}
```

`#16 프로브(SpiralProbe/FanProbe)` 순수함수 검증 블록(BeginPlay 하단)은 **유지**.

### 1-5. 빌드/검증 게이트

**빌드:**
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
기대: `Result: Succeeded`

**페이즈 전환 프로브 (Git Bash):**
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
timeout 90 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -nullrhi -unattended -nosound -nosplash -stdout -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\phase-probe.log" >/dev/null 2>&1
grep -aF "Boss Phase" "E:\UnrealProjects\Project_RE\Saved\Logs\phase-probe.log" | head -20
```
기대: 첫 줄 `Boss Phase: Spiral 5.0s`, 이후 `Boss Phase: Rest 1.0s` 최소 1회. (정지 플레이어가 ~2s에 DEFEAT→StopFiring으로 조기 종료될 수 있음 — 첫 Spiral+Rest 1회 이상이면 통과.)

**측정 하네스 회귀 (Git Bash):**
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
timeout 60 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -nullrhi -unattended -nosound -nosplash -stdout -ExecCmds="re.Profiling.KeepFiring 1,re.Bullets.Count 1000" -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\keepfire-probe.log" >/dev/null 2>&1
grep -aF "Boss Phase" "E:\UnrealProjects\Project_RE\Saved\Logs\keepfire-probe.log" | head -5
grep -aF "RenderProbe" "E:\UnrealProjects\Project_RE\Saved\Logs\keepfire-probe.log" | tail -5
```
기대: `Boss Phase: Fan` / `Rest` **미출현**(Spiral 우회만). `RenderProbe live=`가 목표 1000 방향 증가.

### 1-6. 커밋

```bash
git add Source/Project_RE/Core/REBossCharacter.h Source/Project_RE/Core/REBossCharacter.cpp Source/Project_RE/Core/REGameMode.h Source/Project_RE/Core/REGameMode.cpp
git commit -m "$(cat <<'EOF'
feat(M5): 보스 발사 주체 Boss 이관 + 랜덤 패턴 페이즈 로테이션 (#64)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

================================================================
## TASK 2: Fan 플레이어 조준
================================================================

### 2-1. `Source/Project_RE/Core/REBossCharacter.cpp` (수정)

상단 include에 추가:

```cpp
#include "GameFramework/PlayerController.h"
```

`TriggerBulletPattern`의 `case EBulletPattern::Fan:` 블록 교체. 기존:

```cpp
	case EBulletPattern::Fan:
	{
		REBulletPattern::FFanParams FP;
		Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Spread=%.1f -> N=%d"), FP.SpreadDeg, Params.Num());
		break;
	}
```

교체 후:

```cpp
	case EBulletPattern::Fan:
	{
		REBulletPattern::FFanParams FP;
		// 플레이어 방향 조준. 폰 없으면 0°(기존 기본) 폴백.
		// TODO M5: 멀티는 타깃 선택 필요 — 지금은 첫 플레이어 고정.
		if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APawn* Target = PC->GetPawn())
			{
				const FVector D = Target->GetActorLocation() - GetActorLocation();
				FP.CenterAngleDeg = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			}
		}
		Params = REBulletPattern::GenerateFan(GetActorLocation(), FP);
		UE_LOG(LogTemp, Log, TEXT("[RE] Boss Fan: Center=%.1f Spread=%.1f -> N=%d"),
			FP.CenterAngleDeg, FP.SpreadDeg, Params.Num());
		break;
	}
```

### 2-2. 빌드/검증 게이트

**빌드:**
```
"/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
기대: `Result: Succeeded`

**Fan 조준각 프로브 (Git Bash):**
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
timeout 90 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -nullrhi -unattended -nosound -nosplash -stdout -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\fan-probe.log" >/dev/null 2>&1
grep -aF "Boss Fan: Center" "E:\UnrealProjects\Project_RE\Saved\Logs\fan-probe.log" | head -5
```
기대: `Boss Fan: Center=180.0` 근처(보스 600,0 → 플레이어 원점 부근이면 ≈180°). **Fan 페이즈가 DEFEAT 전에 안 잡히면**: 임시로 `REBossCharacter.h`의 `SpiralPhaseSec=0.5f`로 낮춰 Fan을 앞당겨 관측 후 `5.f`로 원복(원복 후 재빌드). 그래도 안 잡히면 Task 3 실RHI 스크린샷에서 Fan 페이즈 캡처로 대체 확인.

### 2-3. 커밋

```bash
git add Source/Project_RE/Core/REBossCharacter.cpp
git commit -m "$(cat <<'EOF'
feat(M5): Fan 패턴 플레이어 조준 (#64)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

================================================================
## TASK 3: Spiral 회전 스텝 튜닝 (직선 방사 → 나선)
================================================================

`SpiralRotationStepDeg`(현재 15°, 링 간격 22.5°와 근사 정합 → 직선 방사)를 비정합 값으로 바꿔 나선을 만든다. 실RHI 스크린샷으로 후보 비교 후 확정.

### 3-1. 임시 스크린샷 프로브 삽입 — `Source/Project_RE/Core/REGameMode.cpp`

상단 include 추가 (검증 후 3-6에서 제거):

```cpp
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
```

BeginPlay `#16 프로브` 블록 **위**에 임시 삽입:

```cpp
	// TEMP-PROBE: 스파이럴 곡선감 스크린샷 (검증 후 제거)
	{
		FTimerHandle S1, S2, Q;
		GetWorld()->GetTimerManager().SetTimer(S1, FTimerDelegate::CreateLambda([]()
		{ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("SpiralTune3s.png"), false, false); }), 3.0f, false);
		GetWorld()->GetTimerManager().SetTimer(S2, FTimerDelegate::CreateLambda([]()
		{ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("SpiralTune5s.png"), false, false); }), 5.0f, false);
		GetWorld()->GetTimerManager().SetTimer(Q, FTimerDelegate::CreateLambda([this]()
		{ GEngine->Exec(GetWorld(), TEXT("quit")); }), 7.0f, false);
	}
```

### 3-2. 후보 A (황금각 137.5°) 빌드 + 스샷

`REBossCharacter.h`: `SpiralRotationStepDeg = 15.f;` → `= 137.5f;`

빌드 후 실RHI 창모드 실행 (Git Bash):
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
rm -f /e/UnrealProjects/Project_RE/Saved/SpiralTune3s.png /e/UnrealProjects/Project_RE/Saved/SpiralTune5s.png
timeout 300 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -windowed -ResX=1280 -ResY=720 -nosplash -nosound -stdout -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\spiral-tuneA.log" >/dev/null 2>&1
sleep 3
cp /e/UnrealProjects/Project_RE/Saved/SpiralTune5s.png /e/UnrealProjects/Project_RE/Saved/SpiralTuneA5s.png
ls -la /e/UnrealProjects/Project_RE/Saved/SpiralTuneA5s.png
```
Read 툴로 `SpiralTuneA5s.png` 확인.

### 3-3. 후보 B (소각 9.7°) 빌드 + 스샷

`REBossCharacter.h`: `SpiralRotationStepDeg = 9.7f;` 빌드 후:
```bash
export MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*"
rm -f /e/UnrealProjects/Project_RE/Saved/SpiralTune3s.png /e/UnrealProjects/Project_RE/Saved/SpiralTune5s.png
timeout 300 "/e/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "E:\UnrealProjects\Project_RE\Project_RE.uproject" "/Game/Level/Main" -game -windowed -ResX=1280 -ResY=720 -nosplash -nosound -stdout -abslog="E:\UnrealProjects\Project_RE\Saved\Logs\spiral-tuneB.log" >/dev/null 2>&1
sleep 3
cp /e/UnrealProjects/Project_RE/Saved/SpiralTune5s.png /e/UnrealProjects/Project_RE/Saved/SpiralTuneB5s.png
ls -la /e/UnrealProjects/Project_RE/Saved/SpiralTuneB5s.png
```
Read 툴로 `SpiralTuneB5s.png` 확인.

### 3-4. 확정값 선택

판단 기준: ① 직선 방사(모든 각도 살) 소멸, ② 나선 팔이 휘어 보임, ③ 균등 밀도. 두 후보 중 나은 쪽 확정. (둘 다 부족하면 3번째 후보 5.3° / 23.1° 추가 반복.) `REBossCharacter.h` 확정값 + 주석 갱신:

```cpp
	/** Spiral 호출당 BaseAngle 증가량(deg). 링 간격(22.5°)과 비정합 → 나선 팔이 휜다(#64). */
	static constexpr float SpiralRotationStepDeg = <확정값>f;
```

### 3-5. 스펙 문서에 확정값 기록

`docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md` 설계 결정 5번 "Spiral 회전 스텝 조정" 항목에 확정값 + 근거(어느 후보가 왜 나았는지 1줄) 추가.

### 3-6. 임시 프로브 제거 + 빌드

3-1에서 넣은 `TEMP-PROBE` 블록 + 3개 임시 include(`UnrealClient.h`/`Misc/Paths.h`/`Engine/Engine.h`) 제거. 빌드 게이트 → `Result: Succeeded`.

### 3-7. 커밋

```bash
git add Source/Project_RE/Core/REBossCharacter.h docs/superpowers/specs/2026-07-18-boss-pattern-rotation-design.md
git commit -m "$(cat <<'EOF'
feat(M5): Spiral 회전스텝 비정합값으로 나선 개선 (#64)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
EOF
)"
```

## 완료 후

PR 생성 (base=`dev`):
- 이슈 #64 메타 미러링: label(enhancement, mass-entity, C++) + milestone(M5) + assignee(leejimin3) + project(Project_RE 개발 로드맵).
- PR 본문 6개 필드 전부 채움. Reviewer 생략.
- `gh pr create --base dev --title "feat(M5): 보스 탄막 패턴 로테이션 (#64)" --body-file <파일>` 후 `gh pr edit`로 프로젝트/이슈 링크.
- 본문에 `Closes #64`.

의도된 잔여 TODO (후속 이슈 몫, 건드리지 말 것):
- `TriggerBulletPattern` 내 `// TODO M5: Multicast_TriggerPattern RPC` — M5 본작업.
- Fan case `// TODO M5: 멀티는 타깃 선택` — M5 본작업.

## 하지 말 것 (스코프 밖)

- **Homing 구현** — 스텁 유지(early-return). 건드리지 말 것.
- **패턴 파라미터 Settings(ini) 노출** — 상수(`SpiralPhaseSec` 등)로 유지.
- **M5 RPC 실배선** — 시드 동기화/Multicast는 M5 본작업.
- **패턴 3종+ 추가** — Spiral/Fan 2종만.
- **Mass 프로세서 수정** — 스폰 경로만 사용.
