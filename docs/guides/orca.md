# Orca 에이전트 오케스트레이션 가이드

이 프로젝트는 **Orca ADE** 위에서 개발한다. Claude Code 세션은 Orca 터미널 페인 안에서 실행되며,
Orca CLI를 통해 **다른 페인의 에이전트 세션에 지시를 보내고 응답을 받을 수 있다.**

---

## 1. 내가 Orca 안인지 판별

환경변수가 결정적 증거다.

```powershell
$env:TERM_PROGRAM          # "Orca"
$env:ORCA_TERMINAL_HANDLE  # 내 페인 핸들 (term_xxxxxxxx-...)
$env:ORCA_PANE_KEY         # <tabId>:<leafId>
$env:ORCA_WORKTREE_ID      # <repoId>::<worktree 경로>
$env:ORCA_APP_VERSION      # 1.4.139
```

`ORCA_TERMINAL_HANDLE`이 곧 **내 주소**다. 다른 세션도 이 핸들로 나에게 메시지를 보낼 수 있다.

부모 프로세스 체인으로도 확인된다:
`powershell.exe → claude.exe → powershell.exe → orca-terminal-daemon.exe → Orca.exe`

## 2. CLI 위치 — PATH에 없다

`orca`는 **PATH에 등록되어 있지 않다.** 그냥 `orca ...`를 치면 command not found가 난다.
풀경로를 써라.

```powershell
$orca = "$env:LOCALAPPDATA\Programs\orca\resources\bin\orca.exe"
& $orca status --json    # ok:true 면 런타임 정상
```

전제조건: Settings → Experimental 에서 orchestration이 켜져 있어야 한다. (2026-07-14 기준 켜져 있음)

---

## 3. 통신 채널 두 개 — 용도가 다르다

| 방식 | 지시 보내기 | 응답 받기 | 용도 |
|---|---|---|---|
| `terminal send` | **X** (긴 텍스트는 뭉개짐) | **X** (alt-screen) | 일반 셸 페인 전용 |
| `orchestration` | O (자동 배달) | O (스레드) | **에이전트 간 통신** |

### `terminal send` — 일반 셸 전용. 에이전트에게는 쓰지 마라

```powershell
& $orca terminal send --terminal term_xxx --text "git status" --enter
& $orca terminal read --terminal term_xxx     # 일반 셸이면 출력이 보인다
```

일반 셸 페인에는 잘 동작한다. **Claude Code / Codex 같은 TUI 에이전트에는 양방향 모두 실패한다.**

**함정 1 — 응답을 못 읽는다.** TUI 에이전트는 **alt-screen 버퍼**를 쓴다.
화면 내용이 스크롤백에 쌓이지 않으므로 `terminal read`는 빈 값을 돌려준다:

```
tail: ["PS E:\UnrealProjects\Project_RE>claude"]
returnedLineCount: 1
```

**함정 2 — 긴 지시는 조용히 뭉개진다. 이게 더 위험하다.**
`terminal send`는 `accepted: true` 와 정상 `bytesWritten` 을 돌려주지만,
긴 프롬프트를 TUI에 밀어넣으면 **일부만 도착하거나 통째로 사라진다.**
실제로 3개 워커 세션에 긴 작업 프롬프트를 보냈을 때, 한 세션에는 `2` 한 글자만 도착했고
나머지 둘은 아무것도 받지 못했다. **CLI는 성공을 보고했다.**

> **에이전트에게 보내는 것은 지시든 질문이든 전부 `orchestration send` 를 써라.**
> `terminal send` 의 성공 반환값을 믿지 마라 — 도달 여부는 상대에게 물어봐야만 확인된다.

### `orchestration` — 에이전트 간 메시지 큐. 이게 정답

```powershell
& $orca orchestration send --from $env:ORCA_TERMINAL_HANDLE --to term_xxx `
    --subject "..." --body "..."
& $orca orchestration check --terminal $env:ORCA_TERMINAL_HANDLE --unread
& $orca orchestration reply --id msg_xxx --body "..."
& $orca orchestration inbox --json          # 전체 큐 (송수신 양방향 전부)
```

**자동 배달된다.** `send` 하는 즉시 `delivered_at`이 찍히고 수신측 `read:1`이 된다.
상대를 `terminal send`로 찌르거나 `check --inject`를 날릴 필요가 **없다**.
Orca가 각 에이전트 세션에 훅(`ORCA_AGENT_HOOK_*` 환경변수)을 심어 프롬프트로 직접 주입하기 때문이다.

응답은 `thread_id`로 원본 메시지에 묶여 돌아온다.

`check` 옵션: `--unread`(기본, 읽음 처리) / `--peek`(읽음 처리 안 함) / `--all` / `--wait --timeout-ms <n>`(도착까지 블로킹)

---

## 4. 태스크 / 디스패치 / 게이트

메시지 위에 얹힌 3층 구조다.

```
gates      사람 판단이 필요한 지점. 태스크를 막는다.
tasks      작업 단위. 의존성 + 소유권 + 생명주기.
messages   위 두 개가 이 위에서 돈다.
```

**태스크 생명주기:** `pending → ready → dispatched → completed / failed / blocked`
**메시지 타입:** `status`, `dispatch`, `worker_done`, `escalation`, `decision_gate`, `heartbeat`

### 역할

- **Coordinator** — 태스크 생성, 워커에 배정, 진행 감시, 게이트 해소
- **Worker** — 스펙 실행, `worker_done` 전송, 막히면 게이트로 에스컬레이션

### 워커 규약 — 어기면 코디네이터가 멈춘다

> `worker_done`은 **실패해도 정확히 한 번** 보낸다. 안 보내면 코디네이터가 영원히 대기한다.
> task ID와 dispatch ID를 **둘 다** 넣는다. 재시도로 생긴 유령 워커가 엉뚱한 dispatch를 완료 처리하는 것을 막기 위함이다.

```powershell
& $orca orchestration send --from $env:ORCA_TERMINAL_HANDLE --to <coordinator> `
    --type worker_done --task-id task_1 --dispatch-id dsp_1 `
    --files-modified "a.cpp,b.h" --subject "..." --body "..."
```

`--payload`에 raw JSON을 넣지 마라 — PowerShell이 따옴표를 먹는다. `--task-id` / `--dispatch-id` /
`--files-modified` / `--report-path` / `--phase` 전용 플래그를 써라.

### 수동 디스패치

```powershell
& $orca orchestration task-create --spec "..." --task-title "..." --deps '["task_1"]'
& $orca terminal create --worktree active --command "claude"      # 워커 페인 확보
& $orca orchestration dispatch --task task_2 --to term_xxx --inject
& $orca orchestration check --terminal $env:ORCA_TERMINAL_HANDLE --wait --timeout-ms 600000
```

`--inject`는 워커 TUI에 통신 규약 preamble까지 자동으로 박아준다.
`--dry-run` / `--return-preamble`로 무엇이 주입될지 미리 볼 수 있다.

### 자동 코디네이터 루프

```powershell
& $orca orchestration run --spec "..." --max-concurrent 3 --poll-interval-ms 5000
```

ready 상태 태스크를 유휴 워커에 알아서 뿌린다. 동시 실행 상한만 정해주면 된다.

### 결정 게이트

```powershell
& $orca orchestration gate-create --task task_2 --question "ISM vs Niagara?" --options '["ISM","Niagara"]'
& $orca orchestration gate-resolve --id gate_1 --resolution "ISM"
```

워커가 애매한 설계 결정에 부딪히면 혼자 찍지 말고 게이트로 올린다 (태스크는 `blocked`가 됨).
CLAUDE.md 5번 "모호하면 질문" 규칙의 기계화 버전이다.

---

## 5. 지뢰 — 읽고 넘어가라

### 5-1. auto mode 페인에 보내는 것은 무승인 실행이다
옆 페인이 auto mode(`⏵⏵`)면 `terminal send` / `orchestration send`로 밀어넣은 내용이
**사람 승인 없이 그대로 실행된다.** `git push`, 파일 삭제, 빌드 전부 확인 없이 나간다.
보낼 대상과 내용을 반드시 사용자에게 확인받아라.

### 5-2. `orchestration reset`은 스코프가 없다
runtime-global이다. 코디네이터가 여럿 붙어 있어도 **전부 날아간다.** 함부로 치지 마라.

### 5-3. UE5 빌드 비용
워커 N개 = worktree N개 = 각자 `Binaries/` + `Intermediate/`로 수십 GB, CPU도 N분할.
**코드 편집·리뷰 병렬화는 이득이지만 빌드까지 동시에 물리면 오히려 느려진다.**
게이트로 빌드를 직렬화하는 편이 현실적이다.

### 5-4. 문서에 있는 `orca orchestration ask`는 실재하지 않는다
공식 문서는 워커가 `orca orchestration ask`로 질문하라고 적어 놓았으나,
바이너리 1.4.139의 서브커맨드 목록에 **없다.** 실제로는 `gate-create`가 그 역할이다.
워커 preamble을 직접 쓸 때 `ask`를 넣지 마라.

---

## 6. 현재 worktree ↔ 워커 매핑

`& $orca worktree ps` 로 언제든 확인 가능. 2026-07-14 기준:

| worktree 경로 | 브랜치 |
|---|---|
| `E:/UnrealProjects/Project_RE` | 메인 작업 페인들 (본체) |
| `E:/UnrealProjects/Project_RE-autofire` | `feature/M2-autofire` |
| `E:/UnrealProjects/Project_RE-issue34` | `feature/M2-issue34` |
| `E:/UnrealProjects/Project_RE-bullet-hit` | `feature/M2-bullet-hit` |

worktree 셀렉터는 경로 말고 `--worktree branch:<브랜치명>` / `active` / `current` 로도 지정된다.

---

## 7. 기타 유용한 명령

```powershell
& $orca terminal list                    # 살아있는 페인 전부 (핸들 + 제목 + worktree)
& $orca terminal show --terminal term_xxx
& $orca terminal wait --terminal term_xxx --for tui-idle --timeout-ms 60000
& $orca worktree create --name <이름> --agent claude --prompt "..."   # 워커째로 새 worktree
& $orca file diff <path>                 # Orca 에디터에 diff 띄우기
& $orca agent-context --json             # 에이전트용 기계 판독 커맨드 스키마 전체
```

브라우저 자동화(`orca snapshot` / `click` / `goto` ...)와 스케줄 자동화(`orca automations ...`)도 있으나
이 프로젝트에서는 아직 쓰지 않는다.

---

## 참고

- 공식 문서: https://www.onorca.dev/docs
- CLI 레퍼런스: https://www.onorca.dev/docs/cli/overview
- 오케스트레이션: https://www.onorca.dev/docs/cli/orchestration
- 저장소: https://github.com/stablyai/orca (MIT)
