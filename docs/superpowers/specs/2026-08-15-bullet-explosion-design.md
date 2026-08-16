# 탄환 소멸 폭발 연출 설계 (#98)

**작성일:** 2026-08-15 · **이슈:** #98 · **마일스톤:** M6

> 한 줄: **피격·착지 시 Niagara 폭발을 띄운다. 판정을 클라로 열어 복제 없이 처리하고(데미지는 서버 권위 유지), 스폰은 클라 전용 게임스레드 프로세서 한 곳으로 모은다. 수명 만료에는 붙이지 않는다.**

---

## 1. 목표와 비목표

**목표:** 탄환이 소멸할 때 시각 피드백을 준다. 현재는 아무 표시 없이 사라진다.

**비목표:** 탄막 렌더러를 Niagara 로 교체하는 것. 그건 #50 에서 성능 근거가 없다고 판정됐고, 데디서버에 렌더가 없어 Niagara 는 이동을 소유할 수도 없다.

## 2. 소멸 지점 셋 — 하나는 절대 쓰면 안 된다

| 위치 | 계기 | 실행 넷모드 | 스레드 | 폭발 |
|---|---|---|---|---|
| `REBulletSimProcessor.cpp:45` | **수명 만료** | `AllNetModes` | 워커 | **금지** |
| `REBulletHitProcessor.cpp:70` | 직선탄 피격 | `Standalone \| Server` | **GT** | 필요 |
| `REArcSimProcessor.cpp:48` | 곡사탄 착지 | `AllNetModes` | **워커** | 필요 |

**수명 만료에 붙이면 안 된다.** 정상상태 4,800발 / 수명 15초 = **초당 약 320발 만료**. 여기에 이펙트를 스폰하면 다른 모든 설계가 무의미해진다. 폭발은 **게임플레이 의미가 있는 소멸**(피격·착지)에만 붙인다.

## 3. 네트워크 — 복제하지 않는다

### 현재 상태

`REBulletHitProcessor` 는 `Standalone | Server` 라 클라에서 돌지 않고, 소멸을 클라에 알리는 경로가 **없다**(파괴 후 `UE_LOG` 만 남긴다). 따라서 **데디 환경에서 서버가 피격으로 없앤 탄이 클라 화면에서는 계속 날아간다.** 폭발 이전에 이미 존재하는 어긋남이다.

### 채택: 클라가 코스메틱 판정을 자체 수행

```
서버: 판정 → 데미지 + 파괴
클라: 판정 → 파괴 + 폭발 (데미지 없음)
```

- **`REBulletHitProcessor` 만** `AllNetModes` 로 연다
- `TakeDamage` 호출을 `Player->HasAuthority()` 로 가드한다
- 파괴는 양쪽에서 실행한다

**`REArcHitProcessor` 는 `Standalone | Server` 로 둔다.** 곡사탄은 착지 데미지만 주고 파괴는 `REArcSimProcessor`(이미 `AllNetModes`)가, 폭발은 §5 의 FX 프로세서가 한다. 클라가 거기서 할 일이 없으므로 열 이유가 없다 — 여는 것 자체가 비용이고 권위 경계만 흐린다.

**RPC 가 0개다.**

### 왜 복제가 아닌가

**이 프로젝트는 이미 이 구조를 골랐다.** #84 에서 `REBulletSimProcessor` 를 `AllNetModes` 로 두고, 클라가 `Multicast_FireDirect` + `ServerTime` 으로 탄을 재구성해 **위치를 자체 계산**한다. 판정만 서버 전용인 것이 오히려 봉합선이고, 그 자리에서 위 어긋남이 나왔다.

복제로 가면 위치는 클라가 계산하는데 소멸은 서버가 통보하는 이중 구조가 되고, **클라는 통보받은 좌표로 "어느 탄이었는지" 되짚는 근사**를 해야 한다. 서버 권위의 정확성 이점이 그 근사에서 사라진다.

MMORPG 류는 이벤트 복제를 쓰지만 참조 대상이 아니다 — 거기서는 투사체를 클라가 시뮬하지 않아 **스스로 판정할 근거 자체가 없다**. 판단 기준은 "클라가 그 사건을 독립적으로 알 수 있는가"이고, 결정론적 투사체는 알 수 있다.

**데미지 권위는 그대로 서버에 남는다.** 바뀌는 것은 "누가 탄을 지우는가"뿐이다.

### 감수하는 한계

플레이어 위치 복제 지연 때문에 클라와 서버 판정이 미세하게 갈릴 수 있다. 클라가 먼저 지운 탄을 서버가 나중에 맞히면 **탄 없이 데미지만 들어오는** 순간이 생긴다. 코스메틱 범위이고 양쪽 다 수명으로 수렴한다. **이 한계를 감수한다.**

## 4. 타겟 수집을 클라에서도 동작하게

`GatherHitTargets` 가 `GetPlayerControllerIterator` 를 쓴다. **클라에서는 로컬 컨트롤러 하나만 나온다.** M5 가 N인 협동으로 열어놨으므로 그대로 두면 **자기 피격만 보이고 동료 피격은 안 보인다.**

`TActorIterator<ARECharacterBase>` 로 바꾼다 — 클라도 복제된 모든 캐릭터를 찾는다. 사망·대쉬 필터는 그대로 유지한다.

## 5. 스폰 지점 — 시뮬을 건드리지 않는다

Niagara 스폰은 **게임 스레드 전용**이다. 워커 스레드에서 부르면 크래시한다(ISM 변형 프로세서를 GT 에 고정해야 했던 것과 같은 부류).

| 지점 | 조치 |
|---|---|
| 직선탄 피격 | `REBulletHitProcessor` 는 `TakeDamage` 때문에 **이미 `bRequiresGameThreadExecution = true`** — 그대로 스폰 |
| 곡사탄 착지 | `REArcSimProcessor` 는 워커다. **건드리지 않는다** |

### 곡사탄: 별도 클라 전용 FX 프로세서

```
REArcFxProcessor   (신규, GT, Standalone|Client)  ← 착지 직전 폭발만 스폰
REArcSimProcessor  (기존, 워커, AllNetModes)      ← 그대로. 착지 판정 + 파괴
```

FX 프로세서를 시뮬 **앞**에 배치한다(`FMassProcessorExecutionOrder::ExecuteBefore`). 같은 조건(`Elapsed >= FlightTime`)으로 착지할 탄을 찾아 폭발을 띄우고, 파괴는 시뮬이 그대로 한다.

**이유:** 곡사탄이 수천 개로 늘어도 시뮬이 워커에 남는다. 시뮬을 GT 로 옮기면 그 확장 여지를 지금 팔아버리는 셈이다.

### 데디서버에서는 스폰하지 않는다

FX 프로세서가 `Standalone | Client` 라 데디서버에서 아예 실행되지 않는다. 직선탄 쪽은 `AllNetModes` 로 열리므로 **스폰 직전에 `NM_DedicatedServer` 를 확인**한다.

## 6. 폭발 에셋

엔진이 `/Niagara/DefaultAssets/Templates/Systems/SimpleExplosion` 를 제공한다. **복제해 프로젝트 소유로 만든다** — `Content/FX/NS_REBulletExplosion`.

머티리얼(#97)과 같은 방식으로 **Python 부트스트랩**을 쓴다(`UnrealEditor-Cmd.exe -ExecutePythonScript=`). 에디터 GUI 없이 만들 수 있고, 재현 가능하며, 이후 튜닝은 에디터에서 한다. 스크립트는 부트스트랩이지 정본이 아니다 — 기존 에셋이 있으면 덮어쓰지 않고 중단하며, `--force` 로만 재생성한다.

**색은 탄환과 맞춘다.** #97 의 파스텔 분홍/하늘 톤과 어울려야 하고, 발광이 과하면 블룸이 화면을 씻는다(#97 실측). 초기값은 낮게 잡고 스크린샷으로 조정한다.

## 7. 규모 한계와 상향 경로

**개별 스폰은 호출마다 컴포넌트를 만든다** — 스레드를 어디에 두든 수천 개는 무너진다. 다만 엔진에 **컴포넌트 풀링이 내장돼 있다**: `SpawnSystemAtLocation(..., ENCPoolMethod PoolingMethod)` 에 `AutoRelease` 를 주면 풀에서 꺼내 쓰고 자동 반납한다(엔진 주석: *one-shot fx that you don't need to keep a reference to and can fire and forget*).

**처음부터 `AutoRelease` 를 쓴다.** 인자 하나이고, 안 쓸 이유가 없다. 이것만으로 개별 스폰의 실용 상한이 크게 올라간다.

현재 규모는 작다:

| | 값 | 근거 |
|---|---|---|
| 곡사탄 동시 수 | **최대 12** | `ArtilleryCount=12`, `ArtilleryFireInterval=1.8s` > `ArtilleryFlightTime=1.5s` (겹침 억제가 이미 의도됨) |
| 직선탄 피격 연쇄 | 약 10 | `BulletDamage=10`, 플레이어 HP 100 — 10발이면 사망해 자연히 제한 |

**지금은 개별 스폰으로 간다.** 수천 규모를 위한 NDC 파이프라인을 미리 짓지 않는다 — 존재하지 않는 부하를 위한 설계다.

대신 **스폰을 한 함수로 모으고**, 개별 스폰이 몇 개에서 무너지는지 **측정해 문서에 남긴다**. 곡사탄을 늘릴 때 그 숫자가 판단 근거가 된다.

| 규모 | 방식 |
|---|---|
| 수십 (현재) | `SpawnSystemAtLocation` + `ENCPoolMethod::AutoRelease` |
| 수백~ (측정으로 확인) | 위와 동일. 풀링이 어디까지 버티는지가 §9 측정 항목이다 |
| 그 위 | 영속 시스템 1개 + NDC 로 위치 배열 주입 (#50 에서 검토한 기술) |

**중간 단계가 공짜로 존재하므로 NDC 는 더 멀어졌다.** 측정 없이 미리 지을 이유가 없다.

### 7.1 측정 결과 (2026-08-16, 검증 중 실측)

`ArtilleryCount` 를 올려 동시 스폰 규모를 재봤다. 로테이션을 정상 동작시켜야 한다 —
`re.Profiling.KeepFiring 1` 은 로테이션을 우회하고 Spiral 만 쏘므로 곡사탄이 0발이 된다
(`REBossCharacter.cpp:92`). 이걸 모르고 재면 `ArtilleryCount` 가 무관해 보인다.

| ArtilleryCount | 한 일제사당 폭발 | 관측 최고 비율 | 결과 |
|---|---|---|---|
| 12 (출하값) | 12 | 약 4/s | 정상 |
| 200 | 200 | **110.8/s** (1.81s 창에 200) | 크래시·정지 없음 |

**`AutoRelease` 풀링은 200 동시 스폰에서 안 무너진다.** 곡사탄을 열 배 이상 늘려도
개별 스폰으로 간다. NDC 는 여전히 필요 없다.

프레임 원가 자체는 아직 수치화 못 했다 — `scripts/profile.ps1` 이 측정 오염을 막으려
`re.Fx.Explosions 0` 으로 폭발을 꺼두기 때문이다. 폭발 원가를 CSV 로 재려면 별도 하네스
구성이 필요하다.

### 7.2 실행 순서 함정 (검증에서 발견)

`REArcFxProcessor` 를 `ExecuteBefore` 시뮬로 두면 **폭발이 한 번도 안 뜬다.**
`Elapsed` 를 증가시키는 것이 시뮬이라, 착지 프레임에 먼저 돌면 아직 갱신 전 값
(`Elapsed < FlightTime`)을 보고, 다음 프레임에는 엔티티가 이미 파괴돼 있다.
`ExecuteAfter` 가 맞다 — 시뮬의 파괴는 `Defer()` 라 페이즈 끝에야 반영되므로
뒤에 돌아도 트랜스폼을 읽을 수 있다(`REArcHitProcessor` 가 같은 방식).

조용히 실패하므로 눈으로는 안 잡힌다. 폭발 총합이 `BulletHit` 건수와 **정확히 일치**하면
(곡사탄 초과분 0) 곡사탄 경로가 죽어 있다는 신호다.

## 8. 선행 조건 — 플러그인

`Project_RE.uproject` 의 활성 플러그인에 **Niagara 가 없다**(`ModelingToolsEditorMode` / `StateTree` / `GameplayStateTree` / `MassGameplay` / `GameplayAbilities` / `ModelContextProtocol`). `Build.cs` 에도 `Niagara` 모듈이 없다.

`MassGameplay` 를 안 켜서 Mass 프로세서 `Execute` 가 0회 돌던 전례가 있다 — 코드는 멀쩡한데 실행이 안 돼 한참 못 봤다. **활성화가 첫 작업이고, 실제로 로드됐는지 실행 로그로 확인하고 넘어간다.**

## 9. 검증

| 게이트 | 기준 |
|---|---|
| 플러그인 | 실행 로그에서 Niagara 모듈 로드 확인 — "켰다고 가정" 금지 |
| 빌드 | `Project_REEditor` / `Project_REServer` 양쪽 `Result: Succeeded` |
| 데디 | `scripts/dedi-verify.ps1` 전 항목 PASS. **판정이 클라로 열리므로 서버 권위가 안 깨졌는지가 핵심** |
| 데미지 권위 | 클라에서 `TakeDamage` 가 호출되지 않는 것을 로그로 확인 |
| 상한 | `scripts/profile.ps1` 로 재측정. 판정이 클라에서도 돌게 되므로 영향 확인 (현재 상한 50,000발) |
| 시각 | `re.Debug.ScreenshotFrame` 으로 폭발 확인 (#97 에서 만든 수단) |
| 동시 폭발 부하 | 개별 스폰이 무너지는 지점을 측정해 §7 표에 기록 |

## 10. 범위 밖

- **탄막 렌더러 Niagara 교체** — #50 에서 성능 근거 없음으로 판정. 데디에 렌더가 없어 이동 소유 불가
- **수명 만료 폭발** — §2 사유로 금지
- **NDC 배칭** — §7 사유로 보류. 측정된 상한을 근거로 나중에
- **대쉬 통과 시 탄환 소멸** — #102. 다만 §3 이 클라 판정을 열므로 그 이슈의 설계와 맞물린다. 소멸에 시각 피드백이 필요하다면 이 이슈의 폭발을 재사용한다
- **마커 ISM 연출** — 착지 마커는 현행 유지

## 참고

- `Source/Project_RE/Mass/REHitTargets.cpp` / `.h` — 대상 수집(서버 판정 프로세서 2종 공용, #86)
- `Source/Project_RE/Mass/REBulletHitProcessor.cpp:70` — 직선탄 피격·소멸
- `Source/Project_RE/Mass/REArcSimProcessor.cpp:48` — 곡사탄 착지·소멸
- `Source/Project_RE/Core/REBossCharacter.h:119-125` — 곡사탄 규모 상수
- #84 — `Multicast_FireDirect` + `ServerTime`. §3 이 완성하는 구조
- #97 — Python 부트스트랩 방식, `re.Debug.ScreenshotFrame`, 발광이 과하면 블룸이 씻긴다는 실측
