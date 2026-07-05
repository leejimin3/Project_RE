# HAND-OFF — 다음 세션은 여기서 구현부터 시작

**작성:** 2026-07-05
**상태:** 설계·플랜 완료. **구현 미착수.** 다음 세션은 코드 작성부터 시작.

## 지금 뭐 하는 중

M0 #4 [Mass Processor 시뮬/렌더 분리 구조] 구현.

- 브랜치: `feature/M0-processor-split` (dev에서 분기, 커밋 3개 = 스펙+플랜 문서만)
- 스펙: `docs/superpowers/specs/2026-07-05-processor-sim-render-split-design.md`
- 플랜: `docs/superpowers/plans/2026-07-05-processor-sim-render-split.md` ← **이거 따라 구현**

## 다음 액션 (즉시)

플랜의 Task 1부터 순서대로 구현. 실행 스킬: `superpowers:subagent-driven-development` (추천) 또는 `superpowers:executing-plans`.

3 태스크:
1. Fragments + SimProcessor + Build.cs include path → 빌드
2. RenderProcessor → 빌드
3. GameMode CDO 플래그 프로브 → 빌드 + PIE 로그 관측

각 태스크 게이트 = **빌드 성공**. 최종 게이트 = **PIE Output Log에 `[RE] SimProcessor flags=7  RenderProcessor flags=5`**.

## 알아둘 것 (핵심 결정 요약)

- **구조만.** Execute는 로그 스텁. 실제 이동/ISM·런타임 skip 실증은 M1 (MassGameplay 필요, 지금 안 당김).
- **이슈 텍스트 수정함:** Sim = `AllNetModes`(7, Standalone 포함 — 싱글 M1~M3 안 깨지게), Render = `Standalone|Client`(5, 데디서버 skip). 이유는 스펙 참고.
- MassEntity·MassCore 모듈만 사용. Build.cs 모듈 의존 **변경 금지**, include path만 추가.
- 빌드 커맨드:
  ```bash
  "/e/UE_5.8/Engine/Build/BatchFiles/Build.bat" Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex -NoHotReload
  ```
- Step 4 PIE는 UE 에디터 수동 실행 필요 (Claude가 못 함).

## 완료 후

- PR: base=dev, 이슈 #4 메타 미러링 (PR 생성 규칙 메모리 참고).
- M0 남은 이슈: **#5** (탄막 패턴 시드 RPC 인터페이스) 하나. #4 머지 후 #5 진입 → M0 마일스톤 종료.
