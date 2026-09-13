# 폭발 연출 깊이 구현 플랜 (#151)

설계: `docs/superpowers/specs/2026-09-13-explosion-depth-design.md`

**선행 조건.** #149 / PR #150 이 `dev` 에 머지된 뒤 **`dev` 에서 분기**한다. 이 작업은 그 코드 위에 얹힌다 — 머지 전이면 시작하지 않는다.

브랜치: `feature/M8-explosion-depth`

## Global Constraints

- **빌드 게이트는 `Result: Succeeded`.** `Target is up to date` 로 0 액션이면 아무것도 검증되지 않은 것이다
- **에디터가 열려 있으면 빌드도 에셋 재생성도 거부된다.** 커맨드라인에 `-game` 이 없으면 사람이 연 에디터다 — 죽이지 말고 물어봐라
- **자동화 테스트 인프라가 없다.** 게이트는 (1) 빌드, (2) headless 프로브 로그, (3) 실 RHI 스크린샷, (4) `profile.ps1` CSV 다. 테스트 프레임워크를 도입하지 말 것
- **익명 네임스페이스 상수 이름은 `Explosion*` 접두어.** 유니티 빌드 C4459
- **`-ExecCmds` 는 통짜 문자열 + 쉼표 뒤 공백 없음.** 공백에서 쪼개지면 CVar 가 에러 없이 그냥 안 걸린다
- **머티리얼 정본은 `.uasset`.** 스크립트는 초안 생성기고, 에셋이 이미 있으므로 이번엔 **`--force` 재생성**이다
- 태스크 순서를 지킬 것 — TASK 1(대조군 스크린샷)은 **어떤 변경보다 먼저** 찍어야 의미가 있다

## 검증된 API (실물 확인됨 — 추론하지 말 것)

| API / 사실 | 확인한 곳 |
|---|---|
| `MaterialExpressionFresnel` 의 입력 핀은 `ExponentIn`. 출력은 실루엣 가장자리 1, 정면 0 | `scripts/make_bullet_material.py:120-122` |
| `_arg("core-strength", 1.2)` — 하이픈 포함 이름이 그대로 동작한다(`startswith("--%s=")`) | `scripts/make_explosion_material.py:36-43` |
| `--strength` 가 코어·링에 **공유**돼 있다 | `make_explosion_material.py:132`, `:198` |
| 스크립트는 에셋이 있으면 `--force` 없이 `SystemExit(1)` | `make_explosion_material.py:51-58` |
| `save_asset(full, False)` — `only_if_is_dirty=False` 가 아니면 디스크에 안 써진다 | `make_explosion_material.py:117-119` |
| `SetCustomData(int32 Start, int32 End, TConstArrayView<float>, bool bMarkRenderStateDirty)` 범위 오버로드 존재 | `InstancedStaticMeshComponent.h:331`, 사용 `REExplosionRenderProcessor.cpp:124` |
| `SyncExplosionISM(UInstancedStaticMeshComponent*, const TArray<FTransform>&)` 는 익명 네임스페이스 자유 함수 — 3번째 통에 그대로 쓴다 | `REExplosionRenderProcessor.cpp:37-49` |
| `REBulletGeometry::EngineSphereRadius = 50.f` | `REBulletGeometry.h:25` |
| `re.Debug.BossPattern 2` = Artillery(곡사) | `REBossCharacter.cpp:43-51` |
| `Content/Materials/*.uasset` 는 git 추적 대상이다(gitignore 아님) | `git check-ignore` rc=1 |
| #149 기준선: 동시 90 에서 `Draws` 229, `Translu` 0.18 / p99 0.54 | 선행 설계 §9.1 |

## 파일 구조

| 파일 | 변경 |
|---|---|
| `scripts/make_explosion_material.py` | 수정 — 층별 strength 인자 + 연기 섹션 |
| `Content/Materials/M_REExplosionCore.uasset` | 재생성 (Strength 3.0 → 1.2) |
| `Content/Materials/M_REExplosionRing.uasset` | 재생성 (값 불변, `--force` 부수효과) |
| `Content/Materials/M_REExplosionSmoke.uasset` | **신규** |
| `Source/Project_RE/Mass/REBulletRenderSubsystem.h/.cpp` | 수정 — ISM 3번째 통 |
| `Source/Project_RE/Mass/REExplosionRenderProcessor.cpp` | 수정 — 반경 상수 + Sync 호출 1쌍 |
| `docs/superpowers/specs/2026-09-13-explosion-depth-design.md` | 수정 — §9.1 측정 결과 추가 |

**새 파일 0개.** `.h` 변경도 서브시스템 하나뿐이다.

---

### TASK 1: 튜닝 전 대조군 스크린샷

**어떤 변경보다 먼저.** 뒤로 밀면 대조군이 사라진다.

직선 패턴과 곡사 패턴 두 장. `re.Debug.BossPattern` 기본(-1)은 랜덤 로테이션이라 대조에 못 쓴다 — **직선도 인덱스로 고정한다**(0 = Spiral).

```powershell
$UE='E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$common='"E:\UnrealProjects\Project_RE\Project_RE.uproject" Main -game -windowed -ResX=1600 -ResY=900 -nosplash -NoSound '

# (1) 직선 (Spiral)
$p = Start-Process $UE -PassThru -ArgumentList ($common +
  '-log=RE_d151_before_straight.log -ExecCmds="re.Debug.BossPattern 0,re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Debug.FakeHitTargets 16,re.Debug.ScreenshotFrame 900"')
if (-not $p.WaitForExit(180000)) { $p.Kill() }

# (2) 곡사 (Artillery)
$p = Start-Process $UE -PassThru -ArgumentList ($common +
  '-log=RE_d151_before_arc.log -ExecCmds="re.Debug.BossPattern 2,re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Debug.FakeHitTargets 16,re.Debug.ScreenshotFrame 900"')
if (-not $p.WaitForExit(180000)) { $p.Kill() }
```

산출물은 `Saved/Screenshots/WindowsEditor/`. **파일명을 즉시 기록해라** — 나중에 `ls -t` 로 고르면 직후 런의 이미지를 집어 "안 변했다"고 오진한다.

```bash
cd E:/UnrealProjects/Project_RE && ls -t --time-style=+%H:%M:%S -l Saved/Screenshots/WindowsEditor/ | head -4
```

스크래치패드에 대조군 경로를 적어두고 다음 태스크로 간다. 커밋 없음(스크린샷은 리포에 넣지 않는다).

---

### TASK 2: 머티리얼 — 층별 톤 분리 + 연기 신규

#### 2-1. `scripts/make_explosion_material.py` 수정

**(i) 독스트링 마지막 인자 줄 교체**

```python
  인자: --force --core-strength=1.2 --ring-strength=3.0 --smoke-strength=0.15
        --opacity=0.9 --radius=0.45 --edge=0.10 --inner=0.30
        --smoke-opacity=0.45 --smoke-ramp=5.0 --smoke-rim=2.0
```

그리고 독스트링 첫 줄과 개요를 3종으로 갱신:

```python
M_REExplosionCore / M_REExplosionRing / M_REExplosionSmoke 부트스트랩 생성 (#149, #151).
```

**(ii) 코어 Strength — 인자 이름과 기본값을 바꾼다**

```python
strength = scalar("Strength", _arg("strength", 3.0), -900, 120)
```
↓
```python
# 3.0 은 진행도 0 에서 이미시브가 (3.00, 2.70, 1.65) 라 세 채널이 전부 1 을 넘겨
# 톤매퍼에서 흰색으로 뭉갰다(#151 §2.1). 1.2 면 R 만 살짝 타고 G·B 는 아래라
# 노랑-주황이 채널 비율로 남는다. 링과 인자를 나눈 이유는 링은 태우는 게 의도라서다.
strength = scalar("Strength", _arg("core-strength", 1.2), -900, 120)
```

**(iii) 링 Strength — 값은 그대로, 인자만 분리**

```python
strength = scalar("Strength", _arg("strength", 3.0), -760, 620)
```
↓
```python
# 링은 얇은 원환이라 픽셀 몇 줄로 충격파를 읽혀야 한다 — HDR 1.0 위로 태우는 것이
# 의도다(M_ArenaMarker 가 Color 를 3.0 으로 두는 것과 같은 규약). 코어와 값을 나눈다.
strength = scalar("Strength", _arg("ring-strength", 3.0), -760, 620)
```

**(iv) 파일 끝에 연기 섹션 추가**

```python
# ===== 연기 대역 (큰 반투명 구체) ==========================================
# 코어보다 크고 어둡고 늦게까지 남는다. 이미시브가 거의 검정이라 '빛나는 층'이 아니라
# '배경을 가리는 층'이다 — 세 층 중 실루엣에 부피를 주는 것이 이 층 하나다.
#
# 코어/링과 다른 점 셋:
#   1) 알파가 (1 - p^3) — 선형 (1 - p) 보다 늦게까지 남는다. 수명(0.8s)은 공유한다
#   2) 페이드-인 saturate(p * RampIn) — 없으면 스폰 프레임에서 어두운 구체가 최대
#      알파로 떠서 폭발이 회색으로 열린다(프레넬 역마스크가 하필 중심에서 가장
#      불투명해 그 뒤의 밝은 코어를 정확히 가린다)
#   3) 프레넬 역마스크 — 실루엣 가장자리에서 알파 0. 없으면 딱딱한 공 테두리가
#      보이고, 그건 지금 고치려는 '납작함'을 하나 더 얹는 것이다
mat, full = make_material("M_REExplosionSmoke")
expr, link, scalar, vector, progress, finish = helpers(mat, "smoke")

prog = progress(-1800, 200)

# 알파 커브 1 - p^3. Power 노드 대신 곱 두 번을 쓴다 — 핀 이름 리스크가 없다.
p2 = expr(unreal.MaterialExpressionMultiply, -1550, 200)
link(prog, "", p2, "A")
link(prog, "", p2, "B")
p3 = expr(unreal.MaterialExpressionMultiply, -1350, 200)
link(p2, "", p3, "A")
link(prog, "", p3, "B")
fade = expr(unreal.MaterialExpressionOneMinus, -1150, 200)
link(p3, "", fade, "")

# 페이드-인: RampIn 5.0 이면 p=0.2(0.16초)에 완전 불투명. 그 사이에 코어가 최대
# 밝기 구간을 지나간다.
ramp_p = scalar("RampIn", _arg("smoke-ramp", 5.0), -1800, 400)
ramp_m = expr(unreal.MaterialExpressionMultiply, -1550, 400)
link(prog, "", ramp_m, "A")
link(ramp_p, "", ramp_m, "B")
ramp = expr(unreal.MaterialExpressionClamp, -1350, 400)
link(ramp_m, "", ramp, "Input")

life = expr(unreal.MaterialExpressionMultiply, -1150, 320)
link(fade, "", life, "A")
link(ramp, "", life, "B")

# 프레넬 역마스크. M_REBullet 이 쓰는 것과 같은 노드다(make_bullet_material.py:120).
# Rim 이 클수록 마스크가 가장자리에 몰려 중심이 넓게 남는다.
rim = scalar("Rim", _arg("smoke-rim", 2.0), -1800, 620)
fres = expr(unreal.MaterialExpressionFresnel, -1550, 620)
link(rim, "", fres, "ExponentIn")
soft = expr(unreal.MaterialExpressionOneMinus, -1350, 620)
link(fres, "", soft, "")

masked = expr(unreal.MaterialExpressionMultiply, -1150, 520)
link(life, "", masked, "A")
link(soft, "", masked, "B")

opacity_p = scalar("Opacity", _arg("smoke-opacity", 0.45), -1150, 720)
opacity = expr(unreal.MaterialExpressionMultiply, -900, 620)
link(masked, "", opacity, "A")
link(opacity_p, "", opacity, "B")

# 이미시브에 페이드를 곱하지 않는다 — 거의 검정이라 곱해도 화면에서 같고,
# 소멸은 알파가 담당한다.
col = vector("Color", unreal.LinearColor(0.35, 0.24, 0.18, 1.0), -1150, 900)
strength = scalar("Strength", _arg("smoke-strength", 0.15), -1150, 1000)
emissive = expr(unreal.MaterialExpressionMultiply, -900, 940)
link(col, "", emissive, "A")
link(strength, "", emissive, "B")

finish(emissive, opacity, full)
```

→ 검증: `python -c "import ast,sys; ast.parse(open(r'scripts/make_explosion_material.py',encoding='utf-8').read())"` 가 조용히 끝난다

#### 2-2. 에셋 재생성

**에디터를 먼저 닫아라** (에셋 락). 기존 에셋이 있으므로 `--force` 필수.

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -ExecutePythonScript="E:/UnrealProjects/Project_RE/scripts/make_explosion_material.py --force" \
  -unattended -nosplash -nop4 2>&1 | grep -E "RE_EXPL"
```

기대 (**세 줄 다**):
```
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionCore (blend=BlendMode.BLEND_TRANSLUCENT)
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionRing (blend=BlendMode.BLEND_TRANSLUCENT)
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionSmoke (blend=BlendMode.BLEND_TRANSLUCENT)
```

- `RE_EXPL: 연결 실패` → 노드 입력 이름이 틀렸다. `ExponentIn` / `Input` / `A` / `B` 확인
- `--force` 경고 3줄(`기존 에셋을 지우고 재생성한다`)이 같이 나온다 — 정상
- 세 줄 중 하나라도 없으면 **커밋하지 말 것**

→ 검증: `ls -la Content/Materials/M_REExplosion*.uasset` 로 3개 존재 + `git status --short Content/Materials/` 에 Core/Ring 이 `M`, Smoke 가 `??`

#### 2-3. 커밋

```
feat(fx): 폭발 코어 톤 다운 + 연기 대역 머티리얼 (#151)
```

본문에 담을 것: (1) 3.0 이 왜 흰색이 되는지 채널값, (2) 링을 안 내리는 이유, (3) 연기 층의 세 가지 차이(1-p³ / 페이드-인 / 프레넬 역마스크)와 각각의 이유.

---

### TASK 3: ISM 3번째 통 (연기)

#### 3-1. `REBulletRenderSubsystem.h`

접근자 추가 (`GetExplosionRingISM` 아래):

```cpp
	UInstancedStaticMeshComponent* GetExplosionSmokeISM() const { return ExplosionSmokeISM; }
```

멤버 추가 (`ExplosionRingISM` 아래, 같은 주석 블록 안):

```cpp
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionSmokeISM = nullptr; // 연기 대역(큰 어두운 구체)
```

그리고 `// 폭발 (#149)` 주석 블록을 `(#149, #151)` 로 갱신하고 "두 통" → "세 통".

#### 3-2. `REBulletRenderSubsystem.cpp`

`ExplosionRingISM` 블록 끝(`:168` 의 닫는 `}`)과 마지막 로그 블록 사이에 삽입:

```cpp
	// 연기 대역 (#151) — 세 층 중 가장 크고 어둡다. 코어·링과 같은 설정이고 메시도
	// 같은 구체다. 등록 순서가 코어 → 링 → 연기인 것은 의도다: 반투명 정렬이
	// 컴포넌트 단위라 순서가 화면에 남는다(연기가 코어를 삼키면 이 순서부터 본다).
	ExplosionSmokeISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	ExplosionSmokeISM->SetupAttachment(ISM);
	ExplosionSmokeISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExplosionSmokeISM->bAffectDynamicIndirectLighting = false;
	ExplosionSmokeISM->bAffectDistanceFieldLighting = false;
	ExplosionSmokeISM->SetCastShadow(false);
	ExplosionSmokeISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ExplosionSmokeISM->SetStaticMesh(Mesh);
	}
	ExplosionSmokeISM->SetNumCustomDataFloats(1);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REExplosionSmoke.M_REExplosionSmoke")))
	{
		ExplosionSmokeISM->SetMaterial(0, Base);
	}
	else
	{
		UE_LOG(LogREBullet, Error, TEXT("[RE] M_REExplosionSmoke 로드 실패 — 폭발 연기가 기본 머티리얼로 렌더된다 (#151)"));
	}
```

→ 검증: 빌드는 TASK 4 와 묶어서 한 번만 한다(중간 상태는 연기 ISM 이 비어 있을 뿐 컴파일된다)

#### 3-3. 커밋

```
feat(fx): 폭발 연기 ISM 3번째 통 (#151)
```

---

### TASK 4: 렌더 프로세서 — 연기 층 동기

#### 4-1. `REExplosionRenderProcessor.cpp` 상수 추가

`ExplosionRingRadiusEnd` 아래:

```cpp
	/** 연기 대역(구체) 반경(cm). 시작은 코어(20)보다 크되 링(40)보다 작게 — 스폰 순간
	 *  코어를 삼키지 않는다. 끝은 링(220)보다 크게 — 마지막까지 남는 층이다. */
	constexpr float ExplosionSmokeRadiusStart = 30.f;
	constexpr float ExplosionSmokeRadiusEnd   = 260.f;
```

#### 4-2. `Execute` 수정

ISM 획득 (3줄째 추가 + 가드 확장):

```cpp
	UInstancedStaticMeshComponent* CoreISM  = RS ? RS->GetExplosionCoreISM()  : nullptr;
	UInstancedStaticMeshComponent* RingISM  = RS ? RS->GetExplosionRingISM()  : nullptr;
	UInstancedStaticMeshComponent* SmokeISM = RS ? RS->GetExplosionSmokeISM() : nullptr;
	if (!CoreISM || !RingISM || !SmokeISM)
	{
		return;
	}
```

배열 선언에 추가:

```cpp
	TArray<FTransform> SmokeXf;
	SmokeXf.Reserve(M);
```

루프 안, `Cd.Add(E.Progress);` **위**에:

```cpp
		// 연기도 등방 구체다 — 링과 달리 Z 스케일 함정에 걸리지 않는다.
		const float SmokeR = FMath::Lerp(ExplosionSmokeRadiusStart, ExplosionSmokeRadiusEnd, E.Progress);
		const float SmokeS = SmokeR / REBulletGeometry::EngineSphereRadius;
		SmokeXf.Add(FTransform(FRotator::ZeroRotator, E.Loc, FVector(SmokeS)));
```

동기 호출 추가:

```cpp
	SyncExplosionISM(CoreISM,  CoreXf);
	SyncExplosionISM(RingISM,  RingXf);
	SyncExplosionISM(SmokeISM, SmokeXf);
```

커스텀데이터 — **같은 `Cd` 를 세 통에** (세 층이 같은 진행도를 받고 각자 해석한다):

```cpp
	if (M > 0)
	{
		CoreISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		RingISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		SmokeISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
	}
```

클래스 독스트링(`REExplosionRenderProcessor.h:12-13`)의 "두 ISM" → "세 ISM", "인스턴스 2개" → "인스턴스 3개" 로 갱신.

#### 4-3. 게이트

**(a) Editor 빌드** — `Result: Succeeded`

**(b) headless 프로브**

```bash
cd E:/UnrealProjects/Project_RE && grep -E "ExplosionProbe|M_REExplosion|has no registered queries" Saved/Logs/RE_d151_probe.log | head -20
```

기대: `ExplosionProbe` 한 줄 이상, `M_REExplosionSmoke 로드 실패` **없음**.

#### 4-4. 커밋

```
feat(fx): 폭발 연기 층 렌더 — 드로우콜 상수 4 → 6 (#151)
```

---

### TASK 5: 튜닝 후 스크린샷 + 연출 판정

TASK 1 과 **같은 커맨드**로 두 장(직선 / 곡사). 로그 이름만 `after` 로.

판정 5항목 (설계 §9):

| # | 볼 것 | 무너지면 |
|---|---|---|
| 1 | 코어가 흰 덩어리가 아니라 노랑-주황인가 | `--core-strength` 를 0.8 까지 더 내려 재생성 |
| 2 | 폭발이 회색으로 열리지 않는가 | `--smoke-ramp` 5.0 → 8.0 |
| 3 | 연기 가장자리가 딱딱한 공 테두리가 아닌가 | `--smoke-rim` 2.0 → 3.5 |
| 4 | 연기가 코어를 통째로 가리지 않는가 | `--smoke-opacity` 0.45 → 0.25 |
| 5 | 곡사 착지 연기가 바닥에 묻히지 않는가 | 반경 End 260 → 200 (묻히는 건 크기 문제다) |

**튜닝은 머티리얼 파라미터 재생성 루프다** — `--force` 로 다시 돌리고 스크린샷만 다시 찍는다. 빌드 불필요(4·5번의 반경만 C++). 값을 바꿨으면 **스크립트 기본값도 같이 바꿔라** — 안 그러면 다음 `--force` 에 되돌아간다(정본은 `.uasset` 이지만 스크립트가 낡으면 재현이 깨진다).

판단이 서지 않으면 **스크린샷 4장을 사람에게 보여주고 물어라.** 이 태스크의 판정 기준은 주관적이고, 그게 이 이슈의 본질이다.

튜닝으로 값이 바뀌었으면 스크립트 + `.uasset` 을 추가 커밋한다.

---

### TASK 6: 측정 + 설계 문서 기록

#### 6-1. 프로파일 3런

```powershell
cd E:\UnrealProjects\Project_RE
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Debug.FakeHitTargets 16" -Label d151_off
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.FakeHitTargets 16,re.Fx.ExplosionBudget 0" -Label d151_nobudget
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.FakeHitTargets 16" -Label d151_budget256
```

동시 개수는 런 로그의 `ExplosionProbe` 에서 읽는다.

#### 6-2. 수치 추출

```powershell
scripts\profile-stats.ps1 -RunDir (Get-ChildItem Saved\Profiling\RE_Mass_5000_d151_* -Directory).FullName
```

**판정:**
- `Draws`(`nobudget`) 가 #149 의 **229 대비 +2 수준(≈231)** — 층당 +2 실측과 일치
- 크게 벗어나면(예: 동시 개수에 비례) 연기가 인스턴스로 배칭되지 않은 것 → 머티리얼 `used_with_instanced_static_meshes` 부터 본다
- `Translu` 는 **오를 것으로 예상**한다(층 +1, 그중 가장 큰 구체). #149 의 0.18 / p99 0.54 대비 얼마나 오르는지를 기록한다. `Frame` / `RT` 가 기준선 대비 무너지면 설계 §8 의 예산 노브를 꺼낼 근거가 된다

#### 6-3. 설계 문서 §9.1 작성 + 커밋

`docs/superpowers/specs/2026-09-13-explosion-depth-design.md` §9 아래에 `### 9.1 측정 결과 (2026-XX-XX)` 를 만든다. 담을 것:

- 3런 표(동시 폭발 / Draws mean·p99 / Frame / RT / Translu mean·p99)
- **#149 §9.1 과의 대조** — 상수가 4 → 6 으로 올랐는지
- 오버드로우 실측: `Translu` 가 실제로 얼마나 올랐는지, §8 예측과 맞는지
- 연출 판정: 스크린샷 5항목 결과, 초기값에서 바꾼 파라미터와 그 이유

```
docs: 폭발 연출 깊이 실측 결과 (#151)
```

---

### TASK 7: 풀 유니티 빌드 + PR

#### 7-1. Server 타깃 풀 유니티

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REServer Win64 Development \
  -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -DisableAdaptiveUnity -WaitMutex
```

기대: `Result: Succeeded`. C4459 는 이 구성에서만 잡힌다.

#### 7-2. PR

base `dev`. 이슈 #151 의 메타(label / milestone / assignee / project) 전부 미러링. 본문은 파일로 써서 `--body-file`.

본문에 반드시:
- 문제: 흰색 포화(채널값 근거) + 층 2개의 납작함, 대조 스크린샷
- (a)/(b) 두 갈래와 각각의 비용(코드 0줄 / 드로우콜 +2)
- **TASK 6 실측 표** — `Draws` 상수가 4 → 6, `Translu` 실제 증가폭
- 기각한 대안: 공유 strength 유지 / 전역 수명 연장 / 층별 커스텀데이터 슬롯 / additive / 감산 블렌드
- 한계: 오버드로우 증가, 주황 그라데이션은 절반만 해결(§2.2 는 후속)
- 게이트 표

---

## 하지 말 것 (스코프 밖)

- **코어 그라데이션 알파 리맵**(설계 §10). `--core-strength` 튜닝 후에도 주황이 안 보이면 **후속 이슈**로 남긴다 — 이 PR 에서 노드를 끼우지 말 것
- **링 값 변경.** `--force` 재생성으로 `.uasset` 은 바뀌지만 **파라미터 값은 3.0 / 0.9 그대로**여야 한다
- **`ExplosionLifeSec` 변경.** 0.8s 공유가 이 설계의 전제다(§4.2)
- **커스텀데이터 슬롯 추가.** 세 층이 슬롯 1개를 공유한다
- **4번째 층.** 3층으로 부족하다고 스크린샷이 말할 때만
- **`re.Fx.ExplosionBudget` 기본값 변경.** 측정이 요구하면 그때, 근거와 함께
- **`SyncExplosionISM` 을 공용 헬퍼로 승격.** 세 렌더 프로세서에 비슷한 함수가 있지만 빼지 말 것
- **호출부 3곳 수정.** `SpawnBulletExplosion` 시그니처 불변 — 고쳐야 한다고 느끼면 잘못 구현한 것이다
- **테스트 프레임워크 도입.** 이 리포에 자동화 테스트 인프라가 없다
- **M7 프로파일 문서·README·포트폴리오 수치 갱신.** 별 이슈
