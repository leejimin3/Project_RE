# dedi-verify N인 판정 설계 (#87)

- **이슈**: #87 [M5] dedi-verify 스크립트 N인 판정 — `-Clients 2` 이상 반쪽 판정 해소
- **마일스톤**: M5 — 협동 멀티 (N명) + 시드 탄막 + 서버권위 피격
- **날짜**: 2026-08-14
- **라벨**: enhancement, networking

## 목표

`scripts/dedi-verify.ps1` 은 `-Clients N` 인자를 받지만 N≥2에서 **판정이 반쪽인데 초록불이 뜬다.** 검증 도구에서 가장 나쁜 실패 양식이다.

그리고 #85·#86의 2인 검증을 매번 손으로 돌렸다 — 서버를 특정 플래그 없이 띄우고, 클라 둘을 연달아 붙이고, 로그 여러 개를 `Select-String` 으로 대조했다. 그 절차를 스크립트로 흡수한다.

## 근본 원인

`Source/Project_RE/Core/REPlayerController.cpp:512` — 대쉬 프로브의 마지막 타이머가 **컨트롤러마다 무조건** `FPlatformMisc::RequestExit(false)` 를 부른다.

프로브는 `HasAuthority() && FApp::IsUnattended()` 조건으로 **서버측 PC마다** 돈다(`:96`). 따라서 클라가 둘이면 서버측 PC도 둘이고, **먼저 완주한 하나가 서버 프로세스를 내린다.** 뒤 클라의 프로브는 시작조차 못 한다.

`dedi-verify.ps1` 은 그 자체 종료를 "프로브 완료" 신호로 쓴다(`$srv.WaitForExit`). 신호와 결함이 같은 뿌리다.

그런데도 판정은 통과한다 — 뒤 클라의 코스메틱 로그(`[Attack] fire montage`, `[Dash] anim len=`)는 접속 직후 찍히므로 클라 판정 3항목이 그대로 PASS 한다. 잘린 것은 **서버측** 프로브뿐인데 서버 판정은 클라 개수와 무관하게 1벌만 본다.

**스크립트만으로는 고칠 수 없다.** 첫 `RequestExit` 이 서버를 내리므로 둘째 프로브는 시작조차 못 하고, 로그를 아무리 폴링해도 존재하지 않는 줄을 기다리게 된다. C++ 변경이 필수다.

## 지금 가이드에 모순된 지시 두 개가 공존한다

`docs/guides/dedicated-server.md` 는 같은 스크립트·같은 상황에 대해 정반대를 지시한다:

| 검증 | 서버 `-unattended` | 이유 |
|---|---|---|
| 2인 게임 검증 (#85/#86 수동 절차) | **빼라** | 켜면 약 4.4초에 서버가 죽어 둘째 클라가 못 붙는다 |
| NavMesh 재검증 | **넣어라** | 이동 프로브가 `-unattended` 에서만 돈다 |

둘 다 프로브 self-exit 라는 한 뿌리에서 나왔다. 종료 게이팅을 고치면 이 모순이 사라진다.

## 타이밍 — 한 모드로 다 볼 수 없다

프로브는 컨트롤러 `BeginPlay` 기준 이동 1.0s → 발사 1.5s → 대쉬 2.0s(+0.3 거리측정 +2.1 재활성) 순서로 **약 4.1~4.4초에 완주**한다.

보스 발사는 `re.Coop.ExpectedPlayers` 가 찰 때 시작한다(#85). 2인이면 클라2 ready 시점이다. **#86 실측 TTK는 5.3초.**

즉 종료를 "N개 프로브 완주"로 고쳐도 **종료가 약 4.1초, 전멸이 5.3초** — 서버가 여전히 먼저 죽는다. 프로브 완주 신호와 게임 결과 판정은 **타이밍이 양립하지 않는다.** 그래서 모드를 나눈다.

## 1. 종료 결정을 GameMode로 옮긴다

프로브는 "나 끝났다"만 보고하고, **프로세스를 끌지 말지는 전체를 아는 GameMode가 정한다.** 지금은 개별 컨트롤러가 전체의 운명을 결정하고 있다.

```cpp
// AREPlayerController — RequestExit 자리에
if (AREGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AREGameMode>() : nullptr)
{
    GM->NotifyProbeComplete();
}

// AREGameMode
void AREGameMode::NotifyProbeComplete()
{
    ++CompletedProbes;
    const int32 Expected = FMath::Max(1, CVarExpectedPlayers.GetValueOnGameThread());
    UE_LOG(LogTemp, Log, TEXT("[RE] Probe complete %d/%d"), CompletedProbes, Expected);
    if (CompletedProbes >= Expected)
    {
        FPlatformMisc::RequestExit(false);
    }
}
```

`NotifyPlayerReady` / `TryStartBossFiring` 과 **같은 모양**이다 — #85가 세운 패턴을 그대로 쓴다. 인원 기준도 같은 CVar를 재사용하므로 진실 원천이 하나다.

`CompletedProbes` 는 `int32` 카운터로 충분하다. `ReadyPlayers` 가 `TSet` 인 것은 클라가 `Server_NotifyReady` 를 연타할 수 있기 때문인데, 프로브 완주는 서버 자신의 타이머가 컨트롤러당 정확히 한 번 발화시키므로 중복 경로가 없다.

## 2. 두 모드

| | 프로브 모드 (기본) | 결과 모드 (`-Outcome`) |
|---|---|---|
| 서버 `-unattended` | **켬** | **끔** |
| 종료 신호 | N개 프로브 완주 → 서버 자체 종료 | 서버 로그의 `EndGame` 감지 → 스크립트가 정리 |
| C++ 변경 | 필요(위 게이팅) | **불필요** |
| 소요 | 약 5초 | 약 30초 |
| 보는 것 | 이동·발사·대쉬·NavMesh | 게이트·스폰이격·양클라 탄막·전원사망·양클라 결과화면 |

두 모드 모두 `-Clients N` 에 맞춰 `-ExecCmds="re.Coop.ExpectedPlayers N"` 을 서버에 넘긴다. CVar인 이유는 스테이징 `Config` 가 pak 안이라 ini면 인원을 바꿀 때마다 재쿡해야 하기 때문이다(#85).

**결과 모드에서 프로브를 끄는 이유.** 프로브가 플레이어를 `+X` 로 대쉬시켜 보스 탄막 레인(X=600)으로 밀어넣는다 — #56이 정확히 그 함정이었다. 그러면 측정 대상인 사망 타이밍이 교란된다. #85·#86이 손으로 검증한 절차가 이미 `-unattended` 없는 순수 전투였고, 그것을 그대로 자동화한다.

결과 모드의 종료 대기에는 상한이 필요하다. `EndGame` 이 끝내 안 나오면(보스가 안 죽고 플레이어도 안 죽는 회귀) 타임아웃으로 실패 처리하고 서버 로그 경로를 출력한다.

**결과 모드의 클라 투입은 순차적이어야 한다.** "클라1만 붙은 동안 발사가 시작되지 않는다"는 게이트 판정은 그 상태가 실제로 존재해야 성립한다. 고정 대기가 아니라 로그로 동기화한다 — 클라1 기동 → 서버 로그에 `Player ready 1/N` 이 뜰 때까지 폴링 → 그 시점에 `Boss firing started` 가 없음을 확인 → 클라2 기동. 고정 `Start-Sleep` 은 느린 머신에서 조용히 어긋난다(#82가 리스닝 대기를 폴링으로 바꾼 것과 같은 이유).

**프로브 모드에서 클라가 프로브 완주 전에 끊기면 서버는 영영 안 죽는다.** `CompletedProbes` 가 N에 도달하지 못하기 때문이다. 스크립트의 기존 프로브 타임아웃이 이를 잡아 실패로 낸다 — 조용한 무한 대기가 아니라 명시적 실패다.

## 3. 판정 — 개수 기반으로

지금 판정은 "패턴이 있나"만 본다. N인에서는 **몇 개인지**가 핵심이다. `Assert-Count` 헬퍼를 추가한다(기대 개수와 실제 개수를 함께 출력해야 실패 시 원인이 보인다).

**프로브 모드 (서버 로그):**

| 항목 | 기대 |
|---|---|
| `[Dash] probe done` | **정확히 N건** — 이 이슈의 핵심 판정 |
| `[Move] probe start` | N건 |
| `[Dash] dist=` | N건, 각 500~700 |
| `[Move] rejected: off-navmesh` (실목표) | **0건** |
| `[Move] rejected: off-navmesh` (맵 밖 좌표) | **있어야 한다** |

NavMesh는 **양방향으로 판정해야 한다.** 이동 프로브는 오프메시 거부 경로가 살아있는지 확인하려고 맵 밖 좌표(100000, 100000)를 일부러 한 번 요청한다.

- **음성 판정** — 실제 목표 좌표가 거부되면 안 된다. 맵 밖 좌표를 지목한 줄은 여기서 제외해야 한다. 제외하지 않으면 이 판정은 항상 실패한다.
- **양성 판정** — 그 맵 밖 좌표는 **반드시 거부돼야 한다.** 음성 판정만 두면 `ProjectPointToNavigation` 이 항상 성공하도록 회귀했을 때 거부 로그가 아예 안 찍히고, 그래도 음성 판정은 통과한다 — 거부 경로가 죽었는데 초록불이 뜬다. 이 이슈가 없애려는 바로 그 형태다.

두 판정은 라벨을 따로 둔다. 실패했을 때 어느 쪽이 깨졌는지가 원인을 가른다.

**결과 모드:**

| 항목 | 기대 |
|---|---|
| 게이트 | 클라1만 붙은 동안 `Boss firing started` **없음**, 클라 N 접속 후 `(N/N ready)` |
| 스폰 이격 | `Spawn player idx=` N건, `offsetY` 값이 **서로 다름** |
| 탄막 수신 | **각 클라 로그**에 `Boss Fire(Direct\|Artillery):.*role=ROLE_SimulatedProxy` |
| 전원 사망 | `Player died N/N` → `All N players dead` → `EndGame: DEFEAT` |
| 결과 화면 | **각 클라 로그**에 `Client_ShowResult: DEFEAT` |

### 모드마다 적용 가능한 판정이 다르다

**기존 판정 중 상당수는 프로브가 만들어낸 것이다.** 클라의 `[Attack] fire montage len=` 은 서버측 발사 프로브가 `Server_RequestFire` 를 부르고 그것이 `Multicast_PlayFireMontage` 를 태워야 찍힌다. `[Dash] anim len=... ROLE_AutonomousProxy` 도 대쉬 프로브가 원천이다. 서버측 `[Dash] dist=` 와 `[Move] probe start` 는 말할 것도 없다.

**결과 모드는 프로브를 끄므로 이 판정들이 반드시 실패한다.** 그대로 두면 결과 모드는 항상 빨간불이다. 따라서 판정 집합을 모드별로 나눈다:

| 판정 | 프로브 모드 | 결과 모드 |
|---|---|---|
| 넷드라이버 리스닝 / 월드 기동 / 크래시 없음 | ✅ | ✅ |
| `[Dash] probe done`·`dist=`·`[Move] probe start` | ✅ (개수 N) | ❌ 적용 안 함 |
| 클라 발사·대쉬 몽타주 | ✅ | ❌ 적용 안 함 |
| 서버 코스메틱 생략(몽타주 부재) | ✅ | ❌ **공허하게 통과** — 프로브가 없으니 애초에 안 찍힌다. 데디 가드를 검증하지 못하므로 적용하지 않는다 |
| 탄막 발사(권위)·수신(`SimulatedProxy`) | ✅ | ✅ (클라별) |
| 게이트·스폰이격·전원사망·결과화면 | ❌ 적용 안 함(전투가 그만큼 안 간다) | ✅ |

서버 코스메틱 생략 판정을 결과 모드에서 빼는 이유가 중요하다. 그 판정은 "데디 서버는 몽타주를 재생하지 않는다"(#74/#75)를 확인하는 것인데, 프로브가 없으면 재생을 시도할 계기 자체가 없어 **부재가 가드 덕분인지 계기 부재 탓인지 구분할 수 없다.** 공허한 초록불은 없는 것만 못하다.

**접속 유지 구간 절단**(`Host closed the connection` 이전만 판정, #82)은 두 모드 모두에 그대로 적용된다 — 결과 모드에서도 서버가 먼저 정리되면 클라가 폴백 월드를 띄운다.

## 4. 회귀 안전

**`-Clients 1` 기본 실행이 오늘과 완전히 같다.** `ExpectedPlayers=1` 이면 첫 프로브 완주가 곧 N개 완주라 종료 시점이 불변이고, 기존 12항목 판정도 그대로다.

#85의 "1인이면 스폰 오프셋이 정확히 0", #86의 "1인이면 판정 대상 1개"와 같은 구조적 안전장치다. 이 프로젝트의 모든 단일 플레이어 측정이 이 성질에 의존한다.

## 5. 부수 효과 — 가이드의 수동 절차가 사라진다

종료 게이팅이 고쳐지면 NavMesh는 프로브 모드에서 자연히 검증된다(이동 프로브가 N개 모두 완주한다). 게임 결과는 결과 모드가 맡는다. 위에서 지적한 **모순된 지시 두 개와 "2인 수동 페어 검증" 문단을 통째로 제거**하고 두 모드 사용법으로 대체한다.

## 검증

자동화 테스트 인프라가 없으므로 게이트는 빌드 + 스크립트 자체 검사 + 실 데디다.

1. **빌드 게이트** — `Project_REEditor`, `Project_REServer` 양쪽 `Result: Succeeded`
2. **`-SelfTest`** — 새 `Assert-Count` 가 정상 로그를 통과시키고 **개수 부족 로그를 잡아내는지**까지. 통과만 확인하면 항상-PASS 회귀를 못 잡는다(#82에서 배운 것)
3. **`-Clients 1` 회귀** — 12항목 그대로 PASS, `EXIT=0`. 종료 시점이 종전과 같은지 확인
4. **`-Clients 2` 프로브 모드** — `[Dash] probe done` 2건, 클라별 `[Move] probe start` 발화, off-navmesh 실목표 0건
5. **`-Clients 2 -Outcome`** — 판정 5개 전부 PASS
6. **실패 경로** — 기대 개수보다 적을 때 실제로 `EXIT=1` 과 실패 항목이 나오는지

## 범위 밖

- **VICTORY 경로 자동화** — 헤드리스로 보스 1000HP를 깎을 수단이 없다. 기존 `-Victory` 스위치는 `BossMaxHealth` 임시 하향 + 재쿡 전제 그대로 둔다
- **회전 등 로그에 남지 않는 항목** — #70이 실증한 대로 여전히 육안이며 스크린샷·영상 캡처는 하지 않는다
- **밸런스 조정** — #86이 남긴 TTK 5.3초 신호는 별건이다
- **CI 연동** — 로컬 개발 전용이고 빌드가 무겁다(#82에서 확정)
- **빌드·쿡 자동 수행** — 스크립트는 이미 스테이징된 산출물을 전제한다(#82에서 확정)

## 참고

- 스크립트 원본과 설계 근거: #82
- 시작 게이트와 `re.Coop.ExpectedPlayers`: #85, `docs/superpowers/specs/2026-08-12-coop-nplayers-design.md`
- 전원 사망이 자연 전투로 도달 가능해진 경위: #86, `docs/superpowers/specs/2026-08-13-coop-hit-detection-design.md`
- 프로브가 `+X` 로 이동해 탄막과 충돌한 전례: #56
- 검증 절차·함정: `docs/guides/dedicated-server.md`
