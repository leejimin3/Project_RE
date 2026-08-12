# N인 협동 대응 설계 (#85)

- **이슈**: #85 [M5] N인 접속 대응 — 단일 플레이어 가정 제거 (타깃 선택·승패·결과 화면)
- **마일스톤**: M5 — 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격
- **날짜**: 2026-08-12
- **라벨**: enhancement, networking, C++

## 목표

게임루프 곳곳에 박힌 "플레이어는 1명" 가정을 걷어낸다. 크래시가 아니라 **무반응**으로 나타나는 종류라 눈치채기 어렵다 — 2번 플레이어는 보스가 조준하지 않고, 죽어도 결과 화면이 안 뜬다.

#84가 탄막을 클라에 보이게 만들었으므로, 이제 여러 명이 같은 탄막을 보며 싸울 수 있는 상태로 만든다.

## 현재 상태 (소스 대조)

| 위치 | 증상 |
|---|---|
| `Core/REGameMode.cpp:122` | `EndGame`이 `GetFirstPlayerController()` 하나만 잡아 `Client_ShowResult` 전송 → **2번 이후 플레이어는 VICTORY/DEFEAT 화면을 영영 못 본다** |
| `Core/REBossCharacter.cpp:197` | Fan의 `CenterAngleDeg`가 첫 플레이어 방향 고정 |
| `Core/REBossCharacter.cpp:172` | Artillery의 `AimLoc`이 첫 플레이어 위치 고정 |
| `Core/RECharacterBase.cpp:189-195` | 한 명이라도 죽으면 `GM->EndGame(false)` — **게임 전체 패배** |
| `Content/Level/Main.umap` | `PlayerStart`가 `PlayerStart_0` **하나뿐** → 2인 이상이면 같은 자리에 스폰 |

`Core/REGameMode.cpp`의 시작 게이트는 #84가 남긴 `bool bPlayerReady` 하나다 — 첫 ready에 발사가 시작된다.

**스코프 밖 (별도 이슈):** `Mass/REBulletHitProcessor.cpp:51`·`Mass/REArcHitProcessor.cpp:35`의 `GetPlayerPawn(World, 0)` → #86.

**손대지 않는 선재 죽은 코드:** `Variant_Combat/AI/EnvQueryContext_Player.cpp:13`도 `GetPlayerPawn(0)`을 쓰지만 참조가 0건인 엔진 템플릿 잔재다.

## 확정된 정책

| 항목 | 결정 |
|---|---|
| 시작 시점 | **고정 인원** — ready가 설정값을 채우면 시작 (RPG 던전 입장 모델) |
| 승패 | **전원 사망 = 패배.** 사망자는 입력 차단 + 그 자리 정지(카메라 유지 = 동료 전투 관전) |
| 보스 조준 | **최근접 생존자** |
| 스폰 지점 | **코드 오프셋** — 에디터 에셋 작업 없이 인덱스별 이격 |

## 상태를 어디에 둘까 — GameMode 전용

#40에서 `AREGameState` 상태기계를 "상태 4개뿐이라 과함, M4 데디 전환 시 재검토"로 미뤘다. 지금 재검토한 결론도 **여전히 불필요**다.

클라가 알아야 하는 건 "내가 죽었다"와 "게임이 끝났다" 둘뿐이고, 둘 다 기존 Client RPC로 밀면 된다. 생존자 수를 클라가 알아야 할 이유는 "2/2 생존" 같은 UI뿐인데 요청된 적이 없다. 복제 상태를 만드는 순간 동기화·권위 문제를 떠안는다.

→ **N인 상태는 전부 서버 GameMode 안에만 산다.** 추가되는 멤버는 셋뿐이다:

| 멤버 | 용도 |
|---|---|
| `TSet<TObjectPtr<APlayerController>> ReadyPlayers` | 시작 게이트 분자, 승패 판정 분모 |
| `TSet<TObjectPtr<APlayerController>> DeadPlayers` | 전원 사망 판정 |
| `int32 SpawnedPawnCount` | 스폰 오프셋 인덱스 |

`SpawnedPawnCount`는 **감소시키지 않는다.** 나갔다 다시 들어오면 오프셋이 계속 바깥으로 밀리지만, 감소시키면 두 플레이어가 같은 인덱스를 받아 겹칠 수 있다 — 겹침이 더 나쁘다(#54). 고정 인원 던전 입장 모델에서는 재입장이 전제되지 않으므로 이 드리프트는 허용한다.

## 1. 시작 게이트 — bool에서 인원 카운트로

```cpp
// CVar: re.Coop.ExpectedPlayers (기본 1)
TSet<TObjectPtr<APlayerController>> ReadyPlayers;

void AREGameMode::NotifyPlayerReady(APlayerController* PC)
{
    if (PC) { ReadyPlayers.Add(PC); }
    TryStartBossFiring();
}

void AREGameMode::TryStartBossFiring()
{
    const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
    if (bFiringStarted || !DemoBoss || ReadyPlayers.Num() < Expected)
    {
        return;
    }
    bFiringStarted = true;
    DemoBoss->StartFiring(FMath::Rand());
}
```

**ini가 아니라 CVar인 이유.** 스테이징 빌드의 `Config`는 pak 안에 들어간다(`docs/guides/dedicated-server.md` 함정). ini로 두면 인원을 바꿀 때마다 재쿡해야 하는데, CVar면 서버 커맨드라인(`-ExecCmds`)으로 넘길 수 있어 #87의 `-Clients N`이 재쿡 없이 돈다.

**`TSet`으로 PC를 담는 이유.** 클라가 `Server_NotifyReady`를 두 번 보내도 카운트가 부풀지 않는다. 단순 `int32++`였다면 연타 한 번에 게이트가 뚫린다.

`AREPlayerController::Server_NotifyReady_Implementation`은 자기 자신을 넘기도록 인자를 채운다.

## 2. 승패 — 전원 사망

`RECharacterBase.cpp:189-195`의 `GM->EndGame(false)` 직접 호출을 GameMode 통지로 바꾼다.

```cpp
void AREGameMode::NotifyPlayerDied(APlayerController* PC)
{
    if (!PC || bGameOver) { return; }
    // DeadPlayers ⊆ ReadyPlayers 불변식 — 아래 "분자가 분모 밖에 있으면" 참조.
    ReadyPlayers.Add(PC);
    DeadPlayers.Add(PC);

    if (AREPlayerController* REPC = Cast<AREPlayerController>(PC))
    {
        REPC->Client_NotifyDeath();
    }
    if (DeadPlayers.Num() >= ReadyPlayers.Num())
    {
        EndGame(/*bVictory=*/false);
    }
}
```

**판정 분모는 `ExpectedPlayers`가 아니라 `ReadyPlayers.Num()`(실제 접속자)다.** 중간에 한 명이 끊겼는데 분모가 고정값이면 남은 사람이 다 죽어도 게임이 끝나지 않는다.

### 분자가 분모 밖에 있으면 조기 패배가 난다

두 집합은 **서로 다른 이벤트로 채워지고 순서 관계가 없다** — `ReadyPlayers`는 클라 RPC(`Server_NotifyReady`), `DeadPlayers`는 서버 데미지 콜백이다. 늦게 접속한 플레이어의 폰은 ready RPC가 왕복하기 전에 이미 살아있는 탄막 속에 스폰돼 있을 수 있고, 그 창에서 죽으면 분자가 분모에 도달한다:

`ReadyPlayers={A}`(1), `DeadPlayers={B}`(1) → `1 >= 1` → **A가 멀쩡히 싸우는 중에 DEFEAT.**

그래서 사망자를 `ReadyPlayers`에도 넣어 `DeadPlayers ⊆ ReadyPlayers` 불변식을 강제한다. 죽었다는 사실 자체가 참가자라는 증거다.

**단 이 경로에서 `TryStartBossFiring()`을 부르면 안 된다** — 사망이 매치를 시작시키는 부작용을 갖게 된다.

### 접속 종료를 반드시 처리해야 한다

분모를 접속자 수로 잡는 순간 **떠난 플레이어를 집합에서 빼는 일이 필수**가 된다. 안 빼면 정확히 위에서 막으려던 교착이 그대로 발생한다 — 한 명이 나가고 남은 사람이 전부 죽어도 `DeadPlayers.Num() < ReadyPlayers.Num()`이라 게임이 영영 안 끝난다. `TObjectPtr`는 PC가 파괴돼도 원소 수를 줄여주지 않는다.

```cpp
void AREGameMode::Logout(AController* Exiting)
{
    if (APlayerController* PC = Cast<APlayerController>(Exiting))
    {
        ReadyPlayers.Remove(PC);
        DeadPlayers.Remove(PC);
        // 남은 사람이 이미 다 죽어 있었다면 지금이 종료 시점이다.
        if (ReadyPlayers.Num() > 0 && DeadPlayers.Num() >= ReadyPlayers.Num())
        {
            EndGame(/*bVictory=*/false);
        }
    }
    Super::Logout(Exiting);
}
```

`ReadyPlayers.Num() > 0` 가드가 필요하다 — 마지막 한 명이 나가면 두 집합이 모두 비는데, 그때 `0 >= 0`으로 DEFEAT를 띄우면 받을 클라가 없는 상태에서 게임이 끝난 것으로 기록된다.

`Client_NotifyDeath`는 `Client_ShowResult`(`REPlayerController.cpp:260-272`)와 같은 패턴 — `DisableInput(this)` 한 줄이다. 폰은 그 자리에 서 있고 카메라가 붙어 있으므로 자연히 동료 전투를 본다. **별도 관전 카메라를 만들지 않는다.**

서버측에서는 죽은 폰의 이동을 멈춘다(`StopMovement` + `StopMovementImmediately`). 앞의 것만으로는 패스팔로잉만 끊기고 `CharacterMovementComponent`의 잔여 속도가 남아 시체가 미끄러진다 — 같은 저장소의 공격 정지 경로(`REPlayerController.cpp`)가 이미 두 호출을 짝지어 쓴다.

### 입력 차단은 클라만으로 부족하다 — 서버 가드가 함께 필요하다

`Client_NotifyDeath`의 `DisableInput`은 **클라 로컬 차단**이다. 이것만 두면 두 가지가 뚫린다:

1. **지연** — 서버에서 죽은 시점부터 통지가 도착하기까지 클라가 보낸 입력이 전부 실행된다. 시체가 미끄러지고 회전하고 마지막 한 발을 쏜다.
2. **조작된 클라** — 무한정. 죽은 플레이어가 보스를 계속 때려 **무덤에서 VICTORY를 낼 수 있다.**

이 프로젝트는 이미 "클라 입력은 신뢰 대상이 아니므로 차단은 서버에 둔다"를 규칙으로 갖고 있다. 그래서 `AREPlayerController`에 `IsPawnAlive()`(폰 캐스트 + `ARECharacterBase::IsAlive()`)를 두고 **서버 RPC 세 곳**에서 조기 반환한다:

- `Server_RequestMove_Implementation`
- `Server_Dash_Implementation`
- `Server_RequestFire_Implementation`

**`Server_NotifyReady`는 가드하지 않는다** — 준비는 죽기 전 행위이고, 가드하면 시작 시퀀스가 깨진다.

이 가드가 필요해진 것은 이 이슈 때문이다. 이전에는 "1명 사망 = 게임오버"였기 때문에 `Server_RequestFire`의 `IsGameOver()` 검사가 이 경로를 덮고 있었는데, **전원 사망 규칙으로 바꾸면서 그 등식이 사라졌고 서버측 차단도 같이 사라졌다.**

## 3. 결과 화면 — 전 클라 전파

```cpp
for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
{
    if (!It->IsValid()) { continue; }
    if (AREPlayerController* REPC = Cast<AREPlayerController>(It->Get()))
    {
        REPC->Client_ShowResult(bVictory);
    }
}
```

이미 죽어서 입력이 차단된 플레이어도 결과 화면은 받아야 하므로 필터하지 않는다. `FConstPlayerControllerIterator`는 약참조를 주므로 역참조 전에 `IsValid()`로 거른다 — 접속 종료 중인 PC가 섞일 수 있다.

`Client_NotifyDeath`는 `Client_ShowResult`와 같이 `UFUNCTION(Client, Reliable)`로 선언한다.

## 4. 타깃 — 최근접 생존자

`Health`/`bIsDead`가 `RECharacterBase.h`의 `protected:`라 보스가 읽을 수 없다. 공개 접근자를 하나 연다.

```cpp
// ARECharacterBase (public)
bool IsAlive() const { return !bIsDead; }
```

보스에 private 헬퍼를 두고 두 호출부가 공유한다.

```cpp
const APawn* AREBossCharacter::FindNearestLivingPlayerPawn() const
{
    const FVector BossLoc = GetActorLocation();
    const APawn* Best = nullptr;
    float BestDistSq = TNumericLimits<float>::Max();
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const ARECharacterBase* P = It->IsValid() ? Cast<ARECharacterBase>(It->Get()->GetPawn()) : nullptr;
        if (!P || !P->IsAlive()) { continue; }
        const float D = FVector::DistSquared2D(P->GetActorLocation(), BossLoc);
        if (D < BestDistSq) { BestDistSq = D; Best = P; }
    }
    return Best;
}
```

**RPC 계약은 바뀌지 않는다.** 두 호출부 모두 #84가 만든 서버 결정 경로(`FireCurrentPattern` / `FireArtillery`) 안에 있고, 결과는 `AngleDeg`·`AimLoc`으로 페이로드에 실린다. #84 설계가 "정책이 바뀌어도 RPC 계약은 그대로"라고 적어둔 자리가 실제로 그렇게 동작한다.

전원 사망 시 폴백은 기존 그대로 둔다(Fan 0°, Artillery 보스+300X). 그 상태면 `EndGame`이 `StopFiring`을 부르므로 과도기일 뿐이다.

## 5. 스폰 이격

`AGameModeBase::SpawnDefaultPawnAtTransform_Implementation`을 오버라이드해 인덱스별 오프셋을 적용한 뒤 `Super`를 호출한다.

```cpp
const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
const float Half = (Expected - 1) * 0.5f;
const FVector Offset(0.f, (SpawnedPawnCount - Half) * SpawnSpacing, 0.f);   // SpawnSpacing = 250.f
++SpawnedPawnCount;
```

**+Y로 흩는다.** 보스가 X=600에 있어 +X로 흩으면 플레이어를 탄막 레인으로 밀어넣는다. #56이 정확히 그 함정이었고(이동 프로브 목적지 X=500이 보스 탄막 X=600과 충돌) 그때 +Y로 옮겨 해결했다.

**1인일 때 오프셋이 정확히 0이다** (`SpawnedPawnCount=0`, `Half=0`). 싱글 스폰 좌표가 기존과 동일하므로 회귀 위험이 구조적으로 없다.

## 6. 관측성

게이트가 안 열릴 때 이유가 보여야 한다. 지금은 보스가 조용히 안 쏘면 원인을 알 수 없다.

```
[RE] Player ready 1/2
[RE] Boss firing started (2/2 ready)
[RE] Player died 1/2
[RE] All 2 players dead
[RE] EndGame: DEFEAT
```

## 검증

자동화 테스트 인프라가 없으므로 게이트는 빌드 + 헤드리스 로그 프로브 + 데디 실측이다.

1. **빌드 게이트** — `Project_REEditor`, `Project_REServer` 양쪽 `Result: Succeeded`
2. **싱글 회귀 (최우선)** — `ExpectedPlayers` 기본 1에서 `scripts/dedi-verify.ps1` 12항목 그대로 통과. 스폰 좌표가 기존과 동일한지 로그로 확인
3. **2인 데디 실측** — 서버에 `-ExecCmds="re.Coop.ExpectedPlayers 2"`, 클라 2개 접속:
   - 클라 1만 붙은 동안 보스가 발사하지 않는다
   - 클라 2 접속 시점에 발사 시작
   - 두 폰이 서로 다른 좌표에 스폰(겹침 없음)
   - 한 명 사망 → 그 클라만 입력 차단, 게임 계속
   - 둘 다 사망 → 두 클라 **모두** `Client_ShowResult` 수신
   - **접속 종료 처리** — 클라 하나를 강제 종료시킨 뒤 남은 하나가 죽으면 `EndGame: DEFEAT`가 뜨는지. 안 뜨면 `Logout` 정리가 빠진 것이고, 증상은 "게임이 영영 안 끝남"이다
4. **NavMesh 확인** — 오프셋 스폰 지점에서 이동이 실제로 되는지. 오프메시면 `Server_RequestMove`가 거부돼 이동이 통째로 죽는다
5. **타깃 전환 관측** — 최근접 플레이어가 바뀔 때 조준이 따라가는지 로그로 확인

**2인 검증은 서버를 `-unattended` 없이 띄운다.** 그 플래그를 주면 첫 클라의 서버측 프로브가 약 4.4초에 `RequestExit`으로 서버를 내린다(#84 궤도 측정에서 쓴 것과 같은 수동 페어 방식). 스크립트 자동화는 #87의 몫이고 #85는 수동 실측으로 닫는다.

## 범위 밖

- **리스폰 / 부활** — M2에서 이미 제외됐다. 새 시스템이 필요해 #85가 크게 불어난다
- **대기 로비 UI / "1/2 접속" 화면** — 로그로 충분하다. 요청된 적 없다
- **별도 관전 카메라** — 사망 폰에 붙은 카메라가 그대로 관전 시점이 된다
- **N인 서버권위 피격**(`GetPlayerPawn(0)`) → #86
- **dedi-verify N인 자동 판정** → #87
- **`AREGameState`** — 위에서 판단한 대로 여전히 불필요
- **`Variant_Combat` 템플릿 잔재** — 참조 0건 선재 죽은 코드

## 참고

- 탄막 서버→클라 동기화(선행): #84, `docs/superpowers/specs/2026-08-11-bullet-shot-multicast-design.md`
- 시작 게이트 훅을 남긴 곳: 같은 스펙의 "시작 게이트" 절
- 스폰 겹침 즉사 전례: #54
- +X 이동이 탄막과 충돌한 전례: #56
- 데디 검증 절차: `docs/guides/dedicated-server.md`, `scripts/dedi-verify.ps1` (#82)
