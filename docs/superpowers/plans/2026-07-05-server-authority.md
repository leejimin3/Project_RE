# M0 #3 서버 권위 패턴 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 베이스 클래스 3종(Character/PlayerController/GameMode)에 서버 권위 패턴(Replicated HP + HasAuthority 가드 + Server RPC 뼈대)을 심어 M4 데디 전환을 '켜기'로 만든다.

**Architecture:** `ARECharacterBase`에 `Replicated` Health와 `TakeDamage` 권위 가드를 넣고 `GetLifetimeReplicatedProps`로 복제 등록. `AREPlayerController`에 `Server_RequestMove` RPC를 선언 뼈대만 심고(실배선 M2), `AREGameMode`는 서버 권위 확인 + 마커 주석 갱신. 넷 실동작은 M5에서 실증하며 지금은 싱글로만 돈다.

**Tech Stack:** UE 5.8 C++, Engine replication (`Net/UnrealNetwork.h`, `DOREPLIFETIME`), `AActor::TakeDamage` 파이프라인.

## Global Constraints

- 엔진: UE 5.8, 타깃 `Project_REEditor` Win64 Development.
- 자동 테스트 인프라 없음 → 각 태스크 게이트 = **에디터 빌드 성공(에러 0)** + **grep 정적 존재 확인**.
- 게임상태(Health) 변경은 항상 `HasAuthority()` 블록 경유 (CLAUDE.md / #3 규칙).
- Surgical: 요청 범위 밖 코드·주석·포맷 손대지 않음. 기존 스타일(한글 주석) 유지.
- 커밋: Conventional Commits, 한 태스크 = 한 커밋. `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>` 푸터.

### 빌드 명령 (공통)

```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```

기대: 마지막 줄 `Build succeeded`, 에러 0. (에디터가 열려 있으면 종료 후 실행 — 파일락 회피.)

---

### Task 1: ARECharacterBase — Replicated HP + TakeDamage 권위 가드

**Files:**
- Modify: `Source/Project_RE/Core/RECharacterBase.h`
- Modify: `Source/Project_RE/Core/RECharacterBase.cpp`

**Interfaces:**
- Produces:
  - `float ARECharacterBase::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override` — 서버에서만 Health 차감, 적용 데미지 반환. 클라에서 호출 시 0 반환.
  - `void ARECharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>&) const override`
  - `float Health` (protected, `Replicated`), `float MaxHealth` (protected, `EditDefaultsOnly`)

- [ ] **Step 1: `.h` — 클래스 주석 마커 갱신 + 멤버/함수 선언 추가**

`RECharacterBase.h`의 클래스 주석 15행 `*  (HP/복제는 M0 #3에서 추가)` 를 아래로 교체:

```cpp
 *  HP는 서버 권위(Replicated) — 데미지 적용은 TakeDamage HasAuthority 가드 경유.
```

그리고 클래스 본문 — 기존:

```cpp
public:
	ARECharacterBase();

protected:
	/** 탑뷰 카메라 붐 (절대 하향 고정) */
```

를 아래로 교체 (public에 오버라이드 2개 선언, protected 카메라 위에 Stats 멤버 추가):

```cpp
public:
	ARECharacterBase();

	//~ 서버 권위 데미지 진입점. 서버에서만 Health 차감.
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	                         AController* EventInstigator, AActor* DamageCauser) override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** 현재 체력. 서버 권위, 클라 복제. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Stats")
	float Health = 100.f;

	/** 최대 체력. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats")
	float MaxHealth = 100.f;

	/** 탑뷰 카메라 붐 (절대 하향 고정) */
```

- [ ] **Step 2: `.cpp` — include 추가 + 생성자 Health 세팅 + 두 함수 구현**

`RECharacterBase.cpp` include 블록(10행 `#include "UObject/ConstructorHelpers.h"` 아래)에 추가:

```cpp
#include "Net/UnrealNetwork.h"
```

생성자 안, `bUseControllerRotationRoll = false;` 다음 줄(19행 뒤)에 추가:

```cpp

	// 체력 초기화 — MaxHealth 조정 시 정합 유지
	Health = MaxHealth;
```

파일 맨 끝(생성자 닫는 `}` 다음)에 두 함수 추가:

```cpp

float ARECharacterBase::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
                                   AController* EventInstigator, AActor* DamageCauser)
{
	// 서버 권위 가드 — 게임상태(Health) 변경은 서버에서만
	if (!HasAuthority())
	{
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Clamp(Health - Applied, 0.f, MaxHealth);
	// TODO M5: 서버권위 피격 판정/이펙트, 사망 처리
	return Applied;
}

void ARECharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARECharacterBase, Health);
}
```

- [ ] **Step 3: 빌드 검증**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 4: 정적 존재 확인**

Run:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "DOREPLIFETIME(ARECharacterBase, Health)" Source/Project_RE/Core/RECharacterBase.cpp && grep -n "if (!HasAuthority())" Source/Project_RE/Core/RECharacterBase.cpp && grep -n "UPROPERTY(Replicated" Source/Project_RE/Core/RECharacterBase.h
```
Expected: 3개 매치 전부 출력.

- [ ] **Step 5: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Core/RECharacterBase.h Source/Project_RE/Core/RECharacterBase.cpp && git commit -m "feat(M0): add replicated HP + TakeDamage authority guard to ARECharacterBase (#3)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: AREPlayerController — Server_RequestMove RPC 뼈대

**Files:**
- Modify: `Source/Project_RE/Core/REPlayerController.h`
- Modify: `Source/Project_RE/Core/REPlayerController.cpp`

**Interfaces:**
- Consumes: 없음 (Task 1과 독립).
- Produces:
  - `void AREPlayerController::Server_RequestMove(FVector Target)` — `UFUNCTION(Server, Reliable)`. 현재 빈 구현(M2 배선 예정). 이동 실동작은 여전히 로컬 `OnClickMove`가 담당.

- [ ] **Step 1: `.h` — Server RPC 선언 추가**

`REPlayerController.h`의 `void OnClickMove(const FInputActionValue& Value);` (29행) 다음에 추가:

```cpp

	/** 이동 요청 서버 RPC 뼈대. 실배선(NavMesh)은 M2. */
	UFUNCTION(Server, Reliable)
	void Server_RequestMove(FVector Target);
```

- [ ] **Step 2: `.cpp` — 빈 구현 추가 + OnClickMove 주석 문구 명확화**

`REPlayerController.cpp`의 `OnClickMove` 안 기존 주석(63행):

```cpp
	// TODO M2: 로컬 이동을 Server RPC 이동 요청 + NavMesh 패스파인딩으로 교체
```

를 아래로 교체:

```cpp
	// TODO M2: 로컬 이동을 Server_RequestMove RPC + NavMesh 패스파인딩으로 교체
```

파일 맨 끝(`PlayerTick` 닫는 `}` 다음)에 추가:

```cpp

void AREPlayerController::Server_RequestMove_Implementation(FVector Target)
{
	// TODO M2: 서버권위 이동 — NavMesh 패스파인딩 목표 설정.
	// 현재는 뼈대만. 클라 로컬 이동(OnClickMove)이 싱글 경로를 담당.
}
```

- [ ] **Step 3: 빌드 검증**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 4: 정적 존재 확인**

Run:
```bash
cd E:/UnrealProjects/Project_RE && grep -n "UFUNCTION(Server, Reliable)" Source/Project_RE/Core/REPlayerController.h && grep -n "Server_RequestMove_Implementation" Source/Project_RE/Core/REPlayerController.cpp
```
Expected: 2개 매치 전부 출력.

- [ ] **Step 5: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Core/REPlayerController.h Source/Project_RE/Core/REPlayerController.cpp && git commit -m "feat(M0): add Server_RequestMove RPC skeleton to AREPlayerController (#3)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: AREGameMode 서버권위 확인 + 마커 주석 갱신 + 최종 빌드

**Files:**
- Modify: `Source/Project_RE/Core/REGameMode.h`

**Interfaces:**
- Consumes: 없음.
- Produces: 없음 (코드 변경 없음, 주석만).

GameMode는 `AGameModeBase` 상속 → 서버에만 존재(클라 복제 안 됨). 모든 로직이 곧 서버 권위. 별도 가드 불필요. 코드는 이미 정확하므로 완료 마커 주석만 갱신한다.

- [ ] **Step 1: `.h` 클래스 주석 마커 갱신**

`REGameMode.h`의 클래스 주석(25행) `*  (서버 전용 로직은 M0 #3에서 추가)` 를 아래로 교체:

```cpp
 *  GameModeBase는 서버에만 존재 → 모든 로직이 곧 서버 권위(별도 가드 불필요).
```

- [ ] **Step 2: 전체 빌드 최종 검증**

Run:
```bash
"E:/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -project="E:/UnrealProjects/Project_RE/Project_RE.uproject" -waitmutex
```
Expected: `Build succeeded`, 에러 0.

- [ ] **Step 3: 완료 기준 전체 확인**

Run:
```bash
cd E:/UnrealProjects/Project_RE && grep -rn "M0 #3에서 추가" Source/Project_RE/Core/
```
Expected: **매치 0** (모든 미완료 마커 제거됨).

- [ ] **Step 4: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Core/REGameMode.h && git commit -m "docs(M0): confirm AREGameMode server authority, update marker comment (#3)

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 완료 후

3개 태스크 커밋 완료 → PR 생성(base `dev`, #3 메타 미러링). PR 규칙은 메모리 `pr_creation_convention` 준수.
