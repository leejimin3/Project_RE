# 폭발 FX 드로우콜 상수화 — ISM 다층 설계 (#149)

2026-09-13. 선행: `2026-08-15-bullet-explosion-design.md` §7.3 (드로우콜 실측과 정정, #147).

## 1. 목표와 비목표

**목표.** 폭발 FX 의 드로우콜을 **동시 폭발 개수와 무관한 상수**로 만든다.

상수값이 1이어야 할 이유는 없다. 층 2개면 2, 3개면 3이다. 조건은 **개수에 비례하지 않는 것**뿐이다.

**비목표.**
- 오버드로우(픽셀 비용) 제거 — 개수에 선형으로 남는다. 이 설계는 드로우콜 축만 다룬다(§8).
- 실사 폭발 룩 유지 — 스타일라이즈로 간다(§4 기각 근거).
- 폭발 연출 종류 분기(피격 / 착지) — 범위 밖(§10).

## 2. 문제

탄막은 Mass + ISM 배칭으로 50,000발이 드로우콜 228 이다(#50, #95). **폭발만 그 배칭 밖에 있다.**

`SpawnSystemAtLocation` 호출마다 `UNiagaraComponent` 1개 = 프리미티브 프록시 1개고, 컴포넌트 사이에는 배칭이 없다. #147 실측:

| 구성 | 동시 컴포넌트 | Draws mean | Draws p99 |
|---|---:|---:|---:|
| 폭발 OFF (기준선) | 0 | 228 | 259 |
| 폭발 ON, 예산 OFF | ~50 | 680 | 780 |
| 폭발 ON, 예산 12 | 12 (상한) | 297 | 330 |

**컴포넌트당 4.9 드로우콜.** #147(PR #148)은 이 선형 비용을 인정하고 동시 개수를 `re.Fx.ExplosionBudget` 12 로 묶었다 — 상한이 곧 드로우콜 상한이다. 대가로 **폭풍 페이즈(착지 160/s)에서 초과분이 스폰되지 않는다.** 비용을 없앤 게 아니라 연출을 깎아 맞췄다.

## 3. 사실 확인 (이 설계의 전제)

구현 전 소스로 확인한 것. 추론이 아니다.

| 사실 | 근거 |
|---|---|
| 카메라가 **월드 고정**이다 — SpringArm pitch **-50° 절대**, yaw/pitch/roll 상속 전부 off, `bUsePawnControlRotation=false`, 길이 1500 | `Source/Project_RE/Core/RECharacterBase.cpp:122-136` |
| ISM 이 **이미 3통** 돌고 있다: `ISM`(직선탄 Sphere) / `ArcISM`(곡사탄 Sphere) / `MarkerISM`(착지 경고 Plane) | `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp:35-122` |
| **나이 → 퍼인스턴스 커스텀데이터 → 머티리얼** 경로가 이미 돌아간다(탄환 스폰 팝) | `REBulletRenderProcessor.cpp:103` `Cd.Add(Age / PopDuration)` |
| 배치 트랜스폼 갱신 경로가 이미 있다 (개별 호출 7.22ms 를 없앤 그것, #95) | `REBulletRenderProcessor.cpp:122` `BatchUpdateInstancesTransforms` |
| 머티리얼 그래프는 **파이썬으로 저작한다**. `MaterialExpressionPerInstanceCustomData` 노드를 이미 쓴다 | `scripts/make_bullet_material.py:105` |
| Niagara **시스템**은 파이썬 조립이 막혀 있다(`EmitterHandles` protected) | `scripts/make_beam_material.py:6-10` (#119, #120 에서 두 번 확인) |
| `/Engine/BasicShapes/Plane` 은 100x100 → 반경 50. 스케일 = 반경/50 | `REArcRenderProcessor.cpp:25` |
| `EngineSphereRadius = 50` | `REBulletGeometry.h:25` |
| 에셋 팩은 **gitignore 대상**인데 `NS_REBulletExplosion.uasset`(리포에 커밋돼 있음)이 **팩 머티리얼 6개를 참조**한다 → **팩 없는 클론에서 폭발이 깨진다**. 에셋 바이너리에서 확인한 참조: `M_Explosion_E`, `M_Explosion_G`, `M_Glow_C_Inst`, `M_Impact_B`, `M_Smoke_A`, `M_Smoke_Ground` (전부 `/Game/Realistic_Starter_VFX_Pack_Vol2/Materials/`) | `scripts/convert_explosion_fx.py:19` + 에셋 참조 스캔 |

## 4. 결정 — ISM 다층

폭발 하나를 **ISM 층 2개**로 만든다. 각 층이 ISM 한 통이고, 층 안에서는 폭발 전체가 인스턴스로 배칭된다.

| 층 | 메시 | 역할 |
|---|---|---|
| 코어 | `/Engine/BasicShapes/Sphere` | 불덩이. 진행도에 따라 커지며 흐려진다 |
| 링 | `/Engine/BasicShapes/Plane` | 수평 충격파. 코어보다 빠르고 넓게 퍼진다 |

**드로우콜 = 층 수(2), 동시 폭발 개수와 무관.**

링을 "바닥"이 아니라 **폭발 위치에 수평**으로 둔다. 공중 피격(플레이어 가슴 높이)이면 공중 충격파, 곡사 착지면 바닥 충격파로 읽힌다 — 호출부를 구분할 필요가 없어진다.

### 4.1 왜 쿼드 1장이 아닌가

"판 1장" 안을 먼저 검토했고 뺐다.

Niagara 스프라이트 렌더러가 만드는 것도 **카메라 정면 쿼드**다. 팩 폭발이 입체로 보이는 이유는 판이 아니어서가 아니라 **판 수십 장이 깊이·회전·크기·투명도를 달리해 겹쳐 있어서**다. 한 장이면 스티커로 읽힌다.

구체를 코어로 쓰면 그 문제가 아예 없다 — 어느 각도에서 봐도 구체고, 카메라가 고정이라 빌보드 로직도 불필요하다.

### 4.2 기각한 대안

**NDC (Niagara Data Channel).** 영속 시스템 1개가 채널을 읽어 스폰 → 드로우콜 = 이미터 수(상수 ~5), **연출이 지금과 동일**하다. 엔진 지원도 있다(`Engine/Plugins/FX/Niagara/Source/Niagara/Internal/DataInterface/NiagaraDataInterfaceDataChannelWrite.h`, 5.8 Niagara 모듈 내장, 별도 플러그인 불요).

뺀 이유는 **재현성과 리스크**다. Niagara 그래프는 파이썬 저작이 막혀 있어(§3) 이미터 5개를 에디터에서 손으로 채널 구동으로 고쳐야 하고, 그 결과물은 재생성 스크립트가 없는 수작업 에셋으로 남는다. 채널이 안 읽히면 조용히 0개 스폰이라 디버깅도 어렵다. 룩을 지키려고 재현성과 난이도를 사는 거래이며, 지금 룩이 요구사항이 아니므로 사지 않는다.

**쿼드 SubUV 플립북(팩 시트).** 지금 화면에 가장 가깝다. 뺀 이유: **팩 의존이 유지된다**(§3 마지막 행 — 팩 없는 클론에서 폭발이 깨지는 현 상태가 그대로), SubUV 격자를 알아내려 에디터 인스펙트가 선행돼야 한다.

**이미터 수 축소 / 수명 단축.** `convert_explosion_fx.py` 의 `--skip` / `--maxlife` 인자로 코드 0줄에 드로우콜 단가를 깎을 수 있다(렌더러 5 → 2 면 컴포넌트당 4.9 → ~2). 뺀 이유: **여전히 개수에 선형이다.** 상수화가 목표인데 기울기만 낮춘다. 상수화하면 이 축은 의미가 없어지므로 측정도 하지 않았다.

**예산 상향.** 드로우콜이 개수에 선형인 채로 상한만 올리면 상한만큼 드로우콜이 올라간다. 문제를 옮기는 것.

## 5. 구조

### 5.1 상태와 소유

`REExplosionFx.cpp` 내부에 폭발 목록 하나:

```
{ FVector Loc; float SpawnTime; }  // 배열
```

현재의 `GLiveExplosionExpiry`(만료시각 배열)를 이것으로 교체한다.

시간은 `World->GetTimeSeconds()` — **게임 시간**이다. 현재 `FPlatformTime::Seconds()`(월클럭)는 일시정지·슬로모에서 실제 컴포넌트 수명과 어긋나는 축이었다. 나이는 동기 시점에 `Now - SpawnTime` 으로 계산하므로 dt 누적이 없다.

**진입점 시그니처 불변.** `REExplosionFx::SpawnBulletExplosion(const UWorld*, const FVector&)` 그대로 두고 내부만 배열 push 로 교체한다 → 호출부 3곳 무수정:

- `Source/Project_RE/Core/RECharacterBase.cpp:530` (히트스캔 피격)
- `Source/Project_RE/Mass/REArcFxProcessor.cpp:74` (곡사 착지)
- `Source/Project_RE/Mass/REBulletHitProcessor.cpp:92` (직선탄 피격)

### 5.2 틱 주인 — 새 프로세서

`REExplosionRenderProcessor` 를 새로 만든다. 쿼리 없음(엔티티를 읽지 않는다), `ExecutionFlags = Standalone | Client`, `bRequiresGameThreadExecution = true`.

매 실행마다: 만료분 제거 → 남은 것들의 트랜스폼·커스텀데이터 배열 구성 → 두 ISM 에 배치 반영.

**기존 `REBulletRenderProcessor::Execute` 에 얹지 않는다.** 그 함수는 `CSV_SCOPED_TIMING_STAT(REBullet, BulletRender)`(`REBulletRenderProcessor.cpp:70`) 안쪽이라, 폭발 동기 비용이 **`BulletRender` 수치로 집계된다.** 이 프로젝트는 그 숫자를 프로파일 문서와 포트폴리오에 쓴다. 별 프로세서면 자기 `TRACE_CPUPROFILER_EVENT_SCOPE` + CSV 스코프를 가져 폭발 원가를 따로 잰다 — 선행 문서 §7.1 이 "폭발 원가를 CSV 로 재려면 별도 하네스 구성이 필요하다"고 남긴 구멍도 이걸로 메워진다.

파일 배치도 기존 패턴과 같다(`REBulletRenderProcessor` = 직선탄, `REArcRenderProcessor` = 곡사탄 + 마커).

### 5.3 ISM 2통

`REBulletRenderSubsystem` 에 `ExplosionCoreISM`, `ExplosionRingISM` 을 추가한다. 기존 3통과 같은 설정:

- `SetCollisionEnabled(NoCollision)` — 시각 전용
- `bAffectDynamicIndirectLighting = false`, `bAffectDistanceFieldLighting = false` — Lumen 씬 제외
- `SetCastShadow(false)`
- `SetNumCustomDataFloats(1)`

### 5.4 커스텀데이터 계약

**슬롯 1개. `[0] = 진행도` (0 → 1, `나이 / 수명`).**

두 층이 같은 값을 받고 각자 다르게 해석한다(코어는 알파 감쇠, 링은 링 마스크 + 감쇠).

회전·크기 변주는 커스텀데이터로 넘기지 않는다 — **트랜스폼에서** 처리한다. 쿼드/구체를 roll 로 돌리는 것도, 크기를 흔드는 것도 스폰 시 한 번 계산하면 되는 스케일·회전 값이다. 시드를 머티리얼로 보낼 이유가 없다.

### 5.5 머티리얼

`scripts/make_explosion_material.py` (신규) 가 두 개를 만든다. 기존 생성기와 같은 규약 — 초안 생성기이지 정본이 아니고, 이미 존재하면 `--force` 없이는 중단한다.

| 에셋 | 내용 |
|---|---|
| `/Game/Materials/M_REExplosionCore` | 색 + 알파 감쇠(`1 − 진행도`). 흰 코어 → 주황 → 소멸 |
| `/Game/Materials/M_REExplosionRing` | 링 마스크 + 알파 감쇠. `M_ArenaMarker` 의 링 로직을 가져오되 진행도 페이드를 더한다 |

`M_ArenaMarker` 를 그대로 재사용할 수 없다 — 마커는 커스텀데이터를 읽지 않는다(`REArcRenderProcessor.cpp:124` 주석: "마커는 바닥 디스크라 커스텀데이터를 쓰지 않는다"). 기존 스크립트를 고치면 마커 에셋에 회귀가 생기므로 새 스크립트로 분리한다.

**블렌드: Unlit + Translucent + two_sided.** `M_ArenaMarker`(`make_arena_marker.py:48-50`) · `M_REBeam` 과 같은 조합이고, 마커는 이미 반투명 ISM 인스턴스 수십 개를 띄운 채 출하돼 있다.

**additive 는 쓰지 않는다.** `make_beam_material.py:15-19` 에 되돌린 기록이 있다 — 아레나 바닥이 밝은 라벤더라 additive 가 흰색으로 포화돼 색이 죽었다.

**팩 텍스처를 쓰지 않는다** → 팩 없는 클론에서도 폭발이 나온다(§3 마지막 행의 현존 결함이 같이 해소된다).

### 5.6 초기 수치

스크린샷으로 잡을 값이며 `constexpr` 로 둔다(기존 룩 상수와 같은 방식).

| 값 | 초기값 | 근거 |
|---|---|---|
| 코어 반경 | 20uu → 120uu | 탄 지름 50, `REBulletGeometry::HitRadius` 60 보다 커야 폭발로 읽힌다 |
| 링 반경 | 40uu → 220uu | 코어보다 빠르고 넓게 |
| 수명 | 0.8s (`ExplosionLifeSec` 유지) | 예산 회수 주기와 같은 값이라 어긋날 여지가 없다 |

`ExplosionScale = 0.06f`(Niagara 시스템 스케일)는 **삭제 대상**이다 — Niagara 스폰 경로가 사라진다.

### 5.7 예산 CVar 처리

`re.Fx.ExplosionBudget` 을 **남기고 기본값만 12 → 256** 으로 올린다.

드로우콜 상한이라는 역할은 끝나지만 오버드로우 안전망으로 쓸모가 있고(§8), `0` 으로 두는 재빌드 없는 A/B 노브도 유지된다. 이름은 그대로 둔다 — 의미(동시 개수 상한)가 바뀌지 않았다.

`ExplosionProbe` 로그(`스폰=` / `버림=` / `동시=`)도 유지한다. 상한에 닿았는지를 보는 유일한 관측점이다.

## 6. 데이터 흐름

```
피격/착지 (호출부 3곳)
  → REExplosionFx::SpawnBulletExplosion(World, Loc)
      → 데디서버 / re.Fx.Explosions 0 이면 반환
      → 예산 초과면 버림 (카운트만)
      → 목록에 {Loc, Now} 추가

매 프레임 (REExplosionRenderProcessor::Execute, GT)
  → 만료분 제거 (나이 > 수명)
  → 각 폭발마다:
       코어 트랜스폼 = {Loc, 상수회전, 스케일=코어반경(진행도)/50}
       링   트랜스폼 = {Loc, 수평,     스케일=링반경(진행도)/50, Z=1.0}
       커스텀데이터 = 진행도
  → ExplosionCoreISM / ExplosionRingISM 에 배치 반영
```

## 7. 함정 (선례에서 확인된 것)

**Z 스케일 1.0 미만 금지.** `REArcRenderProcessor.cpp:16-23` — XY 2.4 에 Z 를 0.02/0.15/0.4 로 두면 **인스턴스가 화면에서 통째로 사라진다**(실RHI 스크린샷 이진탐색으로 확인, ISM 극단 비등방 스케일의 컬링/바운즈 이슈 추정, 엔진 레벨·원인 미상). 링은 XY 만 키우고 **Z = 1.0 고정.**

**유니티 빌드 섀도잉.** 프로브용 함수 지역 static 이름을 다른 파일과 겹치지 않게 둔다(`ProbeTick` / `ArcProbeTick` 선례 — `REArcRenderProcessor.cpp:134` 에 같은 주의가 이미 적혀 있다). 머지 전 풀 유니티 빌드(`-DisableAdaptiveUnity`)로 확인한다.

**CSV 스코프 오염.** §5.2. 폭발 동기를 `BulletRender` 스코프 안에 두면 프로파일 수치가 거짓이 된다.

**반투명 ISM 정렬.** 인스턴스 간 정렬이 되지 않아 겹칠 때 순서가 틀릴 수 있다. 폭발은 겹침이 순간적이라 넘어갈 것으로 보지만 **스크린샷 게이트로 판정한다.** 아티팩트가 뜨면 코어를 `Opaque` 로 돌리는 것이 폴백이다(대가: 소멸이 뚝 끊긴다).

**바닥 Z-fighting.** Main 레벨 바닥 윗면이 Z=40 이다(`REArcRenderProcessor.cpp:28-31`, 마커가 Z+55 를 쓰는 이유). 착지 폭발의 링이 바닥에 묻히면 같은 종류의 오프셋이 필요하다 — 스크린샷으로 확인한다.

## 8. 한계 (의도한 것)

**오버드로우는 상수화되지 않는다.** 드로우콜이 2로 묶여도 반투명 구체·링 수백 개가 화면에서 겹치면 같은 픽셀을 여러 번 칠한다. 이 비용은 동시 개수에 선형으로 남는다.

즉 동시 개수 상한이 사라지는 게 아니라 **걸리는 지점이 12 에서 수백으로 올라간다.** 그래서 예산 CVar 를 지우지 않는다(§5.7). 상한의 근거가 "드로우콜"에서 "오버드로우"로 바뀌는 것이고, 그 지점은 실측으로 잡는다(§9).

**연출이 바뀐다.** 스파크 파편, 연기 잔류, 열왜곡이 없어진다. 스타일라이즈 룩이다.

## 9. 검증

| 게이트 | 기준 |
|---|---|
| **상수성 (핵심)** | `scripts/profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.FakeHitTargets 16,re.Fx.ExplosionBudget 0"` — **예산을 끈 채로** 드로우콜이 기준선(228) + 상수여야 한다. 현재 이 조건은 680 / 780 이다. 동시 개수를 `ExplosionProbe` 로 읽어 개수와 드로우콜이 무상관인 것을 확인한다 |
| 오버드로우 상한 | 같은 하네스에서 RT ms 를 관측해 동시 개수 몇 개부터 프레임이 무너지는지 찾는다. 그 값이 `re.Fx.ExplosionBudget` 의 새 근거가 된다 |
| 연출 | `re.Debug.ScreenshotFrame N` 으로 변경 전/후 대조(#97). 플레이어 주변 피격 폭발이 보이는지, 곡사 착지 폭발이 바닥에 묻히지 않는지, 반투명 정렬 아티팩트가 없는지 |
| 팩 없는 클론 | 폭발 경로가 팩 에셋을 참조하지 않는 것을 확인(로드 실패 로그 없음) |
| 빌드 | `Project_REEditor` + `Project_REServer`(풀 유니티, `-DisableAdaptiveUnity`) 둘 다 `Result: Succeeded` |

## 10. 범위 밖 (YAGNI)

- **3번째 층(연기 대역 큰 반투명 구체).** 스크린샷이 납작하다고 말하면 그때 더한다 — ISM 한 통 추가 = 드로우콜 +1, 수정 1곳이라 나중에 붙여도 비싸지지 않는다
- **연출 종류 분기(피격 / 착지 다른 폭발).** 호출부가 이미 갈려 있어 인자 하나로 되지만, 지금 문제는 연출 부족이 아니라 드로우콜이다
- **SubUV 플립북 룩(팩 시트).** §4.2 기각. 팩 의존을 다시 들이는 결정이 필요해지면 별 이슈
- **NDC.** §4.2 기각. 실사 파티클 룩이 요구사항이 되면 그때 스파이크부터
- **`convert_explosion_fx.py` / `NS_REBulletExplosion` 제거.** Niagara 경로가 죽으면 에셋과 생성기가 고아가 되지만, 폴백 여지를 남겨 이 작업에서는 지우지 않는다. 별 이슈로 정리
- **M7 프로파일 문서 재측정.** `docs/profiling/M7-pattern-p99.md` 는 폭발 무제한으로 잰 값이라 #147 이후 이미 재현되지 않는다. `README.md:143` · `docs/portfolio/portfolio-source.md:246` 의 "곡사 패턴은 렌더 스레드 병목 — 착지 폭발이 비용의 대부분" 결론도 같이 재검토해야 한다. 이 설계가 끝난 뒤 한 번에 한다

## 참고

- 선행 설계: `docs/superpowers/specs/2026-08-15-bullet-explosion-design.md` (개별 Niagara 스폰, §7.3 이 드로우콜 실측과 정정)
- #147 / PR #148 — 동시 개수 예산(이 설계가 대체하는 조치)
- #98 — 폭발 단일 진입점
- #119 / #120 — Niagara 시스템 파이썬 조립 불가 확인, 머티리얼 저작 경로 확립
- #50 / #95 — ISM 배칭·배치 갱신 선례
- #97 — 퍼인스턴스 커스텀데이터 룩, 스크린샷 검증 CVar
