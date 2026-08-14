# M6 GPU 병목 진단 설계 (#50 1단계)

**작성일:** 2026-08-14 · **이슈:** #50 · **마일스톤:** M6

> 한 줄: **#50이 지목한 병목(bloom/오버드로우)은 측정된 적이 없고 코드와 어긋난다. 고치기 전에 GPU 38 ms가 어디서 나오는지 실측해 리포트로 낸다. 이 문서의 범위는 진단뿐 — 수정은 포함하지 않는다.**

---

## 1. 왜 진단을 분리하나

#50 본문은 병목을 이렇게 지목한다:

> 삼각형은 사소(52.8K prim)한데 GPU 27 ms — 즉 지오메트리가 아니라 **오버드로우/필**
> - 탄환이 화면에서 크고 **강한 emissive → bloom** 대면적
> - 인스턴스 컬링 미검토 (plain ISM vs HISM)

이 진단은 스크린샷 추론이고, **코드와 대조하면 전제가 무너진다.**

| #50 / M3 리포트 주장 | 코드 실제 |
|---|---|
| Mass 저폴리 구 / Actor 고폴리 구 | 같은 메시 `/Engine/BasicShapes/Sphere.Sphere` (`REBulletRenderSubsystem.cpp:39`, `REBulletActor.cpp:26`) |
| 탄환 화면 크기 Mass 큼 / Actor 작음 | 같은 스케일 `0.5`. `REBulletActor.cpp:11` 이 Mass 값을 참조한다고 주석에 명시 |
| 강한 emissive → bloom | 같은 `BasicShapeMaterial` 공유. 소스·`Config/*.ini` 에 bloom/post-process 설정 **0건** |
| 그림자 차이 | 양쪽 다 `CastShadow` 를 끄지 않는다 |

메시·스케일·머티리얼·탄환 수가 전부 같은데 GPU가 **38.61 vs 5.93 ms**다. 더구나 **삼각형이 적은 쪽(Mass 52.8K vs Actor 9.6M)이 6.5배 느리다.** 필 때문이라면 같은 크기·같은 재질에서 이 역전이 나올 수 없다.

`52.8K ÷ 1920tri(구 LOD0) ≈ 27` — Mass 쪽 가시 인스턴스가 사실상 27개였을 가능성이 있다. 그렇다면 GPU 38 ms는 **탄환을 그리는 비용이 아니다.** 무엇인지는 아무도 재보지 않았다.

**#88이 같은 형태였다.** 이슈 본문 가설이 셋 중 하나에만 맞았고 실측이 진짜 원인 셋을 따로 짚었다. 여기서 추론대로 Niagara 교체(M6 본과제, 큰 작업)에 들어갔다가 원인이 딴 데 있으면 통째로 헛일이 된다.

## 2. M3 표는 before 로 쓸 수 없다

`docs/profiling/M3-mass-vs-actor.md` 측정일은 **2026-07-16**이다. 그 뒤에 전부 바뀌었다:

| 변경 | 시점 | 측정에 미치는 영향 |
|---|---|---|
| `BulletLifetime` 3 → 15 | 07-17 (`c70d2aa`) | 탄환 비행거리 900 → 4500 uu. **화면 분포·오버드로우 면적이 완전히 다르다** |
| `BossFireInterval` 0.1 → 0.15 | 07-17 | 링 간격 변화 |
| 런처 바이너리 → 소스 빌드 엔진 | M4 | 렌더 기본값·엔진 리비전 상이 (5.8.0 → 5.8.1) |
| 하네스 3중 결함 발견·수정 | #88 (08-14) | M3 당시 캡처 창의 상당 부분이 **부팅·셰이더 컴파일 구간**이었을 수 있다 |

**베이스라인부터 다시 잰다.** M3 표는 아키텍처 결론(게임 스레드·드로우콜 축)만 유효하고, GPU·프레임타임 숫자는 참고치로만 인용한다.

## 3. 범위

**포함:** GPU 시간의 귀속을 숫자로 밝히고 리포트 1건을 낸다.

**제외 (이 문서 밖):**
- 어떤 수정도 하지 않는다 — HISM 전환, 크기/emissive 조정, Niagara 교체 전부 다음 단계
- 목표 부하 확정 — 1600·5000 둘 다 재고, 어디까지 고칠지는 숫자를 보고 정한다
- 밸런스(`BulletLifetime=15` 등)는 게임플레이 결정이라 건드리지 않는다

## 4. 측정 대상 부하

| 부하 | 근거 |
|---|---|
| **1600발** | 실제 게임플레이 정상상태. `BulletsPerShot=16` × (`BulletLifetime=15` / `BossFireInterval=0.15`) = 16 × 100 |
| **5000발** | M3 스트레스 벤치마크. 최악 상황·영상 촬영 안전마진 |

1600은 `re.Bullets.Count 1600`(클로즈드루프)으로 만든다. 게임플레이는 오픈루프지만 정상상태 탄환 수와 공간 분포가 같으므로 GPU 측정에는 등가다.

## 5. 방법

### 5.1 CVar A/B 소거법 (본선)

기능을 하나씩 끄고 GPU가 얼마나 빠지는지 잰다. **게임 코드 변경은 0이다** — 엔진 내장 CVar만 주입한다. 하네스 쪽은 CVar를 밖에서 넣을 수단이 없어 스크립트 파라미터 두 개가 필요하다(§6).

| 구성 | 주입 CVar | 답하는 질문 |
|---|---|---|
| `base` | 없음 | 기준값 |
| `nobloom` | `r.BloomQuality 0` | #50이 지목한 bloom이 실제로 범인인가 |
| `noshadow` | `r.ShadowQuality 0` | 5000 인스턴스의 그림자 뎁스 패스인가 |
| `halfres` | `r.ScreenPercentage 50` | 순수 필레이트인가 (픽셀 1/4 → 필 바운드면 GPU도 약 1/4) |

세 CVar 모두 UE 5.8 실재 확인:
`r.BloomQuality`(`PostProcessBloomSetup.cpp:189`), `r.ShadowQuality`(`ShadowRendering.cpp:260`), `r.ScreenPercentage`(`LegacyScreenPercentageDriver.cpp:38`).

**소거법을 본선으로 두는 이유:** 완전 자동화되고, `frames.csv` 의 GPU 컬럼이 그대로 리포트 표가 되며, "무엇이 아닌지"를 확실히 배제한다. 이 프로젝트에는 단위 테스트 프레임워크가 없어 검증이 로그·산출물 기반인데, 소거법은 그 방식에 그대로 맞는다.

### 5.2 `frames.csv` 의 패스별 렌더스레드 분해 (무료 보강)

계획 수립 중 확인한 사실: `frames.csv` 는 이미 315개 컬럼을 담고 있고, 그중에 **패스별 렌더스레드 시간이 들어 있다.**

| 컬럼 | 답하는 질문 |
|---|---|
| `Exclusive/RenderThread/RenderBasePass` | 불투명 지오메트리 |
| `Exclusive/RenderThread/RenderShadows` | 그림자 |
| `Exclusive/RenderThread/RenderPostProcessing` | post/bloom |
| `Exclusive/RenderThread/RenderTranslucency` | 반투명 |
| `Exclusive/RenderThread/UpdateGPUScene`, `.../UpdatePrimitiveInstances`, `.../ConsolidateInstanceDataAllocations` | **인스턴스 갱신 — §10의 "셋 다 아니면" 가설을 직접 잰다** |
| `GPUSceneInstanceCount`, `RHI/DrawCalls`, `RHI/PrimitivesDrawn` | 인스턴스·드로우콜 실측 |

이 컬럼들은 **렌더스레드 CPU 시간**이지 GPU 시간이 아니다. 따라서 GPU 귀속은 여전히 소거법(§5.1)이 낸다. 다만 이 컬럼들이 **어느 패스에 작업이 몰려 있는지**를 추가 도구 없이 보여주므로, 소거법 결론의 교차 검증으로 리포트에 함께 싣는다.

**함정:** `frames.csv` 는 끝에 **헤더 1행 + 메타 1행이 더 붙는다**(`[HasHeaderRowAtEnd]`). 그대로 읽으면 두 행이 데이터로 섞여 통계가 깨진다.

### 5.3 Insights GPU 트랙 (확인용)

`profile.ps1` 의 트레이스 채널에 `gpu` 를 더한다 — `-trace=cpu,frame,counters,gpu`. **한 단어이고 트레이스는 이미 뜨고 있으므로 추가 비용이 사실상 0이다.**

소거법이 "무엇이 아닌지"를 좁히면, GPU 트랙은 패스별 시간을 직접 보여줘 "무엇인지"를 확정한다. 읽기는 Insights GUI라 자동 검증이 안 되므로 **본선이 아니라 확인용**이다. 리포트에는 소거법 숫자를 싣고, GPU 트랙에서 확인한 패스 이름을 근거로 병기한다.

### 5.4 채택하지 않은 것

**`ProfileGPU` 로그 덤프.** 단일 프레임 계층 분해라 정확하지만, 정상상태에서 쏘려면 C++ 훅이 필요하다(#88이 만든 채움 완료 지점 재사용). 5.1~5.3 이 답하면 불필요한 코드다. 소거법이 결론을 못 내면 그때 추가한다.

## 6. 하네스 변경

하네스에 네 가지가 부족하다. **전부 작고, 진단이 끝나도 남아서 쓸모 있다.**

### 6.1 `-ExtraExec` 파라미터

현재 `profile.ps1` 은 `$ExecCmd` 를 내부에서만 조립해 외부에서 CVar를 못 넣는다. 스위프 구성마다 CVar가 달라야 하므로 추가한다.

```powershell
# param 블록
[string]$ExtraExec = '',
```

```powershell
# 기존 $Ki 처리 바로 뒤
if ($ExtraExec) { $ExecCmd += ",$ExtraExec" }
```

`-Ki` 가 이미 같은 방식(`$ExecCmd += ",re.Bullets.SpawnKi $Ki"`)으로 붙으므로 기존 패턴 그대로다.

### 6.2 `-Label` 파라미터

스위프는 같은 탄환 수로 여러 번 돌기 때문에 run 디렉터리가 타임스탬프로만 갈린다. 어느 폴더가 어느 구성인지 사람이 기억해야 하는 상태로는 10회 런을 신뢰할 수 없다.

```powershell
[string]$Label = '',
```

```powershell
$Suffix = if ($Label) { "_$Label" } else { '' }
$RunDir = Join-Path $Root "Saved\Profiling\RE_${Tag}_${Bullets}${Suffix}_${Stamp}"
```

### 6.3 트레이스 채널

`-trace=cpu,frame,counters` → `-trace=cpu,frame,counters,gpu` (§5.3).

### 6.4 `scripts/profile-stats.ps1` (신규)

`frames.csv` 에서 통계를 뽑는 수단이 **없다.** M3 는 즉석 계산이었고, 그 방식으로는 10회 × 12컬럼을 신뢰할 수 없다. 게다가 §5.2의 말미 2행 함정을 매번 손으로 피해야 한다.

run 디렉터리들을 받아 컬럼별 mean/p99 를 마크다운 표 행으로 출력하는 작은 스크립트를 만든다. 리포트 표가 **손 계산이 아니라 재현 가능한 명령의 출력**이 된다.

`-Label` 로 붙인 구성 이름이 run 디렉터리에 남으므로 표의 행 이름도 자동으로 맞는다.

## 7. 스위프 행렬

| # | 경로 | 탄환 | 구성 | 커맨드 |
|---|---|---|---|---|
| 1 | Mass | 1600 | base | `profile.ps1 -Bullets 1600 -Label base` |
| 2 | Mass | 1600 | nobloom | `profile.ps1 -Bullets 1600 -Label nobloom -ExtraExec "r.BloomQuality 0"` |
| 3 | Mass | 1600 | noshadow | `profile.ps1 -Bullets 1600 -Label noshadow -ExtraExec "r.ShadowQuality 0"` |
| 4 | Mass | 1600 | halfres | `profile.ps1 -Bullets 1600 -Label halfres -ExtraExec "r.ScreenPercentage 50"` |
| 5 | Mass | 5000 | base | `profile.ps1 -Bullets 5000 -Label base` |
| 6 | Mass | 5000 | nobloom | `profile.ps1 -Bullets 5000 -Label nobloom -ExtraExec "r.BloomQuality 0"` |
| 7 | Mass | 5000 | noshadow | `profile.ps1 -Bullets 5000 -Label noshadow -ExtraExec "r.ShadowQuality 0"` |
| 8 | Mass | 5000 | halfres | `profile.ps1 -Bullets 5000 -Label halfres -ExtraExec "r.ScreenPercentage 50"` |
| 9 | Actor | 1600 | base | `profile.ps1 -Actor -Bullets 1600 -Label base` |
| 10 | Actor | 5000 | base | `profile.ps1 -Actor -Bullets 5000 -Label base` |

9·10은 대조군이다 — 같은 메시·스케일·수인데 GPU가 6.5배 갈렸다는 M3의 역전이 현행 코드·현행 하네스에서도 재현되는지 먼저 확인해야 나머지 해석이 성립한다. **재현되지 않으면(예: #88 수정으로 사라졌으면) 그 자체가 결론이고 스위프 해석이 달라진다.**

## 8. 산출물

`docs/profiling/M6-gpu-breakdown.md` — `M3-mass-vs-actor.md` 와 같은 형식.

담을 것:

1. **재측정 베이스라인** — 1600·5000, Mass·Actor, Frame/GT/RT/GPU. M3 표와의 차이와 그 사유(§2)
2. **소거 표** — 구성별 GPU와 base 대비 감소분. 어느 기능이 몇 ms를 먹는지
3. **귀속 결론** — 한 문장. "GPU N ms 중 M ms 는 X 다"
4. **패스별 교차 검증** — `frames.csv` 의 렌더스레드 패스 컬럼(§5.2)과, 필요하면 Insights GPU 트랙(§5.3)에서 확인한 패스 이름
5. **다음 단계 권고** — 소거 결과가 가리키는 수정과, Niagara 교체가 그 원인에 유효한지에 대한 판단

## 9. 성공 기준

- 스위프 10회가 전부 유효한 `frames.csv` 를 낸다 (탄환 수가 목표 ±5%, 캡처가 정상상태에서 시작)
- GPU 시간의 **과반이 어느 기능에 귀속되는지 숫자로 말할 수 있다**
- 리포트만 읽고 다음 단계(무엇을 고칠지)를 정할 수 있다 — 추가 측정 없이

**명시적 비목표:** 이 단계에서 GPU가 빨라지지 않는다. 아무것도 최적화하지 않는다.

## 10. 실패 시

소거법 셋 다 GPU를 유의미하게 못 낮추면 — 즉 bloom·그림자·필레이트 어느 것도 아니면 — 원인은 인스턴스 처리(컬링/버퍼 갱신) 쪽이다. 그때 §5.4의 `ProfileGPU` 훅을 추가해 패스 계층을 직접 뜬다. 리포트에 그 사실과 근거를 적고 후속 이슈로 넘긴다.

## 참고

- `docs/profiling/M3-mass-vs-actor.md` — 이전 측정. §2의 사유로 GPU 숫자는 무효
- `docs/guides/profiling.md` — 하네스 사용법·고정 조건 (현행 정본)
- #88 — 하네스 3중 결함 수정. 이 진단이 성립하는 전제
- #46 — M3 측정 리포트를 낸 이슈. 형식의 원본
