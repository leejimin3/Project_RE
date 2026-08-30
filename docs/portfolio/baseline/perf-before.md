# 성능 기준선 (STEP 0) — 게이트 8

**캡처일:** 2026-08-29 · **조건:** `docs/guides/profiling.md` 고정 조건
**커맨드:**

```powershell
foreach ($n in 0,3,6) {
  scripts/profile.ps1 -Bullets -1 -Frames 720 `
    -ExtraExec "re.Fx.Explosions 1,re.Debug.BossPattern $n" -Label "baseline_perf_p$n"
}
scripts/profile-stats.ps1 -RunDir (Get-ChildItem Saved\Profiling\RE_Mass_-1_baseline_perf_p*_* -Directory).FullName
```

- `p0` = Spiral (직선탄 기준), `p3` = ArtilleryStorm (곡사 폭풍), `p6` = LissajousStorm (M7 최악 패턴)
- `-Bullets -1` = 오픈루프(게임플레이 경로, `BulletsPerShot` 고정 발수)

## 판정 대상 — GT (게임 스레드)

R-06 이 바꾸는 것은 **스폰 경로**이고 그건 게임 스레드다. RT/GPU 는 참고값이다.

| 패턴 | GT mean | GT p99 | Frame mean | Frame p99 |
|---|---:|---:|---:|---:|
| p0 Spiral | **6.17** | **9.97** | 6.40 | 9.49 |
| p3 ArtilleryStorm | **7.54** | **11.78** | 9.60 | 15.55 |
| p6 LissajousStorm | **4.65** | **9.09** | 7.92 | 13.93 |

**게이트 8 통과 조건: 리팩터 후 GT mean / p99 가 위 값보다 나빠지지 않을 것.**
런 간 노이즈가 ±0.9ms 라고 M7 이 기록해 두었다(`REBossCharacter.h` LissaFireInterval 주석) —
그보다 작은 차이는 개선/악화로 읽지 않는다.

## 전체 컬럼

| run | frames | Frame mean | Frame p99 | GT mean | GT p99 | RT mean | RT p99 | GPU mean | GPU p99 | Instances mean | Instances p99 | Draws mean | Draws p99 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| baseline_perf_p0 | 720 | 6.40 | 9.49 | 6.17 | 9.97 | 6.24 | 9.15 | 4.42 | 4.85 | 3,443 | 3,548 | 260 | 308 |
| baseline_perf_p3 | 720 | 9.60 | 15.55 | 7.54 | 11.78 | 9.58 | 15.70 | 5.27 | 6.39 | 943 | 1,233 | 705 | 1,003 |
| baseline_perf_p6 | 720 | 7.92 | 13.93 | 4.65 | 9.09 | 7.86 | 13.38 | 4.57 | 8.39 | 612 | 805 | 503 | 1,499 |

> 이 창은 300 → 720 프레임이고 `re.Fx.Explosions 1`(폭발 ON) 이라 `docs/profiling/M7-pattern-p99.md`
> 의 표와 조건이 다르다. 그 표와 직접 비교하지 마라 — 비교 대상은 **이 문서 자신의 전/후**다.
