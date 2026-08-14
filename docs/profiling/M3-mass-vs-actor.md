# M3 — Mass vs Actor 스케일 프로파일 리포트

**측정일:** 2026-07-16 · **엔진:** UE 5.8 · **빌드:** `UnrealEditor.exe -game` (Development)
**하네스:** `scripts/profile.ps1` · **조건 고정:** `docs/guides/profiling.md`

> 한 줄 결론: **동일 5000발에서 Mass는 게임 스레드 4.5 ms, Actor는 16.2 ms — 3.6배. 탄환을 100 → 5000으로 90배 올려도 Mass GT는 2.7 → 4.5 ms로 평평하지만 Actor는 2.8 → 16.2 ms로 선형 붕괴한다. 드로우콜은 Mass 386 vs Actor 5053. 이것이 "왜 Mass를 썼나"의 숫자다.**

> 갱신(#51): Mass 스폰이 목표를 못 채우던 언더슛(5000→3352)을 클로즈드루프 스폰으로 고쳐, 이제 **두 경로가 같은 실제 탄환 수(Mass 4999 vs Actor 5000)**에서 공정 비교된다. 아래 표는 재측정 값.

> **⚠️ 무효 갱신(#50, 2026-08-14): 아래 표의 절대 수치는 쓰지 마라 — 렌더 축(GPU·프레임타임·RenderThread)뿐
> 아니라 드로우콜 수도 포함이다.** 이 측정은 캡처가 엔진 부팅 시점에 시작하던 시절의 것이라, 17.9초 창 중
> 13.3초가 레벨 로드·셰이더 컴파일 구간이었다(#88이 밝히고 고쳤다). 재측정 결과 Mass 5000은
> GPU **38.61 → 5.28 ms**, 프레임 **39.54 → 6.18 ms**, 드로우콜 **386 → 96** 이다.
> `BulletLifetime` 을 당시 값(3.0)으로 되돌려도 재현되지 않으므로 밸런스 변경 탓이 아니다.
>
> **다만 이 리포트의 결론 자체는 무효가 아니다.** "Mass 는 게임 스레드가 평평하고 드로우콜이 훨씬 적다"는
> 방향은 재측정에서 오히려 강해졌다 — 현재 Mass 5000 은 드로우콜 96 / 프레임 6.18 ms 인 반면
> Actor 5000 은 드로우콜 3,358 / GT 16.99 ms 로 60fps 게이트를 자력으로 넘지 못한다.
> **숫자는 갈아 끼우고 결론은 유지하라.** 현행 정본: `docs/profiling/M6-gpu-breakdown.md`

---

## 0. 읽기 전에 — 이 비교의 통제 범위

측정을 유효하게 만들기 위해 이번 이슈에서 하네스를 세 군데 고쳤다(§5). 그 과정에서 **두 경로의 렌더 셋업이 통제돼 있지 않다**는 것을 발견했다:

| | Mass (ISM) | Actor |
|---|---|---|
| 메시 | 엔진 저폴리 구 | 엔진 **고폴리** 구 |
| 5000발 삼각형 수 | **52.8K** | **9.6M** |
| 탄환 화면 크기 / bloom | 큼 / 강함 | 작음 / 약함 |

→ **총 프레임타임·GPU 시간은 아키텍처(Mass vs Actor)가 아니라 이 렌더 차이가 지배한다.** 두 경로를 공정하게 가르는 축은 **게임 스레드 시간**과 **드로우콜 수**다. 결론은 그 두 축으로 낸다. (렌더 통제 재측정은 범위 밖 — 렌더는 M6에서 Niagara로 교체될 플레이스홀더다. §6 후속 이슈.)

---

## 1. 측정표 (6회, 워밍업 120프레임 제외 · 측정 600프레임)

`re.Bullets.Count`(Mass) / `re.ActorBullets.Count`(Actor)를 노브로 100/1000/5000 지정. **"실제 유지"는 `run.log`의 정상상태 중앙값** — 클로즈드루프 스폰(#51) 이후 두 경로 모두 목표 ±5% 유지.

| 경로 | 노브 | 실제 유지 | Frame mean | Frame p99 | **GameThread** | RenderThread | GPU |
|---|---:|---:|---:|---:|---:|---:|---:|
| Mass  | 100  | 103  | 4.96  | 6.91  | **2.71**  | 4.95  | 3.86 |
| Mass  | 1000 | 1016 | 9.67  | 16.85 | **3.17**  | 9.66  | 8.67 |
| Mass  | 5000 | 4999 | 39.54 | 61.59 | **4.45**  | 39.53 | 38.61 |
| Actor | 100  | 100  | 4.96  | 7.03  | **2.84**  | 4.96  | 3.86 |
| Actor | 1000 | 1000 | 6.25  | 9.78  | **5.22**  | 6.15  | 4.19 |
| Actor | 5000 | 5000 | 16.71 | 25.51 | **16.20** | 15.25 | 5.93 |

(단위 ms. 컬럼은 엔진 CSV 프로파일러 `frames.csv` 실측.)

### 게임 스레드 — 이 리포트의 핵심

```
탄환 수 →      100     1000     5000
Mass GT        2.71     3.17     4.45     ← 거의 평평
Actor GT       2.84     5.22    16.20     ← 선형 붕괴
```

Mass는 탄환이 90배 늘어도 게임 스레드가 1.7 ms만 오른다. Actor는 같은 구간에서 13.4 ms 오른다. **동일 5000발에서 게임 스레드 3.6배(4.45 vs 16.20 ms) 차이.** Actor 5000은 프레임이 GT에 완전히 묶였다(GT 16.20 ≈ Frame 16.71). Mass 5000은 GT가 4.45 ms로 비어 있고 프레임(39.54 ms)은 전부 렌더 GPU다(§5).

---

## 2. 프로세서 3분해 (Mass, CSV 컬럼)

`RE_BulletSim/Render/Hit` 스코프에 `CSV_SCOPED_TIMING_STAT` 계측(§5). 컬럼 이름 자체가 **스레드 배치의 증거**다:

| CSV 컬럼 | 스레드 | Mass 5000 mean | p99 |
|---|---|---:|---:|
| `REBullet/AllWorkers/BulletSim`   | **워커 스레드** | 0.04 | 0.08 |
| `REBullet/GameThread/BulletRender`| 게임 스레드 | 0.74 | 1.33 |
| `REBullet/GameThread/BulletHit`   | 게임 스레드 | 0.10 | 0.23 |

**3개 프로세서 총합 = 0.88 ms.** 5000발 부하에서 Mass 탄막 시뮬 자체는 프레임(39.5 ms)의 ~2%다. 이동(Sim)은 워커 스레드로 완전히 빠졌다 — 게임 스레드가 비어 있는 이유.

---

## 3. 드로우콜 — ISM 배칭 (stat unit @ 5000)

| | Mass 5000 | Actor 5000 |
|---|---:|---:|
| **Draws** | **386** | **5053** |
| Prims | 52.8K | 9.6M |

Mass ISM은 인스턴스 수천 개를 한 줌의 드로우콜로 묶는다. Actor는 탄환 1개당 드로우콜 1개. **드로우콜 13배 차이** — 게임 스레드/RHI 부담의 아키텍처적 근거.

---

## 4. 스크린샷

| Mass 5000 | Actor 5000 |
|---|---|
| ![Mass stat unit](img/statunit_mass_5000.png) | ![Actor stat unit](img/statunit_actor_5000.png) |
| ![Mass Insights](img/insights_mass_5000.png) | ![Actor Insights](img/insights_actor_5000.png) |

Mass stat unit의 나선은 클로즈드루프 스폰(#51) 이후 목표 5000발을 꽉 채운다. Insights 상단 프레임 스트립: Mass는 균일한 GPU 바운드 블록, Actor는 스파이키한 ~16 ms(게임 스레드 `Tick_Engine` 39 ms 지배).

---

## 5. 게이트 판정 & 병목 지목

- **게이트("Mass 5000 평균 frame time ≤ 16.6 ms"): 총 프레임타임 39.54 ms로 미달** (5000발 꽉 채운 값 — 언더슛 3352발이던 이전 28 ms보다 오히려 높다).
- 단, 병목은 **Mass 프로세서(0.88 ms)가 아니다.** 분해가 지목하는 병목은 **플레이스홀더 ISM 렌더의 GPU(38.61 ms)** — 저폴리(52.8K prim)인데도 큰 탄환 + 강한 emissive/bloom에 의한 오버드로우/필 바운드.
- 이슈 #46의 "미달 시" 규약대로 **이 이슈에서 최적화하지 않고** 별도 이슈로 분리한다(§6).
- 게임 스레드/CPU 관점 목표는 통과: Mass 5000발 GT 4.45 ms (60fps 예산 16.6 ms의 27%).

## 6. 후속 이슈 (측정으로 지목)

1. **#50 ISM 렌더 GPU 최적화** (M6, 열림) — Mass 5000 병목 = GPU 38.6 ms. bloom/탄환 크기/HISM 컬링/머티리얼 검토. M6 Niagara 렌더가 근본 대체.
2. **#51 Mass 스폰 언더슛** (M3, **해결**) — 오픈루프 `MakeSpiralForLiveCount`가 목표 5000 → 실제 3352. Boss에 라이브 카운트(ISM) 피드백 **클로즈드루프 적분 컨트롤러**(fill-phase 안티와인드업 + 소수부 누산)를 넣어 목표 ±5% 유지로 교정. 위 표가 재측정 결과.

---

## 7. 재현

```powershell
scripts/profile.ps1 -Bullets 100          # Mass
scripts/profile.ps1 -Bullets 1000
scripts/profile.ps1 -Bullets 5000
scripts/profile.ps1 -Bullets 100  -Actor  # Actor 베이스라인
scripts/profile.ps1 -Bullets 1000 -Actor
scripts/profile.ps1 -Bullets 5000 -Actor
```

산출물: `Saved/Profiling/RE_<경로>_<노브>_<스탬프>/` → `frames.csv`(프레임별 ms + 프로세서 분해), `trace.utrace`(Insights), `run.log`(실제 유지 탄환 수).

측정 조건(실 RHI `-windowed 1280x720`, 워밍업 120 컷, `re.Profiling.KeepFiring 1`)은 `docs/guides/profiling.md` 참조.
