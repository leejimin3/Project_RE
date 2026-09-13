# 구현 목표: [M8 / #151] 폭발 연출 깊이 — 코어 톤 튜닝 + 연기 대역 3번째 ISM 층

## 컨텍스트

UE 5.8.1 소스 빌드로 만드는 탑다운 탄막(bullet-hell) 게임. 탄환은 MassEntity + ISM 배칭으로 50,000발에서 드로우콜 228을 유지한다.

선행 작업 #149(PR #150)가 폭발 FX 를 개별 `UNiagaraComponent` 스폰에서 **ISM 2층**(코어 구체 + 수평 링)으로 옮겨 드로우콜을 상수화했다(동시 90에서 +4, 변경 전 680). 목적은 달성했지만 **연출이 납작하다.**

**이 goal 이 하는 것:** 두 갈래다.

- **(a) 코어 톤 튜닝** — `M_REExplosionCore` 의 `Strength` 기본값 3.0 이 언릿 이미시브라 톤매퍼에서 흰색으로 뭉갠다. 층별 인자를 쪼개고 코어만 1.2 로 내린다. **C++ 0줄, 드로우콜 변화 0.**
- **(b) 연기 대역** — 3번째 ISM 층(큰 반투명 어두운 구체)을 더해 실루엣에 부피를 준다. **드로우콜 +2 상수**, 동시 개수와는 여전히 무관.

이슈 **#151**, 마일스톤 **M8: 포트폴리오 하드닝 — 동작 불변 리팩터링**.

**스코프 밖(손대지 말 것):** 코어 그라데이션 알파 리맵, 4번째 층, 연출 종류 분기(피격/착지), NDC / SubUV 플립북, `ExplosionLifeSec` 변경, 커스텀데이터 슬롯 추가, `re.Fx.ExplosionBudget` 기본값 변경, M7 프로파일 문서·README·포트폴리오 수치 갱신. 자세한 목록은 맨 아래.

설계 스펙: `docs/superpowers/specs/2026-09-13-explosion-depth-design.md`
상세 플랜: `docs/superpowers/plans/2026-09-13-explosion-depth.md`

(참고 가능. 단 **아래 코드가 최종 정본**이다 — spec/plan 과 어긋나면 이 문서를 따른다.)

## 브랜치

**선행 조건: #149 / PR #150 이 `dev` 에 머지돼 있어야 한다.** 이 작업은 그 코드 위에 얹힌다.

```bash
cd E:/UnrealProjects/Project_RE && git fetch origin && git log --oneline origin/dev -3
```

기대: `dev` HEAD 근처에 PR #150 머지 커밋이 보인다. **안 보이면 시작하지 말고 사람에게 물어라** — 아래 코드는 전부 `REExplosionRenderProcessor` / `ExplosionCoreISM` / `M_REExplosionCore` 가 이미 존재한다고 전제한다.

`dev` 에서 분기:

```bash
cd E:/UnrealProjects/Project_RE && git checkout dev && git pull && git checkout -b feature/M8-explosion-depth
```

## 전역 제약

- **빌드 커맨드** (Git Bash. `MSYS_NO_PATHCONV=1` 없으면 경로가 mangling 돼 조용히 실패한다):
  ```bash
  cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
    "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
    Project_REEditor Win64 Development \
    -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
    -DisableAdaptiveUnity -WaitMutex
  ```
- **빌드 게이트는 `Result: Succeeded`.** `Target is up to date` 로 0 액션이면 **아무것도 검증되지 않은 것이다** — 유니티 섀도잉(C4459)은 실제 컴파일에서만 잡힌다
- **에디터가 열려 있으면 빌드도 에셋 재생성도 거부된다**: `Unable to build while Live Coding is active` / 에셋 락. `Get-CimInstance Win32_Process -Filter "Name LIKE '%Unreal%'"` 로 확인하고, **커맨드라인에 `-game` 이 없으면 사람이 연 에디터다 — 죽이지 말고 물어봐라**
- **자동화 테스트 인프라가 없다.** 게이트는 (1) 빌드 성공, (2) headless 프로브 로그, (3) 실 RHI 스크린샷, (4) `profile.ps1` CSV 수치다. **테스트 프레임워크를 도입하지 말 것**
- **익명 네임스페이스·함수 지역 static 이름을 다른 파일과 겹치지 말 것.** 유니티 빌드에서 C4459 로 깨진다. 이 문서의 이름은 전부 `Explosion*` 접두어다
- **머티리얼 정본은 `.uasset` 이다.** 스크립트는 초안 생성기고, 세 에셋 중 둘이 이미 존재하므로 이번엔 **`--force` 재생성**이다
- **`-ExecCmds` 는 통짜 문자열 + 쉼표 뒤 공백 없음.** PowerShell 이 공백에서 인자를 쪼개면 CVar 가 **에러 없이 그냥 안 걸린다**
- **TASK 순서를 지킬 것.** TASK 1(대조군 스크린샷)은 **어떤 변경보다 먼저** 찍어야 의미가 있다
- 커밋 메시지 끝에 붙일 것:
  ```
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
  ```

## 검증된 API (실물 확인됨 — 추론하지 말 것)

| API / 사실 | 확인한 곳 |
|---|---|
| `MaterialExpressionFresnel` 은 파이썬으로 만들 수 있고 입력 핀 이름이 `ExponentIn` 이다. 출력은 실루엣 가장자리에서 1, 정면에서 0 | `scripts/make_bullet_material.py:120-122` |
| `_arg()` 는 `--<이름>=<값>` 토큰을 float 로 읽는다. **이름에 하이픈이 있어도 된다**(`startswith("--%s=")` 비교) | `scripts/make_explosion_material.py:36-43` |
| `--strength` 가 코어·링에 **공유**돼 있다 — 코어만 못 내린다 | `make_explosion_material.py:132`(코어), `:198`(링) |
| 스크립트는 에셋이 이미 있으면 `--force` 없이 `SystemExit(1)` 로 중단한다 | `make_explosion_material.py:51-58` |
| `MaterialEditingLibrary` 변경은 dirty 플래그를 세우지 않는다 → `save_asset(full, False)` 가 아니면 디스크에 안 써진다 | `make_explosion_material.py:117-119` |
| 머티리얼에 `used_with_instanced_static_meshes = True` 가 없으면 엔진이 **조용히 기본 머티리얼로 대체**한다 (`GetMaterial()` 은 정상 반환) | `make_explosion_material.py:70` (#97 이력) |
| `MaterialExpressionClamp` 의 입력 핀은 `Input` (기본 min 0 / max 1) | `make_explosion_material.py:210` |
| `MaterialExpressionOneMinus` 연결은 `link(src, "", node, "")` | `make_explosion_material.py:127-128` |
| `virtual bool SetCustomData(int32 InstanceIndexStart, int32 InstanceIndexEnd, TConstArrayView<float> CustomDataFloats, bool bMarkRenderStateDirty = false)` — 범위 오버로드 존재 | `InstancedStaticMeshComponent.h:331`, 사용 `REExplosionRenderProcessor.cpp:124` |
| `SyncExplosionISM(UInstancedStaticMeshComponent*, const TArray<FTransform>&)` 는 익명 네임스페이스 자유 함수 — **3번째 통에 그대로 재사용된다** | `REExplosionRenderProcessor.cpp:37-49` |
| `REBulletGeometry::EngineSphereRadius = 50.f` | `REBulletGeometry.h:25` |
| `re.Debug.BossPattern` 인덱스: **0 = Spiral(직선), 2 = Artillery(곡사)**. 기본 -1 은 랜덤 로테이션이라 대조에 못 쓴다 | `REBossCharacter.cpp:43-51` |
| `Content/Materials/*.uasset` 는 git 추적 대상이다(gitignore 아님) | `git check-ignore` rc=1 |
| **ISM 비등방 스케일 함정**: XY 를 키운 채 Z 를 1.0 미만으로 두면 인스턴스가 화면에서 통째로 사라진다. **연기는 등방 구체라 해당 없다** — 링을 건드리면 걸린다(건드리지 않는다) | `REArcRenderProcessor.cpp:16-23` |
| #149 §9.1 기준선 실측: 동시 90 에서 `Draws` 229, `Translu` mean 0.18 / p99 0.54 (폭발 OFF 는 225 / 0.19 / 0.61) | `specs/2026-09-13-explosion-ism-o1-design.md` §9.1 |
| `scripts/profile-stats.ps1 -RunDir <경로...>` 가 `frames.csv` 에서 컬럼별 mean/p99 를 마크다운 표로 낸다. `Draws`=`RHI/DrawCalls`, `Translu`=`Exclusive/RenderThread/RenderTranslucency` | `scripts/profile-stats.ps1:1-30` |

## 기존 파일 현황 (변경 대상)

**`scripts/make_explosion_material.py`** — 톱레벨 순차 스크립트. `make_material(name)` 이 빈 Unlit/Translucent/two_sided 머티리얼을 만들고, `helpers(mat, tag)` 가 `(expr, link, scalar, vector, progress, finish)` 6종을 돌려준다. 그 아래 `# ===== 코어 (구체) =====` 와 `# ===== 링 (수평 원환) =====` 두 섹션이 순차 실행된다. **함수 정의부는 손대지 않고, 코어/링의 `strength` 한 줄씩 + 파일 끝 연기 섹션만 바꾼다.**

**`Content/Materials/M_REExplosionCore.uasset` / `M_REExplosionRing.uasset`** — 이미 존재하고 git 추적 중. `--force` 재생성 대상.

**`Source/Project_RE/Mass/REBulletRenderSubsystem.h/.cpp`** — `UREBulletRenderSubsystem : UWorldSubsystem`. `OnWorldBeginPlay` 에서 데디서버·비게임월드를 걸러내고 `Holder` 액터에 ISM **5통**을 만든다: `ISM`(Sphere, `M_REBullet`, cd 2), `ArcISM`(Sphere, MID 주황, cd 2), `MarkerISM`(Plane, `M_ArenaMarker` MID), `ExplosionCoreISM`(Sphere, `M_REExplosionCore`, cd 1), `ExplosionRingISM`(Plane, `M_REExplosionRing`, cd 1). **6번째 통 `ExplosionSmokeISM` 추가.**

**`Source/Project_RE/Mass/REExplosionRenderProcessor.h/.cpp`** — 쿼리 없는 `UMassProcessor`. 익명 네임스페이스에 `ExplosionCoreRadiusStart/End`, `ExplosionRingRadiusStart/End`, `ExplosionPlaneBaseRadius`, `ExplosionRingZScale`, `SyncExplosionISM()`. `Execute` 가 `PruneAndGetLive` → 트랜스폼 2벌 + `Cd` 1벌 → `SyncExplosionISM` 2회 → `SetCustomData` 2회. **연기 반경 상수 + 3번째 벌 추가.**

**`Source/Project_RE/Mass/REExplosionFx.h/.cpp`** — **수정하지 않는다.** `ExplosionLifeSec = 0.8f`, CVar `re.Fx.Explosions`(1) / `re.Fx.ExplosionBudget`(256), `SpawnBulletExplosion` / `PruneAndGetLive`. 진행도 계약(슬롯 1개)이 불변이므로 손댈 곳이 없다.

**호출부 3곳 (수정하지 않는다)** — `RECharacterBase.cpp:530`, `REArcFxProcessor.cpp:74`, `REBulletHitProcessor.cpp:92`.

================================================================
## TASK 1: 튜닝 전 대조군 스크린샷
================================================================

**어떤 변경보다 먼저.** 뒤로 밀면 대조군이 사라진다. 커밋 없음(스크린샷은 리포에 넣지 않는다).

### 1-1. 두 장 촬영 (직선 / 곡사)

PowerShell:

```powershell
cd E:\UnrealProjects\Project_RE
$UE='E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$common='"E:\UnrealProjects\Project_RE\Project_RE.uproject" Main -game -windowed -ResX=1600 -ResY=900 -nosplash -NoSound '

# (1) 직선 — Spiral 고정. 기본 -1 은 랜덤 로테이션이라 전/후 대조가 성립하지 않는다.
$p = Start-Process $UE -PassThru -ArgumentList ($common +
  '-log=RE_d151_before_straight.log -ExecCmds="re.Debug.BossPattern 0,re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Debug.FakeHitTargets 16,re.Debug.ScreenshotFrame 900"')
if (-not $p.WaitForExit(180000)) { $p.Kill() }

# (2) 곡사 — Artillery 고정
$p = Start-Process $UE -PassThru -ArgumentList ($common +
  '-log=RE_d151_before_arc.log -ExecCmds="re.Debug.BossPattern 2,re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Debug.FakeHitTargets 16,re.Debug.ScreenshotFrame 900"')
if (-not $p.WaitForExit(180000)) { $p.Kill() }
```

### 1-2. 게이트

```bash
cd E:/UnrealProjects/Project_RE && ls -t --time-style=+%H:%M:%S -l Saved/Screenshots/WindowsEditor/ | head -4
```

기대: 방금 시각의 PNG **2장**이 새로 있다.

**두 파일 경로를 스크래치패드에 기록해라.** 나중에 `ls -t` 로 고르면 직후 런의 이미지를 집어 "안 변했다"고 오진한다.

============================================================
## TASK 2: 머티리얼 — 층별 톤 분리 + 연기 신규
============================================================

### 2-1. `scripts/make_explosion_material.py` (전문 교체)

```python
"""
M_REExplosionCore / M_REExplosionRing / M_REExplosionSmoke 부트스트랩 생성 (#149, #151).

폭발을 개별 Niagara 컴포넌트에서 ISM 인스턴스로 옮긴다(설계: specs/2026-09-13-explosion-ism-o1-design.md).
폭발 1개 = 코어 구체 1 + 수평 링 1 + 연기 구체 1 이고, 셋 다 퍼인스턴스 커스텀데이터[0]
(진행도 0→1)을 읽어 스스로 확장·감쇠한다. 진행도는 REExplosionRenderProcessor 가
매 프레임 써준다. 세 층이 같은 값을 받고 각자 다르게 해석한다.

왜 팩 텍스처(SubUV 플립북)를 쓰지 않는가:
  팩(Realistic_Starter_VFX_Pack_Vol2)은 gitignore 대상인데 기존 NS_REBulletExplosion 이
  팩 머티리얼 6개를 참조한다 — 팩 없는 클론에서 폭발이 깨진다. 프로시저럴로 그리면
  그 의존이 끊긴다.

Translucent 를 쓰는 이유와 additive 를 쓰지 않는 이유:
  마커(M_ArenaMarker)·빔(M_REBeam)과 같은 Unlit + Translucent 조합이다. additive 는
  아레나 바닥이 밝은 라벤더라 흰색으로 포화된다 — make_beam_material.py 에 되돌린 기록이 있다.
  연기 층은 애초에 '어두워야' 하므로 additive 는 방향이 반대다.

Strength 인자가 층별로 나뉜 이유 (#151):
  하나로 공유하면 코어의 흰색 포화를 잡으려 내릴 때 링까지 같이 죽는다. 링은 얇은
  원환이라 HDR 1.0 위로 태우는 것이 의도다(M_ArenaMarker 가 Color 를 3.0 으로 두는 규약).

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 생성 후 에디터에서 수치를 손보면 낡는다.
정본은 Content/Materials/M_REExplosion*.uasset 이다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
  인자: --force --core-strength=1.2 --ring-strength=3.0 --smoke-strength=0.15
        --opacity=0.9 --radius=0.45 --edge=0.10 --inner=0.30
        --smoke-opacity=0.45 --smoke-ramp=5.0 --smoke-rim=2.0
"""
import unreal

PKG = "/Game/Materials"
CMD = unreal.SystemLibrary.get_command_line()
mel = unreal.MaterialEditingLibrary


def _arg(name, default):
    """--name=값. -ExecutePythonScript="<경로> --x=1" 로 넘기면 마지막 토큰에 닫는
    따옴표가 붙어 오므로 strip 필수 — 없으면 float() 에서 죽는다."""
    for tok in CMD.split():
        tok = tok.strip("\"'")
        if tok.startswith("--%s=" % name):
            return float(tok.split("=", 1)[1])
    return default


FORCE = "--force" in [t.strip("\"'") for t in CMD.split()]


def make_material(name):
    """빈 Unlit/Translucent 머티리얼을 만들어 돌려준다. 이미 있으면 중단(또는 --force 로 삭제)."""
    full = PKG + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        if not FORCE:
            unreal.log_error("RE_EXPL: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % full)
            raise SystemExit(1)
        unreal.log_warning("RE_EXPL: --force - 기존 에셋을 지우고 재생성한다: %s" % full)
        unreal.EditorAssetLibrary.delete_asset(full)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(name, PKG, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        unreal.log_error("RE_EXPL: 생성 실패 - 아직 누가 참조 중일 수 있다: %s" % full)
        raise SystemExit(1)

    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    # ISM 으로 그려진다. 이 플래그가 없으면 엔진이 조용히 기본 머티리얼로 대체한다(#97 이력).
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    return mat, full


def helpers(mat, tag):
    """노드 생성·연결 헬퍼. 연결 실패를 조용히 넘기지 않는다."""
    def expr(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def link(frm, frm_out, to, to_in):
        if mel.connect_material_expressions(frm, frm_out, to, to_in):
            return
        if to_in and mel.connect_material_expressions(frm, frm_out, to, ""):
            return
        unreal.log_error("RE_EXPL(%s): 연결 실패 %s.%s -> %s.%s" % (
            tag, frm.get_class().get_name(), frm_out, to.get_class().get_name(), to_in))
        raise SystemExit(1)

    def scalar(pname, value, x, y):
        e = expr(unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", pname)
        e.set_editor_property("default_value", value)
        return e

    def vector(pname, value, x, y):
        e = expr(unreal.MaterialExpressionVectorParameter, x, y)
        e.set_editor_property("parameter_name", pname)
        e.set_editor_property("default_value", value)
        return e

    def progress(x, y):
        """퍼인스턴스 커스텀데이터[0] = 진행도(0→1). 렌더 프로세서가 매 프레임 쓴다."""
        e = expr(unreal.MaterialExpressionPerInstanceCustomData, x, y)
        e.set_editor_property("data_index", 0)
        e.set_editor_property("const_default_value", 0.0)
        return e

    def finish(emissive_node, opacity_node, full):
        if not mel.connect_material_property(emissive_node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
            unreal.log_error("RE_EXPL(%s): 이미시브 연결 실패" % tag)
            raise SystemExit(1)
        if not mel.connect_material_property(opacity_node, "", unreal.MaterialProperty.MP_OPACITY):
            unreal.log_error("RE_EXPL(%s): 오파시티 연결 실패" % tag)
            raise SystemExit(1)
        mel.recompile_material(mat)
        # only_if_is_dirty=False - MaterialEditingLibrary 변경은 dirty 플래그를 안 세워서
        # 기본값으로 부르면 로그만 찍히고 디스크에는 안 써진다(실측).
        unreal.EditorAssetLibrary.save_asset(full, False)
        unreal.log("RE_EXPL: 생성 완료 %s (blend=%s)" % (full, mat.get_editor_property("blend_mode")))

    return expr, link, scalar, vector, progress, finish


# ===== 코어 (구체) =========================================================
# 흰-노랑에서 주황-적으로 물들며 흐려진다. 마스크가 없다 — 구체 전면이 곧 불덩이다.
mat, full = make_material("M_REExplosionCore")
expr, link, scalar, vector, progress, finish = helpers(mat, "core")

prog = progress(-1200, 200)
fade = expr(unreal.MaterialExpressionOneMinus, -1000, 200)   # 1 → 0
link(prog, "", fade, "")

col_a = vector("Color",  unreal.LinearColor(1.0, 0.90, 0.55, 1.0), -1200, -120)  # 초반: 흰-노랑
col_b = vector("ColorB", unreal.LinearColor(1.0, 0.25, 0.03, 1.0), -1200, 20)    # 후반: 주황-적
col = expr(unreal.MaterialExpressionLinearInterpolate, -900, -60)
link(col_a, "", col, "A")
link(col_b, "", col, "B")
link(prog, "", col, "Alpha")

# 3.0 은 진행도 0 에서 이미시브가 (3.00, 2.70, 1.65) 라 세 채널이 전부 1 을 넘겨
# 톤매퍼에서 흰색으로 뭉갰다(#151). 1.2 면 R 만 살짝 타고 G·B 는 아래라 노랑-주황이
# 채널 비율로 남는다. 링과 인자를 나눈 이유는 링은 태우는 게 의도라서다.
strength = scalar("Strength", _arg("core-strength", 1.2), -900, 120)
lit = expr(unreal.MaterialExpressionMultiply, -650, 0)
link(col, "", lit, "A")
link(strength, "", lit, "B")

emissive = expr(unreal.MaterialExpressionMultiply, -400, 60)
link(lit, "", emissive, "A")
link(fade, "", emissive, "B")

opacity_p = scalar("Opacity", _arg("opacity", 0.9), -650, 300)
opacity = expr(unreal.MaterialExpressionMultiply, -400, 260)
link(fade, "", opacity, "A")
link(opacity_p, "", opacity, "B")

finish(emissive, opacity, full)

# ===== 링 (수평 원환) ======================================================
# UV 중심 거리로 원환 마스크를 만든다. 링이 퍼지는 것은 머티리얼이 아니라 인스턴스
# 스케일이 한다(렌더 프로세서) — UV 공간 형상은 고정이다.
mat, full = make_material("M_REExplosionRing")
expr, link, scalar, vector, progress, finish = helpers(mat, "ring")

uv = expr(unreal.MaterialExpressionTextureCoordinate, -1600, 0)
center = expr(unreal.MaterialExpressionConstant2Vector, -1600, 160)
center.set_editor_property("r", 0.5)
center.set_editor_property("g", 0.5)
dist = expr(unreal.MaterialExpressionDistance, -1400, 60)
link(uv, "", dist, "A")
link(center, "", dist, "B")

radius = scalar("Radius", _arg("radius", 0.45), -1600, 320)
edge = scalar("Edge", _arg("edge", 0.10), -1600, 420)
inner = scalar("Inner", _arg("inner", 0.30), -1600, 520)

# 바깥 경계: dist < Radius 면 1, 밖으로 Edge 만큼 걸쳐 0
outer_d = expr(unreal.MaterialExpressionSubtract, -1150, 120)
link(radius, "", outer_d, "A")
link(dist, "", outer_d, "B")
outer_n = expr(unreal.MaterialExpressionDivide, -950, 120)
link(outer_d, "", outer_n, "A")
link(edge, "", outer_n, "B")
outer = expr(unreal.MaterialExpressionClamp, -760, 120)
link(outer_n, "", outer, "Input")

# 안쪽 경계: dist > Inner 면 1 → 가운데가 비어 '링'이 된다
inner_d = expr(unreal.MaterialExpressionSubtract, -1150, 320)
link(dist, "", inner_d, "A")
link(inner, "", inner_d, "B")
inner_n = expr(unreal.MaterialExpressionDivide, -950, 320)
link(inner_d, "", inner_n, "A")
link(edge, "", inner_n, "B")
inner_m = expr(unreal.MaterialExpressionClamp, -760, 320)
link(inner_n, "", inner_m, "Input")

ring = expr(unreal.MaterialExpressionMultiply, -560, 220)
link(outer, "", ring, "A")
link(inner_m, "", ring, "B")

prog = progress(-1600, 660)
fade = expr(unreal.MaterialExpressionOneMinus, -1400, 660)
link(prog, "", fade, "")

ring_fade = expr(unreal.MaterialExpressionMultiply, -380, 320)
link(ring, "", ring_fade, "A")
link(fade, "", ring_fade, "B")

col = vector("Color", unreal.LinearColor(1.0, 0.75, 0.35, 1.0), -760, 520)
# 링은 얇은 원환이라 픽셀 몇 줄로 충격파를 읽혀야 한다 — HDR 1.0 위로 태우는 것이
# 의도다(M_ArenaMarker 가 Color 를 3.0 으로 두는 것과 같은 규약). 값은 #149 그대로고
# 코어와 인자만 나눴다.
strength = scalar("Strength", _arg("ring-strength", 3.0), -760, 620)
lit = expr(unreal.MaterialExpressionMultiply, -560, 540)
link(col, "", lit, "A")
link(strength, "", lit, "B")

emissive = expr(unreal.MaterialExpressionMultiply, -160, 420)
link(lit, "", emissive, "A")
link(ring_fade, "", emissive, "B")

opacity_p = scalar("Opacity", _arg("opacity", 0.9), -560, 760)
opacity = expr(unreal.MaterialExpressionMultiply, -160, 700)
link(ring_fade, "", opacity, "A")
link(opacity_p, "", opacity, "B")

finish(emissive, opacity, full)

# ===== 연기 대역 (큰 반투명 구체) ==========================================
# 코어보다 크고 어둡고 늦게까지 남는다. 이미시브가 거의 검정이라 '빛나는 층'이 아니라
# '배경을 가리는 층'이다 — 세 층 중 실루엣에 부피를 주는 것이 이 층 하나다.
#
# 코어/링과 다른 점 셋:
#   1) 알파가 (1 - p^3) — 선형 (1 - p) 보다 늦게까지 남는다. 수명(0.8s)은 공유한다.
#      수명을 늘리는 대신 커브로 하는 이유: ExplosionLifeSec 이 진행도의 분모라
#      건드리면 코어·링의 반경 곡선과 감쇠가 전부 바뀌고 #149 의 룩 판정이 무효화된다
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

### 2-2. 게이트

**(a) 파싱**

```bash
cd E:/UnrealProjects/Project_RE && python -c "import ast; ast.parse(open('scripts/make_explosion_material.py',encoding='utf-8').read()); print('OK')"
```

기대: `OK`

**(b) 에셋 재생성.** 에디터가 열려 있으면 먼저 닫아라(에셋 락). 기존 에셋이 있으므로 **`--force` 필수.**

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -ExecutePythonScript="E:/UnrealProjects/Project_RE/scripts/make_explosion_material.py --force" \
  -unattended -nosplash -nop4 2>&1 | grep -E "RE_EXPL"
```

기대 (**세 줄 다** 나와야 한다. `--force` 경고 3줄이 섞여 나오는 것은 정상):
```
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionCore (blend=BlendMode.BLEND_TRANSLUCENT)
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionRing (blend=BlendMode.BLEND_TRANSLUCENT)
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionSmoke (blend=BlendMode.BLEND_TRANSLUCENT)
```

`RE_EXPL: 연결 실패` 가 보이면 노드 입력 이름이 틀린 것이다 — `ExponentIn` / `Input` / `A` / `B` 를 확인하고 고쳐서 다시 돌려라. 세 줄 중 하나라도 없으면 **커밋하지 말 것.**

**(c) 디스크 확인**

```bash
cd E:/UnrealProjects/Project_RE && ls -la Content/Materials/M_REExplosion*.uasset && git status --short Content/Materials/
```

기대: 파일 3개 존재(크기 0 아님), `M Content/Materials/M_REExplosionCore.uasset` / `M ...Ring.uasset` / `?? ...Smoke.uasset`.
**없으면 `save_asset` 의 `only_if_is_dirty` 문제다** — 로그에 "생성 완료"가 찍혔어도 디스크에 없을 수 있다.

### 2-3. 커밋

```bash
cd E:/UnrealProjects/Project_RE && git add scripts/make_explosion_material.py Content/Materials/M_REExplosionCore.uasset Content/Materials/M_REExplosionRing.uasset Content/Materials/M_REExplosionSmoke.uasset && git commit -m "$(cat <<'EOF'
feat(fx): 폭발 코어 톤 다운 + 연기 대역 머티리얼 (#151)

코어 Strength 를 3.0 에서 1.2 로 내린다. 3.0 은 진행도 0 에서 이미시브가
(3.00, 2.70, 1.65) 라 세 채널이 전부 1 을 넘겨 톤매퍼에서 흰색으로 뭉갰다.
1.2 면 R 만 살짝 타고 G·B 는 아래라 노랑-주황이 채널 비율로 남는다.

Strength 인자를 층별로 쪼갰다(--core-strength / --ring-strength / --smoke-strength).
공유 인자면 코어를 내릴 때 링도 같이 죽는다 — 링은 얇은 원환이라 HDR 1.0 위로
태우는 것이 의도다(M_ArenaMarker 가 Color 를 3.0 으로 두는 것과 같은 규약).
링 값은 3.0 그대로다.

연기 대역(M_REExplosionSmoke) 신규. 이미시브가 거의 검정이라 빛나는 층이 아니라
배경을 가리는 층이고, 세 층 중 실루엣에 부피를 주는 것이 이 층 하나다. 코어/링과
다른 점 셋:

  1) 알파가 (1 - p^3) — 선형 (1 - p) 보다 늦게까지 남는다. 수명을 늘리지 않고
     커브로 한 이유는 ExplosionLifeSec 이 진행도의 분모라 건드리면 코어·링의
     반경 곡선과 감쇠가 전부 바뀌어 #149 의 룩 판정이 무효화되기 때문이다
  2) 페이드-인 saturate(p * RampIn) — 없으면 스폰 프레임에서 어두운 구체가 최대
     알파로 떠서 폭발이 회색으로 열린다(프레넬 역마스크가 하필 중심에서 가장
     불투명해 그 뒤의 밝은 코어를 정확히 가린다)
  3) 프레넬 역마스크 — 실루엣 가장자리에서 알파 0. 없으면 딱딱한 공 테두리가
     보이고, 그건 고치려는 납작함을 하나 더 얹는 것이다

커스텀데이터 계약은 불변이다: 슬롯 1개 [0]=진행도, 세 층이 같은 값을 받는다.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

============================================================
## TASK 3: ISM 3번째 통 (연기)
============================================================

### 3-1. `Source/Project_RE/Mass/REBulletRenderSubsystem.h` (전문 교체)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "REBulletRenderSubsystem.generated.h"

class AActor;
class UInstancedStaticMeshComponent;

/**
 *  탄막 ISM 소유자. Processor는 UMassProcessor(액터 아님)라 ISM을 직접 못 가진다.
 *  월드 BeginPlay 시 홀더 액터를 스폰해 ISM(빨강 엔진 구체)을 부착하고 GetISM()로 노출.
 *  데디서버(NM_DedicatedServer)는 렌더 불요 → 홀더/ISM 생성 skip(GetISM()이 null).
 */
UCLASS()
class UREBulletRenderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UInstancedStaticMeshComponent* GetISM() const { return ISM; }
	UInstancedStaticMeshComponent* GetArcISM() const { return ArcISM; }
	UInstancedStaticMeshComponent* GetMarkerISM() const { return MarkerISM; }
	UInstancedStaticMeshComponent* GetExplosionCoreISM() const { return ExplosionCoreISM; }
	UInstancedStaticMeshComponent* GetExplosionRingISM() const { return ExplosionRingISM; }
	UInstancedStaticMeshComponent* GetExplosionSmokeISM() const { return ExplosionSmokeISM; }

private:
	UPROPERTY()
	TObjectPtr<AActor> Holder = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ISM = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ArcISM = nullptr;     // 곡사탄(주황 구체, Z 궤적)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MarkerISM = nullptr;  // 착지 예고(빨강 평면 원)

	// 폭발 (#149, #151) — 폭발 1개 = 아래 세 통에 인스턴스 1개씩. 컴포넌트를 만들지
	// 않으므로 드로우콜이 동시 폭발 개수와 무관하다(층 수가 상한).
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionCoreISM = nullptr;   // 불덩이(구체)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionRingISM = nullptr;   // 수평 충격파(평면 원환)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionSmokeISM = nullptr;  // 연기 대역(큰 어두운 구체)
};
```

### 3-2. `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp` (부분 삽입)

`ExplosionRingISM` 블록의 `M_REExplosionRing 로드 실패` 로그를 닫는 `}` **바로 뒤**, 마지막 `// 실제 적용된 머티리얼 이름을 찍는다` 블록 **앞**에 아래를 넣는다. 그 앞뒤 코드는 손대지 않는다.

```cpp

	// 연기 대역 (#151) — 세 층 중 가장 크고 어둡다. 코어·링과 같은 설정이고 메시도
	// 같은 구체다. 등록 순서가 코어 → 링 → 연기인 것은 의도다: 반투명 정렬이 컴포넌트
	// 단위라 순서가 화면에 남는다(연기가 코어를 삼키면 이 순서부터 본다).
	ExplosionSmokeISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	ExplosionSmokeISM->SetupAttachment(ISM);
	ExplosionSmokeISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExplosionSmokeISM->bAffectDynamicIndirectLighting = false;
	ExplosionSmokeISM->bAffectDistanceFieldLighting = false;
	ExplosionSmokeISM->SetCastShadow(false);   // 탄환과 같은 이유 (#95)
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
		// 조용한 폴백 금지 — 기본 머티리얼로 렌더되면 커다란 회색 구체가 떠서
		// 폭발이 통째로 가려진다.
		UE_LOG(LogREBullet, Error, TEXT("[RE] M_REExplosionSmoke 로드 실패 — 폭발 연기가 기본 머티리얼로 렌더된다 (#151)"));
	}
```

### 3-3. 게이트

빌드는 TASK 4 와 묶어 한 번만 한다 — 이 시점의 중간 상태는 연기 ISM 이 비어 있을 뿐 정상 컴파일된다.

```bash
cd E:/UnrealProjects/Project_RE && grep -c "ExplosionSmokeISM" Source/Project_RE/Mass/REBulletRenderSubsystem.cpp
```

기대: `10` (`grep -c` 는 매치된 **줄** 수다)

### 3-4. 커밋

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Mass/REBulletRenderSubsystem.h Source/Project_RE/Mass/REBulletRenderSubsystem.cpp && git commit -m "$(cat <<'EOF'
feat(fx): 폭발 연기 ISM 3번째 통 (#151)

코어·링과 같은 설정(NoCollision / Lumen 제외 / 그림자 off / 커스텀데이터 1)이고
메시도 같은 엔진 구체다. 등록 순서가 코어 → 링 → 연기인 것은 의도다 — 반투명
정렬이 컴포넌트 단위라 순서가 화면에 남는다.

머티리얼 로드 실패를 조용히 넘기지 않는다. 연기는 세 층 중 가장 커서 기본 머티리얼로
떨어지면 폭발이 통째로 회색 구체에 가린다.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

============================================================
## TASK 4: 렌더 프로세서 — 연기 층 동기
============================================================

### 4-1. `Source/Project_RE/Mass/REExplosionRenderProcessor.h` (전문 교체)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "REExplosionRenderProcessor.generated.h"

/**
 *  폭발 ISM 동기 (#149, #151).
 *
 *  REExplosionFx 의 살아있는 목록을 세 ISM(코어 구체 / 수평 링 / 연기 구체)에
 *  프레임당 한 번 배치 반영한다. 폭발 1개 = 인스턴스 3개이므로 드로우콜이 동시 폭발
 *  개수와 무관하다.
 *
 *  **엔티티를 읽지 않는다 — 쿼리가 없다.** UMassProcessor 를 쓰는 이유는 두 가지다:
 *  (1) ExecutionFlags 로 데디서버 제외가 선언적으로 되고, (2) bRequiresGameThreadExecution
 *  으로 GT 고정이 보장된다(ISM 변형은 GT 전용). 기존 ISM 동기가 전부 프로세서에
 *  있는 배치와도 맞는다(REBulletRenderProcessor / REArcRenderProcessor).
 *
 *  쿼리가 없으므로 생성자에서 QueryBasedPruning 을 Never 로 꺼야 한다 — 기본값
 *  Prune 이면 쿼리 0개인 프로세서가 런타임에 통째로 프루닝돼 Execute 가 한 번도
 *  안 돈다(조용한 dead code).
 *
 *  탄환 렌더 프로세서에 얹지 않는 이유: 그 함수는 CSV_SCOPED_TIMING_STAT(REBullet,
 *  BulletRender) 안쪽이라 폭발 비용이 BulletRender 로 집계된다. 프로파일 문서와
 *  포트폴리오가 그 숫자를 쓴다.
 */
UCLASS()
class UREExplosionRenderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UREExplosionRenderProcessor();

protected:
	// ConfigureQueries 는 오버라이드하지 않는다 — 베이스가 순수 가상이 아니고, 읽을
	// 엔티티가 없다.
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;
};
```

### 4-2. `Source/Project_RE/Mass/REExplosionRenderProcessor.cpp` (전문 교체)

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionRenderProcessor.h"
#include "REExplosionFx.h"
#include "REBulletGeometry.h"                        // EngineSphereRadius — 메시 기본 반경 단일 출처
#include "REBulletRenderSubsystem.h"
#include "MassExecutionContext.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

CSV_DECLARE_CATEGORY_EXTERN(REBullet);  // 정의는 REBulletSimProcessor.cpp

namespace
{
	/** 코어(구체) 반경(cm). 탄 지름 50, HitRadius 60 보다 커야 폭발로 읽힌다. */
	constexpr float ExplosionCoreRadiusStart = 20.f;
	constexpr float ExplosionCoreRadiusEnd   = 120.f;

	/** 링(수평 원환) 반경(cm). 코어보다 빠르고 넓게 퍼진다. */
	constexpr float ExplosionRingRadiusStart = 40.f;
	constexpr float ExplosionRingRadiusEnd   = 220.f;

	/** 연기 대역(구체) 반경(cm) (#151). 시작은 코어(20)보다 크되 링(40)보다 작게 —
	 *  스폰 순간 코어를 삼키지 않는다. 끝은 링(220)보다 크게 — 마지막까지 남는 층이다. */
	constexpr float ExplosionSmokeRadiusStart = 30.f;
	constexpr float ExplosionSmokeRadiusEnd   = 260.f;

	/** /Engine/BasicShapes/Plane 은 100x100 이라 반경 50. 스케일 = 반경/50.
	 *  (마커 쪽 CylinderBaseRadius 와 같은 값이지만 이름을 다르게 둔다 — 익명
	 *  네임스페이스 동명 상수가 유니티 빌드에서 충돌한 전례가 있다.) */
	constexpr float ExplosionPlaneBaseRadius = 50.f;

	/** 링 Z 스케일. **1.0 미만으로 두지 말 것** — XY 를 키운 상태에서 Z 를 낮추면
	 *  인스턴스가 화면에서 통째로 사라진다(마커에서 실RHI 스크린샷 이진탐색으로 확인:
	 *  0.02/0.15/0.4 전부 무렌더, 등방 2.0 은 정상. ISM 극단 비등방 스케일의 컬링/
	 *  바운즈 이슈로 추정, 엔진 레벨·원인 미상 — REArcRenderProcessor.cpp 참조).
	 *  코어와 연기는 등방 구체라 이 함정에 걸리지 않는다. */
	constexpr float ExplosionRingZScale = 1.0f;

	/** 인스턴스 수를 목표에 맞춘다. 꼬리에서 add/remove 하므로 다른 인덱스가 안 밀린다. */
	void SyncExplosionISM(UInstancedStaticMeshComponent* ISM, const TArray<FTransform>& Xf)
	{
		int32 Count = ISM->GetInstanceCount();
		const int32 N = Xf.Num();
		while (Count < N) { ISM->AddInstance(FTransform::Identity, /*bWorldSpace=*/true); ++Count; }
		while (Count > N) { ISM->RemoveInstance(Count - 1);                               --Count; }
		if (N > 0)
		{
			// 인스턴스당 개별 UpdateInstanceTransform 은 개수에 비례해 GT 를 먹는다 (#95).
			ISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace=*/true,
				/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
		}
	}
}

UREExplosionRenderProcessor::UREExplosionRenderProcessor()
{
	// 데디서버 skip — 렌더가 없다.
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Client);

	// ISM(씬 컴포넌트) 변형은 게임 스레드 전용 — AddInstance 가 물리 바디를 만들어
	// 워커 스레드에서 어서션 크래시한다.
	bRequiresGameThreadExecution = true;

	// 쿼리가 없는 프로세서는 기본값(Prune)에서 런타임에 통째로 프루닝돼 Execute 가
	// 한 번도 안 돈다. 끄지 않으면 이 파일 전체가 조용한 dead code 가 된다.
	QueryBasedPruning = EMassQueryBasedPruning::Never;
}

void UREExplosionRenderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RE_ExplosionRender);
	CSV_SCOPED_TIMING_STAT(REBullet, ExplosionRender);

	UWorld* World = EntityManager.GetWorld();
	if (!World)
	{
		return;
	}

	// 만료 제거를 ISM 확인보다 **먼저** 한다. 뒤에 두면 ISM 이 없는 월드에서 목록이
	// 영원히 안 비고, 예산(re.Fx.ExplosionBudget)이 가득 찬 채 잠겨 폭발이 조용히
	// 전량 버려진다.
	const TArray<REExplosionFx::FLiveExplosion>& Live =
		REExplosionFx::PruneAndGetLive(World->GetTimeSeconds());

	UREBulletRenderSubsystem* RS = World->GetSubsystem<UREBulletRenderSubsystem>();
	UInstancedStaticMeshComponent* CoreISM  = RS ? RS->GetExplosionCoreISM()  : nullptr;
	UInstancedStaticMeshComponent* RingISM  = RS ? RS->GetExplosionRingISM()  : nullptr;
	UInstancedStaticMeshComponent* SmokeISM = RS ? RS->GetExplosionSmokeISM() : nullptr;
	if (!CoreISM || !RingISM || !SmokeISM)
	{
		return;
	}

	const int32 M = Live.Num();
	TArray<FTransform> CoreXf;
	TArray<FTransform> RingXf;
	TArray<FTransform> SmokeXf;
	TArray<float> Cd;
	CoreXf.Reserve(M);
	RingXf.Reserve(M);
	SmokeXf.Reserve(M);
	Cd.Reserve(M);

	for (const REExplosionFx::FLiveExplosion& E : Live)
	{
		const float CoreR  = FMath::Lerp(ExplosionCoreRadiusStart,  ExplosionCoreRadiusEnd,  E.Progress);
		const float RingR  = FMath::Lerp(ExplosionRingRadiusStart,  ExplosionRingRadiusEnd,  E.Progress);
		const float SmokeR = FMath::Lerp(ExplosionSmokeRadiusStart, ExplosionSmokeRadiusEnd, E.Progress);

		// 구체는 등방 스케일이고 회전이 의미 없다. 카메라가 월드 고정(pitch -50 절대)이라
		// 빌보드 갱신도 필요 없다.
		const float CoreS = CoreR / REBulletGeometry::EngineSphereRadius;
		CoreXf.Add(FTransform(FRotator::ZeroRotator, E.Loc, FVector(CoreS)));

		// Plane 은 XY 평면(법선 +Z)이라 회전 없이 그대로 수평 원판이다.
		// Z 는 1.0 고정 — 위 ExplosionRingZScale 주석의 함정.
		const float RingS = RingR / ExplosionPlaneBaseRadius;
		RingXf.Add(FTransform(FRotator::ZeroRotator, E.Loc,
			FVector(RingS, RingS, ExplosionRingZScale)));

		// 연기도 등방 구체라 링과 달리 Z 스케일 함정에 걸리지 않는다 (#151).
		const float SmokeS = SmokeR / REBulletGeometry::EngineSphereRadius;
		SmokeXf.Add(FTransform(FRotator::ZeroRotator, E.Loc, FVector(SmokeS)));

		Cd.Add(E.Progress);
	}

	SyncExplosionISM(CoreISM,  CoreXf);
	SyncExplosionISM(RingISM,  RingXf);
	SyncExplosionISM(SmokeISM, SmokeXf);

	// 인스턴스 수를 맞춘 뒤라야 SetCustomData 의 인덱스 범위가 유효하다.
	// 세 층이 **같은** 진행도를 받고 각자 다르게 해석한다 — 커스텀데이터 슬롯은 1개다.
	if (M > 0)
	{
		CoreISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		RingISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		SmokeISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
	}
}
```

### 4-3. 게이트

**(a) Editor 빌드** — 전역 제약의 커맨드. 기대 `Result: Succeeded`. `Target is up to date` 면 실패다.

**(b) headless 프로브 — 연기 머티리얼이 실제로 붙었는지 확인**

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor.exe" \
  "E:/UnrealProjects/Project_RE/Project_RE.uproject" Main \
  -game -nullrhi -unattended -nosplash -NoSound -log=RE_d151_probe.log
```

```bash
cd E:/UnrealProjects/Project_RE && grep -E "ExplosionProbe|M_REExplosion|has no registered queries" Saved/Logs/RE_d151_probe.log | head -20
```

기대:
- `[RE] ExplosionProbe: 스폰=N 버림=0 / ...초 (.../s, 동시=M)` 이 **한 줄 이상** — 프로세서가 돈다는 증거
- `M_REExplosionSmoke 로드 실패` 가 **없어야** 한다 (Core / Ring 도 마찬가지)
- `has no registered queries` 경고가 보이면 `QueryBasedPruning = Never` 가 안 걸린 것이다

`-nullrhi` 로는 **룩을 판정하지 않는다** — ISM Bounds=0 오진 전례가 있다. 룩은 TASK 5.

### 4-4. 커밋

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Mass/REExplosionRenderProcessor.h Source/Project_RE/Mass/REExplosionRenderProcessor.cpp && git commit -m "$(cat <<'EOF'
feat(fx): 폭발 연기 층 렌더 — 드로우콜 상수 4에서 6으로 (#151)

살아있는 폭발 목록을 세 ISM 에 배치 반영한다. 폭발 1개 = 인스턴스 3개라 드로우콜은
여전히 동시 개수와 무관하고, 상수만 층당 +2 씩 오른다(#149 §9.1 실측).

연기 반경 30 → 260uu. 시작은 코어(20)보다 크되 링(40)보다 작게 둬서 스폰 순간
코어를 삼키지 않고, 끝은 링(220)보다 크게 둬서 마지막까지 남는 층이 된다.
등방 구체라 링의 Z 스케일 함정에는 걸리지 않는다.

커스텀데이터 배열은 세 층이 공유한다 — 슬롯 1개 [0]=진행도, 해석만 머티리얼별로
다르다. 층별 슬롯을 쪼개는 대신 이렇게 둔 이유는 얻는 것이 알파 커브 하나인데
계약이 복잡해지기 때문이다.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

============================================================
## TASK 5: 튜닝 후 스크린샷 + 연출 판정
============================================================

### 5-1. 두 장 촬영 (TASK 1 과 같은 조건)

```powershell
cd E:\UnrealProjects\Project_RE
$UE='E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$common='"E:\UnrealProjects\Project_RE\Project_RE.uproject" Main -game -windowed -ResX=1600 -ResY=900 -nosplash -NoSound '

$p = Start-Process $UE -PassThru -ArgumentList ($common +
  '-log=RE_d151_after_straight.log -ExecCmds="re.Debug.BossPattern 0,re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Debug.FakeHitTargets 16,re.Debug.ScreenshotFrame 900"')
if (-not $p.WaitForExit(180000)) { $p.Kill() }

$p = Start-Process $UE -PassThru -ArgumentList ($common +
  '-log=RE_d151_after_arc.log -ExecCmds="re.Debug.BossPattern 2,re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Debug.FakeHitTargets 16,re.Debug.ScreenshotFrame 900"')
if (-not $p.WaitForExit(180000)) { $p.Kill() }
```

**새 파일인지 타임스탬프로 확인해라** — `ls -t` 만 믿으면 직전 런의 이미지를 집어 "안 변했다"고 오진한다.

### 5-2. 판정 (TASK 1 대조군과 나란히 본다)

| # | 볼 것 | 무너지면 |
|---|---|---|
| 1 | 코어가 흰 덩어리가 아니라 **노랑-주황**인가 | `--core-strength` 1.2 → 0.8 |
| 2 | 폭발이 **회색으로 열리지** 않는가 | `--smoke-ramp` 5.0 → 8.0 |
| 3 | 연기 가장자리가 **딱딱한 공 테두리**가 아닌가 | `--smoke-rim` 2.0 → 3.5 |
| 4 | 연기가 코어를 **통째로 가리지** 않는가 | `--smoke-opacity` 0.45 → 0.25 |
| 5 | 곡사 착지 연기가 **바닥에 묻히지** 않는가 | `ExplosionSmokeRadiusEnd` 260 → 200 |

**1~4 는 머티리얼 파라미터 재생성 루프다.** TASK 2-2 의 `--force` 커맨드에 값을 얹어 다시 돌리고 스크린샷만 다시 찍는다 — 빌드 불필요:

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -ExecutePythonScript="E:/UnrealProjects/Project_RE/scripts/make_explosion_material.py --force --smoke-opacity=0.25" \
  -unattended -nosplash -nop4 2>&1 | grep -E "RE_EXPL"
```

**값을 바꿨으면 스크립트 기본값도 같이 바꿔라.** 안 그러면 다음 `--force` 에 되돌아간다 — 정본은 `.uasset` 이지만 스크립트가 낡으면 재현이 깨진다. 5번(반경)만 C++ 이라 빌드가 필요하다.

**판단이 서지 않으면 스크린샷 4장(전 2 / 후 2)을 사람에게 보여주고 물어라.** 이 태스크의 판정 기준은 주관적이고, 그게 이 이슈의 본질이다.

### 5-3. 커밋 (값이 바뀐 경우에만)

```bash
cd E:/UnrealProjects/Project_RE && git add scripts/make_explosion_material.py Content/Materials/M_REExplosion*.uasset && git commit -m "$(cat <<'EOF'
fix(fx): 폭발 연출 파라미터 스크린샷 튜닝 (#151)

<바꾼 값과 스크린샷에서 본 근거를 한 줄씩>

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

============================================================
## TASK 6: 측정 + 설계 문서에 결과 기록
============================================================

### 6-1. 프로파일 3런

```powershell
cd E:\UnrealProjects\Project_RE
# (1) 기준선 — 하네스 기본이 re.Fx.Explosions 0 이라 폭발이 꺼진 런이다
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Debug.FakeHitTargets 16" -Label d151_off

# (2) 예산 OFF — 드로우콜 상수성 게이트. #149 는 같은 조건에서 Draws 229 였다
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.FakeHitTargets 16,re.Fx.ExplosionBudget 0" -Label d151_nobudget

# (3) 출하 설정(예산 256)
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.FakeHitTargets 16" -Label d151_budget256
```

동시 개수는 런 로그에서 읽는다:

```bash
cd E:/UnrealProjects/Project_RE && grep "ExplosionProbe" $(ls -td Saved/Profiling/RE_Mass_5000_d151_nobudget_*/ | head -1)run.log | tail -10
```

### 6-2. 수치 추출 (손으로 CSV 를 파싱하지 말 것)

```powershell
cd E:\UnrealProjects\Project_RE
scripts\profile-stats.ps1 -RunDir (Get-ChildItem Saved\Profiling\RE_Mass_5000_d151_* -Directory).FullName
```

**판정:**
- **`Draws`**(`nobudget`) 가 #149 의 **229 대비 +2 수준(≈231)** — 층당 +2 실측과 일치. 크게 벗어나면(특히 동시 개수에 비례하면) 연기가 인스턴스로 배칭되지 않은 것이다 → 머티리얼의 `used_with_instanced_static_meshes` 부터 본다
- **`Translu`**(`Exclusive/RenderThread/RenderTranslucency`) 는 **오를 것으로 예상**한다(반투명 층 +1, 그중 가장 큰 구체). #149 의 mean 0.18 / p99 0.54 대비 **얼마나** 올랐는지를 기록한다
- `Frame` / `RT` 가 기준선(`d151_off`) 대비 무너지면 설계 §8 의 `re.Fx.ExplosionBudget` 노브를 꺼낼 근거다. **이 작업에서 기본값을 바꾸지는 않는다** — 수치를 적고 후속 이슈로 남긴다

### 6-3. 설계 문서 기록 + 커밋

`docs/superpowers/specs/2026-09-13-explosion-depth-design.md` 의 §9 표 **아래**에 `### 9.1 측정 결과 (2026-XX-XX)` 절을 만든다. 반드시 포함:

- 3런 표 (동시 폭발 / Draws mean·p99 / Frame / RT / Translu mean·p99)
- **#149 §9.1 과의 대조** — 드로우콜 상수가 4 → 6 으로 올랐는지, 여전히 동시 개수와 무상관인지
- 오버드로우 실측: `Translu` 가 실제로 얼마나 올랐는지, §8 예측과 맞는지
- 연출 판정: 스크린샷 5항목 결과, 초기값에서 바꾼 파라미터와 그 이유. **초기값 그대로 통과했으면 그렇게 쓴다**

```bash
cd E:/UnrealProjects/Project_RE && git add docs/superpowers/specs/2026-09-13-explosion-depth-design.md && git commit -m "$(cat <<'EOF'
docs: 폭발 연출 깊이 실측 결과 (#151)

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

============================================================
## TASK 7: 풀 유니티 빌드 게이트 + 푸시
============================================================

### 7-1. Server 타깃 풀 유니티 빌드

유니티 섀도잉(C4459)은 이 구성에서만 잡힌다. Editor 빌드를 통과해도 여기서 깨진 전례가 있다.

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REServer Win64 Development \
  -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -DisableAdaptiveUnity -WaitMutex
```

기대: `Result: Succeeded`. 데디서버 타깃이라 폭발 코드가 실행되지는 않지만 **컴파일은 된다** — 그게 이 게이트의 목적이다.

### 7-2. 푸시

```bash
cd E:/UnrealProjects/Project_RE && git push -u origin feature/M8-explosion-depth
```

## 완료 후

**PR 생성.** base 는 **`dev`** 다(Gitflow — `main` 직접 금지). 이슈 #151 의 메타를 전부 미러링한다.

본문은 긴 마크다운이라 **파일로 쓴 뒤 `--body-file`** 로 넘긴다(인라인 문자열은 따옴표 매칭이 깨진다). 스크래치패드에 쓴다 — 리포를 더럽히지 않는다.

```bash
cd E:/UnrealProjects/Project_RE && gh pr create --base dev \
  --title "[M8] 폭발 연출 깊이 — 코어 톤 튜닝 + 연기 대역 3번째 ISM 층 (#151)" \
  --body-file "$TMPDIR/pr151.md" \
  --label enhancement --label architecture --label "C++" \
  --milestone "M8: 포트폴리오 하드닝 — 동작 불변 리팩터링" \
  --assignee leejimin3
```

프로젝트는 생성 후 붙인다(`create` 에서 지정이 까다롭다). PR 번호는 물어보지 말고 브랜치에서 얻는다:

```bash
cd E:/UnrealProjects/Project_RE && PR=$(gh pr view --json number --jq .number) && \
  gh pr edit "$PR" --add-project "Project_RE 개발 로드맵" && \
  gh pr view "$PR" --json labels,milestone,assignees,projectItems --jq '{labels:[.labels[].name],milestone:.milestone.title,assignees:[.assignees[].login],projects:[.projectItems[].title]}'
```

기대: 네 필드 전부 비어있지 않고 이슈 #151 과 같다.

PR 본문에 반드시 넣을 것:
- 문제: 코어 흰색 포화(진행도 0 에서 `(3.00, 2.70, 1.65)` 채널값 근거) + 층 2개의 납작함
- (a)/(b) 두 갈래와 각각의 비용 — (a) C++ 0줄·드로우콜 변화 0, (b) 드로우콜 +2 상수
- **TASK 6 의 실측 표** — `Draws` 상수가 4 → 6 이고 여전히 동시 개수와 무상관, `Translu` 실제 증가폭
- 연기 층의 세 가지 설계 결정(1−p³ 커브 / 페이드-인 / 프레넬 역마스크)과 각각 없으면 뭐가 깨지는지
- 기각한 대안: 공유 `--strength` 유지 / `ExplosionLifeSec` 연장 / 층별 커스텀데이터 슬롯 / additive / 감산 블렌드
- 게이트 표 (Editor 빌드 / Server 풀 유니티 / headless 프로브 / 스크린샷 4장 / 프로파일 3런)
- 한계: 오버드로우는 층이 늘어 **올라간다**(측정값 명시), 주황 그라데이션은 절반만 해결
- 끝에:
  ```
  🤖 Generated with [Claude Code](https://claude.com/claude-code)

  https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
  ```

**남은 의도된 TODO (후속 이슈 몫 — 건드리지 말고 PR 본문에 적기만):**
- **코어 그라데이션 알파 리맵.** `Color`→`ColorB` 보간 알파와 밝기 감쇠가 둘 다 진행도라 **주황이 최대치일 때 알파가 0** 이다. `Strength` 튜닝은 흰색 포화만 잡는다. 남은 절반은 lerp 알파에 `clamp(진행도 × ColorShift)` 를 끼우는 것 — 노드 2개
- 4번째 층(스파크 / 잔불 / 열왜곡)
- 연출 종류 분기(피격 / 착지)
- `re.Fx.ExplosionBudget` 기본값 재산정 — 이번 측정이 근거를 주면
- `convert_explosion_fx.py` / `NS_REBulletExplosion` 정리, M7 프로파일 문서 재측정 (#149 에서 이월)

## 하지 말 것 (스코프 밖)

- **코어 그라데이션 알파 리맵.** 위 TODO. 스크린샷에 주황이 안 보여도 이 PR 에서 노드를 끼우지 말 것 — (a) 는 "코드 0줄 파라미터 튜닝"으로 스코프돼 있고, 층이 3개가 되면 주황 대역을 연기 층이 대신 채울 수 있다
- **링 파라미터 값 변경.** `--force` 재생성으로 `.uasset` 바이너리는 바뀌지만 **값은 `Strength` 3.0 / `Opacity` 0.9 그대로**여야 한다
- **`ExplosionLifeSec` 변경.** 0.8s 공유가 이 설계의 전제다 — 바꾸면 코어·링의 반경 곡선과 감쇠가 전부 바뀌고 #149 의 룩 판정이 무효화된다
- **커스텀데이터 슬롯 추가.** 세 층이 슬롯 1개(`[0]=진행도`)를 공유한다
- **4번째 층.** 3층으로 부족하다고 스크린샷이 말할 때만, 별 이슈로
- **`re.Fx.ExplosionBudget` 기본값 변경.** 측정이 요구하면 그때, 근거와 함께
- **`SyncExplosionISM` 을 공용 헬퍼로 승격.** 세 렌더 프로세서에 비슷한 함수가 있지만 빼지 말 것
- **호출부 3곳 수정.** `SpawnBulletExplosion` 시그니처 불변 — 고쳐야 한다고 느끼면 무언가 잘못 구현한 것이다
- **`REExplosionFx.h/.cpp` 수정.** 진행도 계약이 불변이라 손댈 곳이 없다
- **테스트 프레임워크 도입.** 이 리포에 자동화 테스트 인프라가 없다
- **`Content/FX/NS_REBulletExplosion.uasset` / `convert_explosion_fx.py` 삭제, `Build.cs` 에서 Niagara 제거.** Niagara 는 `RECharacterBase`·`REBossCharacter` 가 아직 쓴다
- **M7 프로파일 문서·README·포트폴리오 수치 갱신.** 별 이슈
- **다른 렌더 프로세서 / 머티리얼 스크립트 리팩터링.** `make_arena_marker.py` · `M_ArenaMarker` 는 건드리지 말 것
