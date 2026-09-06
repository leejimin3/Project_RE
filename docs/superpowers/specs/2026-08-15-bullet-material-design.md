# 탄막 머티리얼 설계 (#97)

**작성일:** 2026-08-15 · **이슈:** #97 · **마일스톤:** M6

> 한 줄: **겹친 탄의 경계가 안 보이는 문제를 인접 탄 색 교차(체커보드)로 푼다. 언릿 발광 + 프레넬 림 머티리얼 위에 퍼인스턴스 커스텀데이터 float 2개(스폰 팝 · 색 선택)를 얹는다. 수명 페이드는 히트박스 모호함 때문에 넣지 않는다.**

> **⚠️ §2 정정(구현 중 육안 확인으로 반증됨).** 이 스펙은 처음에 "프레넬 림이 경계 문제를 푼다"고 진단했다. **틀렸다.** 원인은 셰이딩 부족이 아니라 기하학적 겹침이었고, 림은 오히려 "테두리 있는 선"을 만들었다. 실제 해법은 **§2.5 색 교차**다. 림은 각 구체의 윤곽을 또렷하게 하는 보조로 유지한다.

---

## 1. 문제

**탄과 탄 사이 경계가 흐릿하다.** 겹친 탄이 하나의 덩어리로 보여 패턴을 읽을 수 없다.

원인은 현재 렌더 셋업이다:

| 항목 | 현재 |
|---|---|
| 머티리얼 | 엔진 기본 `/Engine/BasicShapes/BasicShapeMaterial` |
| 색 | `Color` 파라미터를 **컴포넌트 단위** 1회 (`REBulletRenderSubsystem.cpp:46/65/87`) |
| 퍼인스턴스 데이터 | **0** — `SetCustomData` 호출 없음 |
| 렌더 프로세서가 넘기는 것 | **트랜스폼뿐** |

한 ISM 안의 모든 탄이 완전히 동일하고 시간에 따라 아무것도 변하지 않는다.

## 2. 왜 프레넬 림인가

경계 문제를 실제로 푸는 것은 하나뿐이다.

| 방법 | 비용 축 | 경계 해결 | 판정 |
|---|---|---|---|
| **프레넬 림** | GPU 픽셀 | **각 구체 실루엣이 밝아져 인접 탄 사이 경계가 반드시 생긴다** | **채택** |
| 툰 밴딩 단독 | GPU 픽셀 | ✗ — 같은 조명 아래 인접한 동일 구체는 밴드가 **똑같이** 잡혀 오히려 더 뭉친다 | 탈락 |
| 포스트프로세스 외곽선 | 화면 해상도 | △ — 같은 깊이로 맞닿은 탄끼리 선이 안 생기고, 5만 밀도에서 깊이 버퍼가 난잡 | 나중에 취향으로 |
| 인버티드 헐 | **인스턴스 2배 → 게임 스레드** | ○ | **탈락 (측정)** |

**인버티드 헐은 측정으로 배제된다.** #95 이후 병목은 게임 스레드다 — 50,000발에서 GT 12.08 vs GPU 7.69 ms. 인스턴스를 두 배로 만드는 방법은 이미 병목인 축을 직격한다. **반대로 머티리얼 복잡도(GPU 픽셀)는 지금 유일하게 남는 예산이다.**

## 2.5 실제 해법 — 인접 탄 색 교차 (체커보드)

**림만으로는 안 됐다.** 구현 후 육안 확인에서 "탄이 선으로 보인다"가 나왔고, 원인은 기하학이었다:

```
탄 간격 = BulletSpeed(200) × BossFireInterval(0.15) = 30 uu
탄 지름 = Sphere(100uu) × BulletScale(0.5)          = 50 uu
```

**30 < 50 — 연속된 탄이 지름의 40%씩 겹쳐 진행 방향으로 하나의 튜브가 된다.** 겹친 구체들의 합집합 실루엣은 튜브 옆면이므로, 그 가장자리를 밝히는 림은 선을 **더 선처럼** 만든다.

탄을 줄여 틈을 만드는 안(`BulletScale` 0.2)은 실제로 겹침을 없앴지만 **"너무 작다"로 기각**됐다 — 포트폴리오 캡처에서 탄막이 압도적으로 보여야 한다.

**채택: 겹침을 허용하고 인접 탄을 다른 색으로 칠한다.** 불투명 렌더에서 앞 구체가 뒤 구체를 가릴 때 색이 다르면 그 실루엣 경계가 보인다. 같은 색이면 안 보인다.

**축이 둘이다:**

| 축 | 겹침 원인 | 교차 기준 |
|---|---|---|
| 방사(진행 방향) | 연속 발사 회차 | 발사 회차 패리티 |
| 원주(링 내부) | 링 안 인접 탄 | 링 인덱스 |

한 축만 교차시키면 다른 축이 통짜로 뭉친다. 둘을 더해 홀짝을 낸다:

```
ColorSel = (발사회차 패리티 + 링 인덱스) & 1
```

**동기화:** 발사 회차 패리티는 `Multicast_FireDirect` 의 `ServerTime` 에서 뽑는다. 이 함수는 서버·클라 양쪽에서 같은 값으로 실행되므로 별도 복제가 필요 없다. 카운터를 따로 두면 멀티캐스트 유실 시 클라마다 색이 어긋난다.

**고정 시점:** 색은 스폰 시 `FBulletSimFragment` 에 고정한다. 매 프레임 나이에서 파생하면 같은 탄의 색이 주기적으로 뒤집혀 화면 전체가 깜빡인다.

따라서 커스텀데이터는 **2개**다 — `[0]` 스폰 팝, `[1]` 색 선택. 인스턴스당 연속으로 인터리브된다.

## 3. 셰이딩 모델 — 언릿 발광

**탄환은 조명을 받지 않는다.** `MSM_Unlit`.

| | 언릿 발광 (채택) | 라이팅 + 툰 밴딩 |
|---|---|---|
| 가독성 | 씬 조명과 무관하게 항상 같은 밝기 | 조명 방향에 따라 어두운 탄이 생겨 흔들림 |
| 비용 | 라이팅 계산 0 | 픽셀당 증가 |
| 장르 관습 | 고전 탄막의 탄은 발광체 | — |

**"카툰 쉐이더" 의향과 모순되지 않는다.** 탄환만 언릿이고 캐릭터·보스를 툰으로 가는 조합은 흔하고 자연스럽다. 밴딩은 밴딩할 조명이 있어야 성립하므로 **탄환에서는 성립할 수 없다** — #97 이슈 본문의 "툰 밴딩"은 이 사유로 탄환 범위에서 제외한다.

## 4. 머티리얼 구조

에셋: **`Content/Materials/M_REBullet`**. 현재 `Content/` 에 머티리얼 폴더가 없으므로 이 스펙이 위치를 정한다.

```
BaseColor (VectorParameter)  ─┐
                              ├─ Multiply ─┐
Fresnel × RimPower (Scalar)  ─┘            ├─ Multiply ─→ Emissive Color
                                           │
PerInstanceCustomData[0] (스폰 팝) ────────┘

Shading Model: Unlit          Blend Mode: Opaque
```

- **불투명 유지가 요구사항이다.** additive/translucent 로 가면 early-Z 가 사라져 겹친 만큼 오버드로우가 누적된다 — **현재 50,000발 상한을 지탱하는 것이 바로 불투명 렌더다.**
- 색은 기존대로 **ISM별 MID `Color` 파라미터**(직선탄 빨강 / 곡사탄 주황). 3-ISM 구조를 유지한다 — 드로우콜이 95로 고정이라 통합할 이유가 없다(YAGNI).

사용할 노드 클래스(엔진 실재 확인): `UMaterialExpressionFresnel`, `UMaterialExpressionVectorParameter`, `UMaterialExpressionScalarParameter`, `UMaterialExpressionPerInstanceCustomData`, `UMaterialExpressionMultiply`.

## 5. 저작 방식 — 스크립트 부트스트랩 + 수동 튜닝

에디터 GUI 없이 머티리얼을 만들 수 있다. `PythonScriptPlugin`(엔진 내장)과 `MaterialEditingLibrary`(`CreateMaterialExpression` / `ConnectMaterialExpressions` / `ConnectMaterialProperty` / `RecompileMaterial`)가 있다.

**역할 분담을 명확히 한다:**

| 단계 | 담당 |
|---|---|
| 동작하는 머티리얼 초안 생성 + C++ 배선 | 스크립트 (`UnrealEditor-Cmd.exe -run=pythonscript`) |
| 림 굵기·발광 강도·색 등 **수치 튜닝** | 사람이 에디터에서 |

**스크립트는 부트스트랩이지 정본이 아니다.** 생성 후 에디터에서 수동 수정하면 스크립트는 낡는다. 스크립트 파일에 그 사실을 주석으로 명시하고, `.uasset` 이 정본이 된다.

## 6. 스폰 팝 — 커스텀데이터 float 1개

`SetNumCustomDataFloats(1)`.

```
직선탄:  Age = REBulletPattern::BulletLifetimeSec() - FBulletSimFragment::Lifetime
곡사탄:  Age = FArcBulletFragment::Elapsed
Pop = saturate(Age / PopDuration)          // PopDuration = 0.1s (머티리얼 밖 상수, 튜닝 대상)
```

`FBulletSimFragment::Lifetime` 은 **잔여시간**이며 `REBulletSimProcessor.cpp:42` 에서 `Dt` 만큼 감소한다. 스폰 시 `REBulletSpawnSubsystem.cpp:44` 가 설정값으로 초기화한다. **새 프래그먼트가 필요 없다.**

### 수명 페이드는 넣지 않는다

죽기 직전 탄이 흐려지면 **여전히 치명적인데 사라지는 중으로 오독된다.** 탄막에서 히트박스 가독성은 공정성 문제다. 탄은 끝까지 같은 밝기를 유지하다 즉시 사라진다.

스폰 팝은 반대다 — 태어나는 순간만 밝기/크기가 솟아오르므로 **생성이 눈에 띄면서 치명성은 항상 명확하다.**

## 7. 배선

- ISM 생성부(`REBulletRenderSubsystem`)에서 `SetNumCustomDataFloats(1)` 1회
- 렌더 프로세서(`REBulletRenderProcessor` / `REArcRenderProcessor`)에서 `Xf` 와 나란히 `TArray<float> Pop` 을 채우고 **`SetCustomData(0, M-1, Pop, false)` 한 번**
- dirty 마크는 기존 `BatchUpdateInstancesTransforms(..., bMarkRenderStateDirty=true, ...)` 가 담당한다 — 커스텀데이터를 먼저 쓰고 트랜스폼을 나중에 쓴다

`SetCustomData(int32 Start, int32 End, TConstArrayView<float>, bool)` 은 실패 시 `ensureMsgf` 로 크게 터진다(`InstancedStaticMesh.cpp:3950/3955`). #95 검증에서 문제가 됐던 **무음 실패가 아니다.**

## 8. 성능 위험과 철회 기준

`SetCustomData` 는 memcpy 1회지만 **dirty 표시가 인스턴스 루프**다(`InstancedStaticMesh.cpp:3967` — `PrimitiveInstanceDataManager.CustomDataChanged(i)` 를 범위 전체에 반복). 50,000회 반복이므로 **#95 에서 없앤 것과 같은 형태의 비용이 일부 돌아올 수 있다.**

**철회 기준을 미리 정한다: 재측정에서 60fps 상한이 45,000발 아래로 떨어지면 스폰 팝을 버린다.** 경계 가독성(림)이 본 목적이고 팝은 부가다. 림만으로도 이 스펙의 문제 정의는 충족된다.

## 9. 검증

| 게이트 | 기준 |
|---|---|
| 빌드 | `Project_REEditor` / `Project_REServer` 양쪽 `Result: Succeeded` |
| 데디 | `scripts/dedi-verify.ps1` 전 항목 PASS |
| 성능 | `scripts/profile.ps1` 로 **상한 N 을 다시 찾는다**(p99 ≤ 16.6 ms 인 최대 탄환 수). §8 철회 기준과 대조 |
| 머티리얼 적용 | 런 로그 또는 지표로 `M_REBullet` 이 실제 사용되는지 확인 — 엔진 기본 머티리얼로 조용히 폴백하면 안 된다 |

**경계 가독성은 자동 판정이 불가능하다.** 저장소에 스크린샷 수단이 없다(`FScreenshotRequest` 계열 0건). **사람 눈 확인이 완료 조건이며, 수치 게이트는 "상한이 안 깎였다"만 보증한다.** 이 스펙은 그 한계를 감수한다 — 스크린샷 하네스 구축은 별도 작업이다.

## 10. 범위 밖

- **마커 ISM** — 바닥 디스크라 성격이 다르다. 현행 유지
- **ISM 통합** — 드로우콜 95 고정이라 통합 이유 없음
- **포스트프로세스 외곽선** — 경계는 림으로 먼저 풀린다. 스타일 목적이면 별건
- **캐릭터·보스 툰 셰이딩** — 조명 요구가 다르다
- **수명 페이드·탄별 색 변주** — §6 사유로 제외. 커스텀데이터 배선이 생기므로 나중에 float 을 늘려 추가 가능
- **탄환 메시 교체** — 리소스 전면 교체 계획의 일부. 메시가 바뀌어도 이 머티리얼은 그대로 쓰인다
- **스크린샷 검증 하네스** — §9 의 한계를 없애려면 필요하지만 별도 작업

## 참고

- `docs/profiling/M6-gpu-breakdown.md` 부록 A — GT/GPU 예산과 50,000발 상한 근거
- `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp` — ISM 3개 생성부
- `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` / `REArcRenderProcessor.cpp` — 커스텀데이터가 붙을 곳
- #95 — 배치 인스턴스 갱신. §8 위험의 배경
