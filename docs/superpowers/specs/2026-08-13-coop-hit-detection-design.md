# N인 서버권위 피격 설계 (#86)

- **이슈**: #86 [M5] N인 서버권위 피격 — HitProcessor의 GetPlayerPawn(0) 제거
- **마일스톤**: M5 — 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격
- **날짜**: 2026-08-13
- **라벨**: enhancement, networking, mass-entity, C++

## 목표

Mass 탄막의 피격 판정이 플레이어 인덱스 0만 검사한다. 2번 이후 플레이어는 탄에 맞아도 아무 일이 없다 — **영구 무적**이다.

#85가 N인 게임루프를 열었지만, 그 검증의 마지막 조각이 이 결함에 막혀 있다. 2인 자연 전투로는 "둘 다 사망"에 도달할 수 없어서 #85는 임시 프로브로만 그 경로를 증명했다.

## 현재 상태 (소스 대조)

두 프로세서가 **같은 형태**이고 결함도 같다.

```cpp
// REBulletHitProcessor.cpp:50-68 / REArcHitProcessor.cpp:34-49 — 거의 동일
APawn* Pawn = World ? UGameplayStatics::GetPlayerPawn(World, 0) : nullptr;   // ①
ARECharacterBase* Player = Cast<ARECharacterBase>(Pawn);
if (!Player) { return; }

const UAbilitySystemComponent* ASC = Player->GetAbilitySystemComponent();
if (ASC && ASC->HasMatchingGameplayTag(RETag_State_Dashing)) { return; }     // ②

const FVector PlayerLoc = Player->GetActorLocation();                        // ③
```

| # | 결함 | 증상 |
|---|---|---|
| ① | `GetPlayerPawn(World, 0)` | 인덱스 0 외 플레이어 **영구 무적** |
| ② | 대쉬 태그 확인 후 **프로세서 전체 조기 반환** | **1명이 대쉬하면 전원 무적** — 그 프레임 판정이 통째로 스킵된다 |
| ③ | 위치 하나 | ①의 귀결 |

②는 개별 스킵이 아니라 전체 `return`이다. 이슈 본문이 적은 것보다 나쁘다.

**실측 근거 (#85 Task 6, 15분 관측):** `[RE] BulletHit: Applied=10` 이 1580건인데 `[RE] Player died` 는 **1건**. 나머지 전부가 이미 죽은 인덱스 0 플레이어에게 적용됐고, 인덱스 1은 한 번도 맞지 않았다.

**두 프로세서의 차이:** 직격탄은 명중 시 엔티티를 소멸시킨다(`Ctx.Defer().DestroyEntity`). 곱사탄은 소멸시키지 않는다 — `REArcSimProcessor` 가 착지 시점에 자체 소멸시키기 때문이다.

## 확정된 정책

| 항목 | 결정 |
|---|---|
| 다중 명중 | **직격탄 = 첫 명중만 + 소멸** / **곱사탄 = 반경 내 전원, 소멸 없음** |
| 대쉬 무적 | **개별화** — 대쉬 중인 본인만 빠진다 |
| 사망자 | **판정 대상에서 제외** |
| 성능 실측 | **#88 해결 후로 이월** — 비용 분석으로 대체 |

직격탄을 첫 명중만으로 두는 이유: 투사체 하나가 여러 명을 죽이면 체감이 가혹하고, 몸으로 막아주는 탱 플레이가 성립하지 않는다. 곱사탄은 이미 범위 폭발이고 소멸도 시키지 않으므로 반경 안 전원이 자연스럽다. 현재 코드 구조와도 일치한다.

## 공용 헬퍼를 만든다

두 프로세서의 전처리부는 지금도 verbatim 중복이고, **이번에 고치는 버그가 정확히 그 중복 때문에 생겼다** — `GetPlayerPawn(0)` 이 두 곳에 같은 모양으로 박혀 있었다. 전처리부가 "PC 순회 + 생존 필터 + 대쉬 필터 + 배열 수집"으로 더 복잡해지므로, 중복을 유지하면 다음에 한쪽만 고치는 사고가 난다.

```cpp
// Source/Project_RE/Mass/REHitTargets.h (신규)
struct FREHitTarget
{
    ARECharacterBase* Player   = nullptr;
    FVector           Location = FVector::ZeroVector;
};

/**
 *  피격 판정 대상 수집 (#86). 살아있고 대쉬 중이 아닌 플레이어만 담는다.
 *  서버 판정 프로세서 2종 공용 — 한쪽만 고쳐지는 사고를 막는다.
 *  Out은 Reset 후 채운다(호출부가 배열을 재사용할 수 있게).
 */
void GatherHitTargets(const UWorld* World, TArray<FREHitTarget>& Out);
```

구현은 `GetPlayerControllerIterator` 순회 → 약참조 유효성 → `Cast<ARECharacterBase>(PC->GetPawn())` → `IsAlive()` → 대쉬 태그 부재 확인 → `Out.Add`. `World` 가 null이면 빈 배열을 남기고 반환한다.

위치를 미리 담는 이유: 내부 루프에서 탄마다 `GetActorLocation()` 을 다시 부르지 않는다.

**두 프로세서 모두 `Targets` 가 비면 청크 순회 전에 조기 반환한다.** 대상이 없는데 탄 전량을 순회하는 것은 낭비이고, 오늘의 "플레이어 없음 / 대쉬 중이면 `return`" 과 같은 자리를 지킨다.

## 1. 대쉬 무적 개별화

②의 조기 반환을 수집 단계의 **필터**로 내린다. 대쉬 중인 플레이어만 `Out` 에서 빠지고 나머지는 정상 판정된다.

"판정 자체를 스킵하고 탄환은 파괴하지 않는다"는 기존 의미(#25/#39)는 유지된다 — 대쉬 중인 사람 옆을 지나는 탄은 소멸하지 않고 통과한다. 대상 목록에 없으면 거리 비교 자체가 일어나지 않기 때문이다.

## 2. 사망자 제외 — 시체 방패 해소

`ARECharacterBase::IsAlive()`(#85가 연 public 접근자)로 거른다.

부수 효과가 크다. #85의 최종 리뷰가 "#86의 몫"으로 넘긴 문제가 여기서 풀린다 — 지금은 시체도 판정 대상이라 **탄이 시체에 맞고 소멸해 뒤에 선 생존자를 가려준다.** 제외하면 탄이 시체를 통과한다. 죽은 플레이어에게 `TakeDamage` 를 계속 호출하던 낭비도 사라진다(실측 1580건 중 대부분이 그것이었다).

**해소 범위를 정확히 적는다.** `Targets` 는 `Execute` 진입 시 1회 수집하므로, 어떤 플레이어가 **그 프레임 안에서** 죽으면 남은 탄들은 여전히 그를 대상으로 본다 — 죽은 그 한 프레임 동안은 시체가 탄을 흡수할 수 있다. 다음 프레임부터는 수집에서 빠져 통과한다.

내부 루프마다 생존을 다시 확인하면 이 한 프레임도 없앨 수 있지만 넣지 않는다. 30~60fps에서 한 프레임이고, 판정당 검사를 하나 더 다는 값이 그만큼의 이득을 주지 않는다. **모르고 남긴 것이 아니라 값을 따져 남긴 것**이므로 여기 적어둔다.

## 3. 판정 루프

**직격탄** — 첫 명중에서 멈추고 소멸:

```cpp
for (int32 i = 0; i < Num; ++i)
{
    const FVector BulletLoc = Transforms[i].GetTransform().GetLocation();
    for (const FREHitTarget& T : Targets)
    {
        // 탑다운 — XY 평면 거리만 (탄환 Z와 캡슐 중심 Z 불일치 함정 회피)
        if (FVector::DistSquaredXY(BulletLoc, T.Location) <= HitRadius * HitRadius)
        {
            T.Player->TakeDamage(BulletDamage, FDamageEvent(), nullptr, nullptr);
            Ctx.Defer().DestroyEntity(Ctx.GetEntity(i));
            break;   // 투사체 하나는 한 명만 — 몸으로 막는 탱 플레이가 성립한다
        }
    }
}
```

**겹쳐 선 두 명 중 누가 맞는지는 `Targets` 순서(= PC 순회 순서)로 정해진다.** 최근접이 아니다. 최근접으로 하려면 `break` 를 버리고 전량 스캔 후 최소를 골라야 하는데, 두 캡슐이 히트 반경 안에서 겹칠 만큼 붙어 있는 상황 자체가 드물고 그때 누가 맞든 체감 차이가 없다. 임의 선택을 의도적으로 받아들인다.

**곱사탄** — 착지 반경 안 전원, `break` 없음, 소멸 없음:

```cpp
for (int32 i = 0; i < Num; ++i)
{
    const FArcBulletFragment& A = Arcs[i];
    if (A.Elapsed < A.FlightTime) { continue; }   // 착지 프레임만 판정
    for (const FREHitTarget& T : Targets)
    {
        if (FVector::DistSquaredXY(A.Target, T.Location) <= A.Radius * A.Radius)
        {
            T.Player->TakeDamage(A.Damage, FDamageEvent(), nullptr, nullptr);
        }
    }
}
```

기존 구조 그대로 대상만 늘어난다.

## 4. 비용

탄당 거리 비교가 1회 → N회(N = 생존·비대쉬 플레이어, 실사용 2~4). 5000발 기준 최대 2만 회 `DistSquaredXY`/프레임이고, Mass 청크 순회 오버헤드에 묻힌다. 수집은 `Execute` 당 1회 PC 순회다.

`bRequiresGameThreadExecution = true` 는 이미 켜져 있다(`TakeDamage` 가 액터 호출). 변경 없음.

**실측은 #88 해결 후로 이월한다.** `scripts/profile.ps1` 이 `frames.csv` 를 생성하지 못하는 상태이며(`dev` 에서도 재현), 원인은 이 이슈와 무관한 선재 버그다. 이 이슈의 완료조건에서 실측을 빼고 이월 사실을 남긴다.

## 5. 회귀 안전

**1인이고 그 1인이 살아있는 동안은 `Targets` 가 원소 1개라 오늘과 동작이 완전히 같다.** 소멸률도 판정 결과도 불변이다. #85의 "1인이면 스폰 오프셋이 정확히 0"과 같은 구조적 안전장치다.

전원이 대쉬 중이면 `Targets` 가 비고 조기 반환한다 — 오늘의 "그 1명이 대쉬 중이면 `return`"과 같은 결과다.

### 정정(최종 리뷰, 2026-08-13): 프로파일 하네스는 영향을 받는다

> 이 절의 최초 버전은 "싱글로 도는 프로파일 하네스에 영향이 없다"고 썼는데 **틀렸다.** 그 등가성은 플레이어가 **살아있는 동안만** 성립한다.
>
> `docs/guides/profiling.md` 는 측정 중 플레이어가 DEFEAT에 이르는 것을 **전제**로 쓰여 있다 — 그것이 `re.Profiling.KeepFiring` CVar의 존재 이유다. #86 이전에는 죽은 폰도 여전히 거리 검사·`TakeDamage`·탄 소멸 대상이라 사망 후에도 소멸률이 그대로 유지됐다. 이제는 유일한 대상이 죽는 순간 `Targets` 가 비어 **두 프로세서가 Mass 청크 순회에 진입조차 하지 않는다** — "시체 근처를 안 때린다"가 아니라 "탄을 아예 안 건드린다"로 바뀐 것이다.
>
> 그 시점 이후의 캡처는 정상 게임플레이 원가가 아니라 "대상 없음" 바닥값을 잰다. 오류 없이 조용히 틀린 숫자가 나오므로 더 위험하다.
>
> 대응: `scripts/profile.ps1` 이 Mass·Actor 양쪽 경로의 `-ExecCmds` 에 `re.Cheat.PlayerInvincible 1` 을 주입해 측정 중 플레이어가 죽지 않도록 고정한다. 그러면 대상 목록이 유지돼 #86 이전의 측정 의미가 복원된다. 사유는 `docs/guides/profiling.md` 표에도 남겼다.
>
> 이 CVar는 클라 로컬이라 `profile.ps1` 이 도는 standalone `-game` 실행에서만 유효하다 — 데디 서버에는 적용되지 않는다(범용 해법이 아니다).

## 검증

자동화 테스트 인프라가 없으므로 게이트는 빌드 + 헤드리스 로그 프로브 + 데디 실측이다.

1. **빌드 게이트** — `Project_REEditor`, `Project_REServer` 양쪽 `Result: Succeeded`
2. **싱글 회귀** — `scripts/dedi-verify.ps1` 12항목 통과. 헤드리스 프로브의 대쉬 무적 동작(#39) 유지 확인
3. **2인 데디** — 이 이슈로 **자연 전투로 도달 가능해지는** 것들:
   - 두 플레이어 **모두** 피격 (`BulletHit: Applied=` 가 양쪽 폰에 발생)
   - 1명의 대쉬가 다른 사람의 피격을 막지 **않는다**
   - 전원 사망 → `All 2 players dead` → `EndGame: DEFEAT` → 양 클라 `Client_ShowResult` — **임시 프로브 없이**
   - `DeadPlayers ⊆ ReadyPlayers` 불변식(#85가 고쳤으나 시험대에 못 올린 것)이 실전에서 성립
4. **곱사탄 반경 전원 피격** — 두 플레이어가 한 착지 반경 안에 있을 때 둘 다 데미지
5. **시체 통과** — 사망자 뒤의 생존자가 정상 피격되는지(시체 방패 해소 확인)

2인 검증은 서버를 `-unattended` 없이 띄우는 수동 페어로 한다(#87 전까지 `dedi-verify.ps1 -Clients 2` 는 반쪽이다). 절차는 `docs/guides/dedicated-server.md` 참조.

## 범위 밖

- **#88 프로파일 하네스 수리** — M6 별도 이슈. 이 이슈의 성능 실측이 거기에 막혀 이월된다
- **탄막 밀도·데미지 밸런스 조정** — N인이 되면 체감이 달라지겠지만 별건이다
- **dedi-verify N인 자동 판정** → #87
- **`Variant_Combat/AI/EnvQueryContext_Player.cpp` 의 `GetPlayerPawn(0)`** — 참조 0건 엔진 템플릿 잔재, 선재 죽은 코드

## 참고

- N인 게임루프(선행): #85, `docs/superpowers/specs/2026-08-12-coop-nplayers-design.md`
- 시체 방패를 "#86의 몫"으로 넘긴 곳: #85 최종 리뷰
- 원본 피격 판정: #27 / 대쉬 무적 프레임: #39
- 프로파일 하네스 고장: #88
- 데디 검증 절차·함정: `docs/guides/dedicated-server.md`
