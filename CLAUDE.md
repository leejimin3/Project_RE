1. Think Before Coding
Don't assume. Don't hide confusion. Surface tradeoffs.

LLMs often pick an interpretation silently and run with it. This principle forces explicit reasoning:

State assumptions explicitly — If uncertain, ask rather than guess
Present multiple interpretations — Don't pick silently when ambiguity exists
Push back when warranted — If a simpler approach exists, say so
Stop when confused — Name what's unclear and ask for clarification
2. Simplicity First
Minimum code that solves the problem. Nothing speculative.

Combat the tendency toward overengineering:

No features beyond what was asked
No abstractions for single-use code
No "flexibility" or "configurability" that wasn't requested
No error handling for impossible scenarios
If 200 lines could be 50, rewrite it
The test: Would a senior engineer say this is overcomplicated? If yes, simplify.

3. Surgical Changes
Touch only what you must. Clean up only your own mess.

When editing existing code:

Don't "improve" adjacent code, comments, or formatting
Don't refactor things that aren't broken
Match existing style, even if you'd do it differently
If you notice unrelated dead code, mention it — don't delete it
When your changes create orphans:

Remove imports/variables/functions that YOUR changes made unused
Don't remove pre-existing dead code unless asked
The test: Every changed line should trace directly to the user's request.

4. Goal-Driven Execution
Define success criteria. Loop until verified.

Transform imperative tasks into verifiable goals:

Instead of...	Transform to...
"Add validation"	"Write tests for invalid inputs, then make them pass"
"Fix the bug"	"Write a test that reproduces it, then make it pass"
"Refactor X"	"Ensure tests pass before and after"
For multi-step tasks, state a brief plan:

1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
Strong success criteria let the LLM loop independently. Weak criteria ("make it work") require constant clarification.

5. Ask Before Implementing
**모호한 것이 하나라도 있으면 절대 구현하지 말고 질문할 것.**
**코드 작성 전 반드시 허락을 받을 것.**

- 요구사항이 불명확하면 → 질문
- 구현 방법이 여러 개라면 → 질문
- 기존 코드 패턴과 충돌 가능성이 있으면 → 질문
- 데이터 구조/API 사용법이 확실하지 않으면 → 질문

6. Git Branching: Gitflow
브랜치 전략: **Gitflow** 사용.

| 브랜치 | 용도 |
|--------|------|
| `main` | 릴리즈 태그 전용 (Tag 0.1, 0.2, 1.0 ...) |
| `dev` | 통합 브랜치. feature → dev 머지. |
| `feature/*` | 기능 개발. dev에서 분기, dev로 머지. |
| `release/*` | 릴리즈 준비. dev에서 분기, bugfix만. main + dev 양쪽 머지. |
| `hotfix/*` | 프로덕션 긴급 수정. main에서 분기, main + dev 양쪽 머지. |

규칙:
- feature 브랜치명: `feature/M1-mass-bullet`, `feature/M2-player-loop` 등 마일스톤 접두어
- main 직접 커밋 금지 — 반드시 PR 경유
- release 브랜치에서는 bugfix 커밋만 허용

7. Orca 에이전트 오케스트레이션
이 프로젝트는 **Orca ADE** 위에서 개발한다. 네 세션은 Orca 터미널 페인 안에서 실행 중이다.
확인법: `$env:ORCA_TERMINAL_HANDLE` 이 있으면 Orca 안이다.

다음 상황이면 **`docs/guides/orca.md` 를 먼저 읽어라**:
- 다른 에이전트 세션(옆 페인)에 지시를 보내거나 응답을 받아야 할 때
- 여러 worktree에 작업을 병렬로 뿌려야 할 때
- 코디네이터/워커로 일해야 할 때 (`worker_done` 규약이 있다 — 모르고 쓰면 코디네이터가 멈춘다)
