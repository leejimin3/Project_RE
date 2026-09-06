# UI 스크린샷

`re.Debug.ScreenshotFrame` + `re.Debug.ScreenshotUI 1` 로 찍는다. 시각 결과물이라 자동 판정만으로 완료를 선언할 수 없어, PR 근거로 여기에 남긴다.

## 촬영 방법

```
UnrealEditor.exe Project_RE.uproject /Game/Level/Main -game -windowed -ResX=1600 -ResY=900 ^
  -ExecCmds="re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Bullets.Count 3000,re.Debug.ScreenMessages 0,re.Debug.ScreenshotUI 1,re.Debug.ScreenshotFrame 900"
```

산출물은 `Saved/Screenshots/WindowsEditor/` 에 떨어진다.

**`re.Debug.ScreenshotUI 1` 이 없으면 화면공간 UI 가 PNG 에 안 나온다.** 기본 0 은 탄막 렌더 검증(#97)용이다 — HUD 가 화면을 가리면 탄 색·밝기 판정을 방해한다. 월드스페이스인 보스 체력바는 0 에서도 찍히므로 "UI 는 나오는데 HUD 만 없다" 로 오해하기 쉽다.

**`-unattended` 로는 못 찍는다.** 헤드리스 프로브 모드에서는 렌더 프로세서가 돌지 않아 스크린샷 훅이 걸리지 않는다(실측: `RenderProbe` 0건).

## 목록

| 파일 | 내용 | 이슈 |
|---|---|---|
| `hud-2026-08-16.png` | 플레이어 HUD — HP / 대쉬 쿨다운 / 투사체 수 | #100 |
| `hero-spiral-2026-09-07.png` | 루트 README 대표 이미지 — Spiral 4,734발 동시 체공, GT 4.57ms / Draws 222 | #141 |
