# M7 보스 패턴별 프레임 p99 (2026-08-23)

패턴 15종 전부에 대해 `scripts/profile.ps1` 로 p99 를 실측했다. **전부 60fps 게이트(16.6ms) 안.**

이전까지 이 세션에서 보고하던 수치는 `stat unit` **순간 표본**이었다 — p99 와 척도가 달라
게이트 판정 근거로 쓸 수 없었다. 이 문서가 그 자리를 대체한다.

## 측정 조건

```
scripts/profile.ps1 -Bullets -1 -Frames 720 \
  -ExtraExec "re.Fx.Explosions 1,re.Debug.BossPattern <N>" -Label pNN_<이름>
```

| 항목 | 값 | 왜 |
|---|---|---|
| `-Bullets -1` | 오픈루프 | 클로즈드루프(`re.Bullets.Count ≥ 0`)는 M3 측정 전용이다. 실제 게임플레이 발수(ini `BulletsPerShot`)를 재야 한다 |
| `re.Fx.Explosions 1` | 폭발 켬 | 하네스 기본이 0인데 **곡사 패턴 비용의 대부분이 착지 폭발**이다. 끄고 재면 실물이 아니다 |
| 해상도 | 1280×720 (하네스 고정) | 별도로 1080p 대조를 했다 — 아래 참조 |
| 캡처 | 720프레임, 정상상태 진입 후 | 고정 패턴 3번째 페이즈부터 (`ProfileWarmupPhases`) |
| 페이즈 구조 | 유지 (발사 + Rest) | 폭풍의 스윕 인덱스처럼 페이즈 단위로 리셋되는 상태가 있어, 우회하면 실제 플레이와 다른 것을 잰다. Rest 프레임은 값이 싸서 p99(상위 1%)에 영향이 사실상 없다 |

## 하네스가 막혀 있던 것 — 이번에 뚫음

1. **`re.Profiling.KeepFiring` 이 `BeginPhase` 앞단에서 Spiral 로 고정하고 early-return** 했다.
   BossPattern 을 뭘로 주든 Spiral 이 측정됐다. → 패턴을 고정했으면(`re.Debug.BossPattern ≥ 0`)
   이 우회를 타지 않게 했다. 미지정(-1)이면 기존 M3 동작 그대로다.
2. **캡처 시작 신호(`REBulletsFilled`)가 Spiral 클로즈드루프 안에서만 발화**했다. 다른 패턴은
   캡처가 영영 시작되지 않았다. → 고정 패턴의 N번째 페이즈에서도 발화하게 했다.
3. 하네스가 `re.Fx.Explosions 0` 을 하드코딩한다. → `-ExtraExec` 가 뒤에 붙으므로 1로 덮었다
   (스크립트 수정 없음).

## 결과 (720p)

| idx | 패턴 | mean | **p99** | max | 게이트 16.6 |
|---:|---|---:|---:|---:|---|
| 6 | LissajousStorm | 8.76 | **14.95** | 28.53 | OK ← 최악 |
| 7 | BezierVortex | 10.53 | 13.40 | 591.86 | OK |
| 3 | ArtilleryStorm | 8.75 | 12.92 | 17.20 | OK |
| 13 | Spirograph | 7.70 | 12.32 | 23.85 | OK |
| 11 | RoseField | 6.99 | 11.46 | 17.65 | OK |
| 14 | MicroMissile | 8.08 | 11.33 | 16.23 | OK |
| 12 | AerialDome | 7.66 | 10.40 | 15.27 | OK |
| 4 | RoseEnvelope | 6.49 | 8.61 | 13.17 | OK |
| 0 | Spiral | 6.46 | 8.24 | 13.53 | OK |
| 5 | Cardioid | 5.84 | 8.18 | 13.39 | OK |
| 8 | StarBloom | 6.04 | 7.86 | 14.90 | OK |
| 1 | Fan | 5.97 | 7.79 | 11.51 | OK |
| 9 | LemniscateBloom | 5.74 | 7.61 | 13.89 | OK |
| 10 | SuperformulaBloom | 5.70 | 7.37 | 14.18 | OK |
| 2 | Artillery | 5.30 | 7.05 | 12.12 | OK |

`max` 열의 큰 값(591.86 등)은 캡처 구간에 섞인 일회성 히치다. p99 는 상위 1%를 잘라내므로
영향받지 않지만, 히치 자체가 없다는 뜻은 아니다.

## 임계 경로는 렌더 스레드다 (게임 스레드가 아니다)

최악 패턴(LissajousStorm, 튜닝 전) 분해:

```
Frame p99 14.95   ←   GT 9.29    RT 13.43    GPU 8.47
```

M6 기준선(직선탄 50,000발)은 GT 병목이었는데, **곡사 패턴은 RT 병목**이다. RT 는 드로우콜
제출이라 해상도가 아니라 오브젝트 수(마커 ISM + 폭발 Niagara 컴포넌트)에 비례한다.
그래서 곡사 패턴의 유일한 실질 손잡이는 **착지율**(`Count / FireInterval`)이다.

## 1080p 대조

RT 병목이면 해상도를 올려도 Frame 이 크게 안 움직여야 한다. 확인:

| 패턴 | 720p Frame p99 | 1080p Frame p99 | GPU p99 |
|---|---:|---:|---|
| LissajousStorm | 14.95 | 15.55 | 8.47 → 10.08 |
| BezierVortex | 13.40 | 13.12 | 7.06 → 8.20 |
| ArtilleryStorm | 12.92 | 12.07 | 6.46 → 7.51 |

GPU 는 1.2~1.6ms 오르지만 임계 경로가 아니라 Frame 은 거의 안 움직인다. 뒤 둘은 오히려
내려갔는데, 그것이 **런 간 노이즈 ±0.9ms** 의 크기를 알려준다.

## LissajousStorm 튜닝 (이 측정으로 잡은 것)

1080p 15.55 는 게이트까지 1.05ms 인데 노이즈가 ±0.9ms 다 — 방어됐다고 말할 폭이 아니다.
`LissaFireInterval` 1.0 → 1.3 (착지율 150 → 115/s):

| | Frame p99 (1080p) |
|---|---:|
| 1.0 (150/s) | 15.55 |
| 1.3 (115/s) 런1 | **12.07** |
| 1.3 (115/s) 런2 | **12.07** |

두 런이 동일하다. 여유 4.5ms(27%). 겹수는 2.5 → 1.9 로 줄지만 RoseField·Spirograph 가
겹수 1로도 곡선이 읽히므로 가독성은 유지된다(창 캡처로 확인).

**튜닝 후 최악은 BezierVortex 13.40 (720p) / 13.12 (1080p)** — 여유 약 3.2ms.

## 인원수 영향

인원에 비례하는 항은 히트 판정 하나다:

```cpp
for (each bullet) { for (each target) { DistSquaredXY } }   // O(탄 × 인원)
```

- 직선탄 히트(`AllNetModes`): 이번 패턴 최대 ~4,800발 × 2인 = 9,600회. 기준선 50,000발의 1/10
- 곡사탄 히트(`Standalone | Server`): **클라에서는 안 돈다**
- 렌더·폭발 FX·네트워크 수신은 인원 무관

데디 + 클라 1개로 재면 10.07~10.72ms 로 단독 실행과 같다. 클라 2개를 붙이면 12~25ms 로
튀지만 **같은 탄막을 그리는 두 클라가 서로 2배 차이 나므로** 그것은 콘텐츠 비용이 아니라
한 머신에서 UE 프로세스 3개(데디+클라2)가 6코어를 나눠 쓰는 경합이다.

**진짜 2인 수치는 머신 두 대가 있어야 잴 수 있다 — 이 문서는 그것을 재지 않았다.**

## 재현

```powershell
# 단일 패턴
scripts\profile.ps1 -Bullets -1 -Frames 720 `
  -ExtraExec "re.Fx.Explosions 1,re.Debug.BossPattern 6" -Label p06_LissajousStorm
scripts\profile-stats.ps1 -Run "Saved\Profiling\<런디렉터리>"
```

배경 부하가 p99 를 직격하므로 측정 중에는 다른 프로세스를 띄우지 않는다.
런당 `trace.utrace` 가 ~460MB 라 여러 패턴을 돌릴 때는 캡처 후 지운다.
