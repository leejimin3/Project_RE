# 폭발 FX 드로우콜 상수화 구현 플랜 (#149)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 폭발 FX 의 드로우콜을 동시 폭발 개수와 무관한 상수(층 수 = 2)로 만든다.

**Architecture:** 폭발 1개 = ISM 인스턴스 2개(구체 코어 + 수평 링). 개별 `UNiagaraComponent` 스폰을 없애고, `REExplosionFx` 가 살아있는 폭발 목록만 들고 있으며, 새 `UREExplosionRenderProcessor` 가 프레임당 한 번 그 목록을 두 ISM 에 배치 반영한다. 진행도(0→1)를 퍼인스턴스 커스텀데이터 `[0]` 으로 넘겨 머티리얼이 확장·감쇠를 그린다 — 탄환 스폰 팝이 쓰는 경로와 같다.

**Tech Stack:** UE 5.8.1 소스 빌드 · MassEntity · `UInstancedStaticMeshComponent` · 파이썬 머티리얼 저작(`MaterialEditingLibrary`)

**Spec:** `docs/superpowers/specs/2026-09-13-explosion-ism-o1-design.md`

## Global Constraints

- **브랜치:** `feature/M8-explosion-ism-o1` (이미 존재, `dev` 에서 분기). 새로 만들지 말 것
- **빌드 게이트는 `Result: Succeeded`.** `Target is up to date` 로 0 액션이면 **아무것도 검증되지 않은 것이다** — 유니티 섀도잉(C4459)은 실제 컴파일에서만 잡힌다
- **에디터가 열려 있으면 빌드가 거부된다**: `Unable to build while Live Coding is active`. `Get-CimInstance Win32_Process -Filter "Name LIKE '%Unreal%'"` 로 확인하고, **커맨드라인에 `-game` 이 없으면 사람이 연 에디터다 — 죽이지 말고 물어봐라**
- **자동화 테스트 인프라가 없다.** 게이트는 (1) 빌드 성공, (2) headless 프로브 로그, (3) 실 RHI 스크린샷, (4) `profile.ps1` CSV 수치다. 테스트 프레임워크를 도입하지 말 것
- **익명 네임스페이스·함수 지역 static 이름을 다른 파일과 겹치지 말 것.** 유니티 빌드에서 C4459 로 깨진다(`REArcRenderProcessor.cpp:134`, `REArcFxProcessor.cpp:16` 에 같은 주의가 적혀 있다). 이 플랜의 이름은 전부 `Explosion*` 접두어다
- **머티리얼 생성 스크립트는 '초안 생성기'다.** 정본은 `.uasset` 이다. 이미 존재하면 `--force` 없이 중단한다 — 기존 스크립트(`make_bullet_material.py`, `make_arena_marker.py`)와 같은 규약
- **Git Bash 에서 UE 실행 파일을 부를 때 `MSYS_NO_PATHCONV=1` 필수.** 없으면 경로가 mangling 돼 조용히 실패한다
- **`-ExecCmds` 는 통짜 문자열 + 쉼표 뒤 공백 없음.** PowerShell 이 공백에서 인자를 쪼개면 CVar 가 **에러 없이 그냥 안 걸린다**
- 커밋 메시지 끝에 붙일 것:
  ```
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
  ```

## 검증된 API (실물 확인됨 — 추론하지 말 것)

| API / 사실 | 확인한 곳 |
|---|---|
| `virtual bool SetCustomData(int32 InstanceIndexStart, int32 InstanceIndexEnd, TConstArrayView<float> CustomDataFloats, bool bMarkRenderStateDirty = false)` — **범위 오버로드가 존재한다** | `Engine/Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h:331` |
| `BatchUpdateInstancesTransforms(int32 StartInstanceIndex, const TArray<FTransform>& NewInstancesTransforms, bool bWorldSpace, bool bMarkRenderStateDirty, bool bTeleport)` | 리포에서 컴파일 확인: `REBulletRenderProcessor.cpp:122` |
| `UMassProcessor::ConfigureQueries` 는 **순수 가상이 아니다**(`UE_API virtual void ConfigureQueries(...)`) → 쿼리가 없는 프로세서는 오버라이드하지 않아도 된다 | `Engine/Source/Runtime/MassEntity/Public/MassProcessor.h:225` |
| **`QueryBasedPruning` 기본값이 `Prune` 이다.** 쿼리가 0개인 프로세서는 런타임에 **프루닝돼 `Execute` 가 아예 안 돈다** → 조용한 dead code. `protected` 멤버이므로 생성자에서 `QueryBasedPruning = EMassQueryBasedPruning::Never;` 로 꺼야 한다 | 선언 `MassProcessor.h:322`(protected: `:306`), 판정 `MassProcessor.cpp:385`, 프루닝 분기 `MassProcessorDependencySolver.cpp:977-990` |
| CSV 카테고리 `REBullet` 은 이미 정의돼 있다(`CSV_DEFINE_CATEGORY(REBullet, true)`). 다른 파일은 `CSV_DECLARE_CATEGORY_EXTERN(REBullet);` 로 선언만 한다 | 정의 `REBulletSimProcessor.cpp:11`, 선언 `REBulletRenderProcessor.cpp:16` |
| CSV 매크로에는 `#include "ProfilingDebugging/CsvProfiler.h"` 가 필요하다 | `REBulletRenderProcessor.cpp:13` |
| `REBulletGeometry::EngineSphereRadius = 50.f` | `REBulletGeometry.h:25` |
| `/Engine/BasicShapes/Plane` 은 100x100 → 반경 50 |  `REArcRenderProcessor.cpp:25` |
| 곡사 착지 폭발은 호출 전에 이미 `Loc.Z += 55` 가 적용된다(바닥 윗면 Z=40 회피) → **링에 추가 Z 오프셋이 필요 없다** | `REArcFxProcessor.cpp:70-73` |
| 머티리얼에 `used_with_instanced_static_meshes = True` 가 없으면 엔진이 **조용히 기본 머티리얼로 대체**한다 | `scripts/make_arena_marker.py:53` (#97 이력) |
| `MaterialEditingLibrary` 변경은 dirty 플래그를 세우지 않는다 → `save_asset(path, False)` 로 저장해야 디스크에 써진다 | `scripts/make_arena_marker.py:139-141` |
| `MaterialExpressionPerInstanceCustomData` 노드 사용법: `set_editor_property("data_index", N)` + `set_editor_property("const_default_value", 0.0)` | `scripts/make_bullet_material.py:105-107` |
| Niagara 모듈은 `Build.cs` 에 **남겨둔다** — `RECharacterBase` / `REBossCharacter` 가 아직 쓴다 | `Project_RE.Build.cs:26`, `grep -l Niagara Source/` |

## 파일 구조

| 파일 | 책임 | 조치 |
|---|---|---|
| `scripts/make_explosion_material.py` | 폭발 머티리얼 2종 초안 생성기 | **신규** |
| `Content/Materials/M_REExplosionCore.uasset` | 코어(구체) 룩 정본 | **신규**(스크립트 산출물, 커밋) |
| `Content/Materials/M_REExplosionRing.uasset` | 링(수평 원환) 룩 정본 | **신규**(스크립트 산출물, 커밋) |
| `Source/Project_RE/Mass/REBulletRenderSubsystem.h/.cpp` | ISM 소유·노출 | 수정 — ISM 2통 추가, `Preload` 호출 제거 |
| `Source/Project_RE/Mass/REExplosionFx.h/.cpp` | 폭발 요청 진입점 + 살아있는 목록 보관 | 수정 — Niagara 제거, 배열화 |
| `Source/Project_RE/Mass/REExplosionRenderProcessor.h/.cpp` | 목록 → 두 ISM 배치 반영 | **신규** |
| `docs/superpowers/specs/2026-09-13-explosion-ism-o1-design.md` | 설계 + 측정 결과 | 수정 — §9 에 실측 기록(TASK 4) |

---

### Task 1: 폭발 머티리얼 2종

**Files:**
- Create: `scripts/make_explosion_material.py`
- Create (스크립트 산출물): `Content/Materials/M_REExplosionCore.uasset`, `Content/Materials/M_REExplosionRing.uasset`

**Interfaces:**
- Consumes: 없음 (첫 태스크)
- Produces: 머티리얼 경로 두 개 — `/Game/Materials/M_REExplosionCore`, `/Game/Materials/M_REExplosionRing`. Task 2 가 `LoadObject` 로 이 경로를 문자열로 참조한다. 파라미터 이름 `Color` / `ColorB` / `Strength` / `Opacity` / `Radius` / `Edge` / `Inner`. 둘 다 퍼인스턴스 커스텀데이터 `[0]` 을 진행도로 읽는다

- [ ] **Step 1: 생성 스크립트 작성**

`scripts/make_explosion_material.py`:

```python
"""
M_REExplosionCore / M_REExplosionRing 부트스트랩 생성 (#149).

폭발을 개별 Niagara 컴포넌트에서 ISM 인스턴스로 옮긴다(설계: specs/2026-09-13-explosion-ism-o1-design.md).
폭발 1개 = 코어 구체 1 + 수평 링 1 이고, 둘 다 퍼인스턴스 커스텀데이터[0](진행도 0→1)을
읽어 스스로 확장·감쇠한다. 진행도는 REExplosionRenderProcessor 가 매 프레임 써준다.

왜 팩 텍스처(SubUV 플립북)를 쓰지 않는가:
  팩(Realistic_Starter_VFX_Pack_Vol2)은 gitignore 대상인데 기존 NS_REBulletExplosion 이
  팩 머티리얼 6개를 참조한다 — 팩 없는 클론에서 폭발이 깨진다. 프로시저럴로 그리면
  그 의존이 끊긴다.

Translucent 를 쓰는 이유와 additive 를 쓰지 않는 이유:
  마커(M_ArenaMarker)·빔(M_REBeam)과 같은 Unlit + Translucent 조합이다. additive 는
  아레나 바닥이 밝은 라벤더라 흰색으로 포화된다 — make_beam_material.py 에 되돌린 기록이 있다.

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 생성 후 에디터에서 수치를 손보면 낡는다.
정본은 Content/Materials/M_REExplosion*.uasset 이다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
  인자: --force --strength=3.0 --opacity=0.9 --radius=0.45 --edge=0.10 --inner=0.30
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

strength = scalar("Strength", _arg("strength", 3.0), -900, 120)
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
strength = scalar("Strength", _arg("strength", 3.0), -760, 620)
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
```

- [ ] **Step 2: 스크립트 실행**

에디터가 열려 있으면 먼저 닫아라(에셋 락).

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -ExecutePythonScript="E:/UnrealProjects/Project_RE/scripts/make_explosion_material.py" \
  -unattended -nosplash -nop4
```

기대 출력 (**두 줄 다** 나와야 한다):
```
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionCore (blend=BlendMode.BLEND_TRANSLUCENT)
RE_EXPL: 생성 완료 /Game/Materials/M_REExplosionRing (blend=BlendMode.BLEND_TRANSLUCENT)
```

`RE_EXPL: 연결 실패` 가 보이면 노드 입력 이름이 틀린 것이다 — 스크립트를 고쳐 다시 돌려라(`--force` 추가).

- [ ] **Step 3: 에셋이 디스크에 생겼는지 확인**

```bash
cd E:/UnrealProjects/Project_RE && ls -la Content/Materials/M_REExplosion*.uasset
```

기대: 두 파일 존재, 크기 0 아님. **없으면 `save_asset` 의 `only_if_is_dirty` 문제다** — 로그에 "생성 완료"가 찍혔어도 디스크에 없을 수 있다.

- [ ] **Step 4: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add scripts/make_explosion_material.py Content/Materials/M_REExplosionCore.uasset Content/Materials/M_REExplosionRing.uasset && git commit -m "$(cat <<'EOF'
feat(fx): 폭발 ISM 머티리얼 2종 — 코어 구체 + 수평 링 (#149)

퍼인스턴스 커스텀데이터[0](진행도 0→1)을 읽어 스스로 확장·감쇠한다. 진행도는
후속 태스크의 REExplosionRenderProcessor 가 매 프레임 쓴다.

팩 SubUV 시트를 쓰지 않는다. 팩은 gitignore 대상인데 기존 NS_REBulletExplosion 이
팩 머티리얼 6개를 참조해 팩 없는 클론에서 폭발이 깨졌다 — 프로시저럴로 그리면
그 의존이 끊긴다.

Unlit + Translucent + two_sided 는 M_ArenaMarker·M_REBeam 과 같은 조합이다.
additive 는 아레나 바닥이 밝은 라벤더라 흰색으로 포화된 기록이 있어 쓰지 않는다.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

---

### Task 2: ISM 2통 추가 + Preload 제거

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletRenderSubsystem.h`
- Modify: `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp`

**Interfaces:**
- Consumes: Task 1 의 머티리얼 경로 문자열 `/Game/Materials/M_REExplosionCore`, `/Game/Materials/M_REExplosionRing`
- Produces: `UREBulletRenderSubsystem::GetExplosionCoreISM()` / `GetExplosionRingISM()` — 둘 다 `UInstancedStaticMeshComponent*` 반환, 데디서버·비게임월드에서는 `nullptr`. 커스텀데이터 슬롯 수는 1. Task 4 의 프로세서가 이 둘을 쓴다

- [ ] **Step 1: 헤더에 ISM 2통 + 접근자 추가**

`REBulletRenderSubsystem.h` — `GetMarkerISM()` 아래에 접근자 두 개를 더하고, `MarkerISM` 아래에 멤버 두 개를 더한다:

```cpp
	UInstancedStaticMeshComponent* GetISM() const { return ISM; }
	UInstancedStaticMeshComponent* GetArcISM() const { return ArcISM; }
	UInstancedStaticMeshComponent* GetMarkerISM() const { return MarkerISM; }
	UInstancedStaticMeshComponent* GetExplosionCoreISM() const { return ExplosionCoreISM; }
	UInstancedStaticMeshComponent* GetExplosionRingISM() const { return ExplosionRingISM; }

private:
	UPROPERTY()
	TObjectPtr<AActor> Holder = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ISM = nullptr;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ArcISM = nullptr;     // 곡사탄(주황 구체, Z 궤적)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MarkerISM = nullptr;  // 착지 예고(빨강 평면 원)

	// 폭발 (#149) — 폭발 1개 = 아래 두 통에 인스턴스 1개씩. 컴포넌트를 만들지 않으므로
	// 드로우콜이 동시 폭발 개수와 무관하다(층 수가 상한).
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionCoreISM = nullptr;  // 불덩이(구체)

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ExplosionRingISM = nullptr;  // 수평 충격파(평면 원환)
```

- [ ] **Step 2: `Preload` 호출 제거**

`REBulletRenderSubsystem.cpp` 에서 아래 블록(주석 포함)을 **삭제**한다:

```cpp
	// 폭발 에셋 선로드 (#107). 첫 폭발에서 동기 로드가 걸리면 ~290ms 멈추고,
	// 그 히치가 대쉬 구간에 겹치면 이동거리까지 틀어졌다(#106). 시작 시 한 번에 끝낸다.
	// 여기가 적기다 — 위 가드가 데디서버·비게임월드를 이미 걸러냈고, 레벨 로드 중이라
	// 로드 비용이 눈에 띄지 않는다.
	REExplosionFx::Preload(&InWorld);
```

그리고 이제 안 쓰는 include 를 지운다:

```cpp
#include "REExplosionFx.h"
```

Niagara 시스템을 더 이상 로드하지 않으므로 선로드할 대상이 없다 — 폭발 머티리얼·메시는 아래 Step 3 에서 `LoadObject` 로 같은 시점에 로드된다.

- [ ] **Step 3: ISM 2통 생성 코드 추가**

`REBulletRenderSubsystem.cpp` 의 `MarkerISM` 블록 **뒤**, 함수 끝 로그 앞에 추가한다:

```cpp
	// ===== 폭발 (#149) =====
	// 폭발 1개 = 코어 구체 1 + 수평 링 1. 컴포넌트를 만들지 않으므로 드로우콜이
	// 동시 폭발 개수와 무관하다 — 이전 구조(개별 Niagara 컴포넌트)는 컴포넌트당
	// 4.9 드로우콜로 개수에 선형이었다(#147).
	// 커스텀데이터는 슬롯 1개: [0]=진행도(0→1). 두 통이 같은 값을 받고 각자 해석한다.
	ExplosionCoreISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	ExplosionCoreISM->SetupAttachment(ISM);
	ExplosionCoreISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExplosionCoreISM->bAffectDynamicIndirectLighting = false;
	ExplosionCoreISM->bAffectDistanceFieldLighting = false;
	ExplosionCoreISM->SetCastShadow(false);   // 탄환과 같은 이유 (#95)
	ExplosionCoreISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ExplosionCoreISM->SetStaticMesh(Mesh);
	}
	ExplosionCoreISM->SetNumCustomDataFloats(1);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REExplosionCore.M_REExplosionCore")))
	{
		ExplosionCoreISM->SetMaterial(0, Base);
	}
	else
	{
		// 조용한 폴백 금지 — 기본 머티리얼로 렌더되면 회색 구체가 떠서 원인을 찾기 어렵다.
		UE_LOG(LogREBullet, Error, TEXT("[RE] M_REExplosionCore 로드 실패 — 폭발 코어가 기본 머티리얼로 렌더된다 (#149)"));
	}

	ExplosionRingISM = NewObject<UInstancedStaticMeshComponent>(Holder);
	ExplosionRingISM->SetupAttachment(ISM);
	ExplosionRingISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExplosionRingISM->bAffectDynamicIndirectLighting = false;
	ExplosionRingISM->bAffectDistanceFieldLighting = false;
	ExplosionRingISM->SetCastShadow(false);
	ExplosionRingISM->RegisterComponent();
	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		ExplosionRingISM->SetStaticMesh(Mesh);
	}
	ExplosionRingISM->SetNumCustomDataFloats(1);
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REExplosionRing.M_REExplosionRing")))
	{
		ExplosionRingISM->SetMaterial(0, Base);
	}
	else
	{
		UE_LOG(LogREBullet, Error, TEXT("[RE] M_REExplosionRing 로드 실패 — 폭발 링이 기본 머티리얼로 렌더된다 (#149)"));
	}
```

- [ ] **Step 4: 빌드**

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REEditor Win64 Development \
  -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -DisableAdaptiveUnity -WaitMutex
```

기대: `Result: Succeeded`. `Target is up to date` 면 아무것도 검증되지 않았다 — 파일이 실제로 저장됐는지 확인해라.

`REExplosionFx::Preload` 를 지웠는데 `REExplosionFx.h` 에는 아직 선언이 남아 있다 — 이 시점에서는 정상이다(Task 3 에서 선언도 지운다). 정의되지 않은 참조가 없으므로 링크는 통과한다.

- [ ] **Step 5: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Mass/REBulletRenderSubsystem.h Source/Project_RE/Mass/REBulletRenderSubsystem.cpp && git commit -m "$(cat <<'EOF'
feat(fx): 폭발 ISM 2통(코어 구체 + 수평 링) 추가 (#149)

기존 3통(직선탄/곡사탄/마커)과 같은 설정 — NoCollision, Lumen 씬 제외, 그림자 off.
커스텀데이터 슬롯 1개: [0]=진행도. 머티리얼 로드 실패는 조용히 넘기지 않고 Error 로
남긴다 — 기본 머티리얼 폴백은 회색 구체로 렌더돼 원인을 찾기 어렵다.

Niagara 시스템을 더 이상 로드하지 않으므로 REExplosionFx::Preload 호출을 제거했다.
폭발 머티리얼·메시는 ISM 생성과 같은 시점에 LoadObject 로 들어온다.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

---

### Task 3: REExplosionFx 배열화 (Niagara 제거)

**Files:**
- Modify: `Source/Project_RE/Mass/REExplosionFx.h` (전문 교체)
- Modify: `Source/Project_RE/Mass/REExplosionFx.cpp` (전문 교체)

**Interfaces:**
- Consumes: 없음
- Produces:
  - `struct REExplosionFx::FLiveExplosion { FVector Loc; float SpawnTime; float Progress; }`
  - `void REExplosionFx::SpawnBulletExplosion(const UWorld* World, const FVector& Location)` — **시그니처 불변**(호출부 3곳 무수정)
  - `const TArray<FLiveExplosion>& REExplosionFx::PruneAndGetLive(float NowSeconds)` — Task 4 의 프로세서가 프레임당 **정확히 한 번** 부른다
  - `REExplosionFx::Preload` 는 **삭제된다**

- [ ] **Step 1: `REExplosionFx.h` 전문 교체**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 *  탄환 소멸 폭발 — 단일 진입점 (#98) + 살아있는 폭발 목록 (#149).
 *
 *  폭발 1개 = ISM 인스턴스 2개(구체 코어 + 수평 링)다. 컴포넌트를 만들지 않으므로
 *  드로우콜이 동시 폭발 개수와 무관하다 — 층 수가 상한이다. 이전 구조(개별
 *  UNiagaraComponent 스폰)는 컴포넌트당 4.9 드로우콜로 개수에 선형이었다(#147).
 *
 *  여기는 상태만 들고 있고 그리지 않는다. 그리는 것은 REExplosionRenderProcessor 다.
 *
 *  데디서버에서는 아무것도 하지 않는다 — 렌더가 없다.
 *  게임 스레드에서만 부를 것. 락이 없다(호출부가 전부 GT 고정이다).
 */
namespace REExplosionFx
{
	/** 살아있는 폭발 하나. Progress 는 PruneAndGetLive 가 갱신한다. */
	struct FLiveExplosion
	{
		FVector Loc = FVector::ZeroVector;
		float   SpawnTime = 0.f;   // World->GetTimeSeconds() — 게임 시간이다
		float   Progress = 0.f;    // 0 → 1 (나이 / 수명)
	};

	/**
	 *  폭발 요청. 예산(re.Fx.ExplosionBudget)이 차 있으면 조용히 버린다 —
	 *  버린 수는 ExplosionProbe 로그가 센다.
	 */
	void SpawnBulletExplosion(const UWorld* World, const FVector& Location);

	/**
	 *  만료분을 제거하고 남은 것들의 Progress 를 갱신해 돌려준다.
	 *
	 *  렌더 프로세서가 프레임당 **한 번만** 부른다. 두 번 부르면 ExplosionProbe 의
	 *  창 길이가 왜곡돼 비율이 거짓으로 읽힌다.
	 */
	const TArray<FLiveExplosion>& PruneAndGetLive(float NowSeconds);
}
```

- [ ] **Step 2: `REExplosionFx.cpp` 전문 교체**

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#include "REExplosionFx.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Project_RE.h"                              // LogRE / LogREBullet / LogRENet

namespace
{
	/**
	 *  폭발 on/off (1=켬). 프로파일 하네스가 0으로 고정한다 (#98).
	 *
	 *  하네스는 re.Cheat.PlayerInvincible 로 플레이어를 살려두는데(#46, 안 그러면 Mass 탄이
	 *  0발로 측정된다), 그러면 무적 플레이어가 탄막 한가운데 서서 **초당 109회** 맞는다.
	 *  실제 게임플레이는 10발이면 사망하므로 결코 나오지 않는 비율이다.
	 */
	static TAutoConsoleVariable<int32> CVarExplosions(
		TEXT("re.Fx.Explosions"),
		1,
		TEXT("탄환 소멸 폭발 표시 (0=끔). 프로파일 측정 시 0으로 고정한다."),
		ECVF_Cheat);

	/**
	 *  동시 폭발 상한 (0 이하 = 무제한). 기본값이 #149 에서 12 → 256 으로 올라갔다.
	 *
	 *  **역할이 바뀌었다.** #147 때는 이 값이 드로우콜 상한이었다 — 폭발 1개가
	 *  컴포넌트 1개였고 드로우콜이 개수에 선형이라, 12 를 넘기면 드로우콜이 튀었다.
	 *  지금은 폭발이 ISM 인스턴스라 드로우콜이 개수와 무관하므로 그 근거가 사라졌다.
	 *
	 *  남겨두는 이유는 **오버드로우**다. 반투명 구체·링이 화면에서 겹치면 같은 픽셀을
	 *  여러 번 칠하고, 그 비용은 여전히 개수에 선형이다. 256 은 그 축의 안전장치다.
	 *  0 으로 두면 상한이 사라지므로 재빌드 없이 A/B 가 된다.
	 */
	static TAutoConsoleVariable<int32> CVarExplosionBudget(
		TEXT("re.Fx.ExplosionBudget"),
		256,
		TEXT("동시 폭발 상한 (0 이하=무제한). 드로우콜이 아니라 오버드로우 안전망이다 (#149)."),
		ECVF_Cheat);

	/**
	 *  폭발 수명(초). 진행도(0→1)의 분모이고 목록에서 언제 빠지는지가 이 값이다.
	 *  이전에는 Niagara 파티클 수명(convert_explosion_fx.py --maxlife)과 맞춰야 했지만,
	 *  이제 폭발 전체가 이 값으로만 그려지므로 외부와 맞출 대상이 없다 — 순수 룩 값이다.
	 */
	constexpr float ExplosionLifeSec = 0.8f;

	/** 살아있는 폭발. 최대 길이가 예산이라 선형 순회로 충분하다. */
	TArray<REExplosionFx::FLiveExplosion> GLiveExplosions;

	/** ExplosionProbe 누적 — 이름을 다른 파일과 겹치지 않게 둔다(유니티 빌드 C4459). */
	int32 GExplosionSpawnedSinceLog = 0;
	int32 GExplosionDroppedSinceLog = 0;
	float GExplosionLastProbeLog = 0.f;
}

void REExplosionFx::SpawnBulletExplosion(const UWorld* World, const FVector& Location)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;   // 데디서버는 렌더가 없다
	}
	if (CVarExplosions.GetValueOnGameThread() == 0)
	{
		return;
	}

	// 오버드로우 안전망 (#149 §8). 드로우콜은 이 값과 무관하다.
	const int32 Budget = CVarExplosionBudget.GetValueOnGameThread();
	if (Budget > 0 && GLiveExplosions.Num() >= Budget)
	{
		++GExplosionDroppedSinceLog;
		return;
	}

	FLiveExplosion& E = GLiveExplosions.AddDefaulted_GetRef();
	E.Loc = Location;
	E.SpawnTime = World->GetTimeSeconds();
	E.Progress = 0.f;
	++GExplosionSpawnedSinceLog;
}

const TArray<REExplosionFx::FLiveExplosion>& REExplosionFx::PruneAndGetLive(float NowSeconds)
{
	// 뒤에서부터 돌며 RemoveAtSwap 한다. 마지막 원소가 i 로 들어오는데 그 원소의 원래
	// 인덱스는 항상 i 보다 크므로 이미 검사된 것이다 — 건너뛰는 항목이 없다.
	for (int32 i = GLiveExplosions.Num() - 1; i >= 0; --i)
	{
		const float Age = NowSeconds - GLiveExplosions[i].SpawnTime;

		// Age < 0 은 월드가 바뀌어 게임 시간이 되감긴 경우다. 그대로 두면 새 월드에서
		// 0.8초가 지날 때까지 유령 폭발이 남고 예산도 그만큼 먹는다.
		if (Age >= ExplosionLifeSec || Age < 0.f)
		{
			GLiveExplosions.RemoveAtSwap(i);
			continue;
		}
		GLiveExplosions[i].Progress = Age / ExplosionLifeSec;
	}

	// 프로브 — 1초에 1줄. 창 길이(Window)를 같이 찍는다: 프레임 간격이 일정하지 않으면
	// 실제 창이 1초보다 길다. 버린 수를 안 찍으면 예산이 걸린 것과 애초에 요청이 적은
	// 것을 구별할 수 없어 "폭발이 왜 안 보이나"를 되짚을 근거가 사라진다.
	if (GExplosionLastProbeLog == 0.f)
	{
		GExplosionLastProbeLog = NowSeconds;
	}
	const float Window = NowSeconds - GExplosionLastProbeLog;
	if (Window >= 1.f)
	{
		UE_LOG(LogREBullet, Log, TEXT("[RE] ExplosionProbe: 스폰=%d 버림=%d / %.2fs (%.1f/s, 동시=%d)"),
			GExplosionSpawnedSinceLog, GExplosionDroppedSinceLog, Window,
			GExplosionSpawnedSinceLog / Window, GLiveExplosions.Num());
		GExplosionSpawnedSinceLog = 0;
		GExplosionDroppedSinceLog = 0;
		GExplosionLastProbeLog = NowSeconds;
	}
	else if (Window < 0.f)
	{
		GExplosionLastProbeLog = NowSeconds;   // 월드 전환 — 창을 다시 잡는다
	}

	return GLiveExplosions;
}
```

- [ ] **Step 3: 빌드**

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REEditor Win64 Development \
  -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -DisableAdaptiveUnity -WaitMutex
```

기대: `Result: Succeeded`.

**이 시점에서 폭발은 화면에 안 나온다 — 정상이다.** 목록에만 쌓이고 그리는 쪽(Task 4)이 아직 없다. 버그로 오해하지 말 것.

- [ ] **Step 4: 팩 의존이 끊겼는지 확인** (설계 §9 게이트)

```bash
cd E:/UnrealProjects/Project_RE && grep -rn "Realistic_Starter\|NS_REBulletExplosion\|Niagara" Source/Project_RE/Mass/REExplosionFx.cpp Source/Project_RE/Mass/REExplosionFx.h
```

기대: **아무것도 안 나온다.** 폭발 경로가 팩 에셋도, Niagara 도 더 이상 참조하지 않는다는 뜻이다(`NS_REBulletExplosion` 이 팩 머티리얼 6개를 참조했고 팩은 gitignore 대상이라, 이게 곧 팩 없는 클론에서 폭발이 나오는 근거다).

`REArcFxProcessor.cpp` 에 남은 "Niagara" 는 주석이므로 무시한다 — 위 grep 대상에 넣지 않았다.

- [ ] **Step 5: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Mass/REExplosionFx.h Source/Project_RE/Mass/REExplosionFx.cpp && git commit -m "$(cat <<'EOF'
refactor(fx): 폭발을 Niagara 스폰에서 살아있는 목록으로 교체 (#149)

SpawnBulletExplosion 시그니처는 그대로 두고 내부만 바꿨다 — 호출부 3곳
(RECharacterBase / REArcFxProcessor / REBulletHitProcessor) 무수정.

시간을 FPlatformTime::Seconds(월클럭)에서 World->GetTimeSeconds(게임 시간)으로
바꿨다. 월클럭은 일시정지·슬로모에서 실제 수명과 어긋나는 축이었다. 시간이
되감기는 월드 전환도 Age < 0 으로 걸러 유령 폭발이 남지 않는다.

re.Fx.ExplosionBudget 기본값 12 → 256. 역할이 드로우콜 상한에서 오버드로우
안전망으로 바뀌었다 — 드로우콜은 이제 개수와 무관하지만 반투명 오버드로우는
여전히 선형이다.

이 커밋만으로는 폭발이 화면에 안 나온다. 그리는 프로세서가 다음 커밋이다.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

---

### Task 4: REExplosionRenderProcessor (폭발이 화면에 뜬다)

**Files:**
- Create: `Source/Project_RE/Mass/REExplosionRenderProcessor.h`
- Create: `Source/Project_RE/Mass/REExplosionRenderProcessor.cpp`

**Interfaces:**
- Consumes: `REExplosionFx::PruneAndGetLive(float)`, `REExplosionFx::FLiveExplosion` (Task 3) · `UREBulletRenderSubsystem::GetExplosionCoreISM()` / `GetExplosionRingISM()` (Task 2) · `REBulletGeometry::EngineSphereRadius` (기존)
- Produces: 없음 (최종 소비자)

- [ ] **Step 1: 헤더 작성**

`Source/Project_RE/Mass/REExplosionRenderProcessor.h`:

```cpp
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "REExplosionRenderProcessor.generated.h"

/**
 *  폭발 ISM 동기 (#149).
 *
 *  REExplosionFx 의 살아있는 목록을 두 ISM(코어 구체 / 수평 링)에 프레임당 한 번
 *  배치 반영한다. 폭발 1개 = 인스턴스 2개이므로 드로우콜이 동시 폭발 개수와 무관하다.
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

- [ ] **Step 2: 구현 작성**

`Source/Project_RE/Mass/REExplosionRenderProcessor.cpp`:

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

	/** /Engine/BasicShapes/Plane 은 100x100 이라 반경 50. 스케일 = 반경/50.
	 *  (마커 쪽 CylinderBaseRadius 와 같은 값이지만 이름을 다르게 둔다 — 익명
	 *  네임스페이스 동명 상수가 유니티 빌드에서 충돌한 전례가 있다.) */
	constexpr float ExplosionPlaneBaseRadius = 50.f;

	/** 링 Z 스케일. **1.0 미만으로 두지 말 것** — XY 를 키운 상태에서 Z 를 낮추면
	 *  인스턴스가 화면에서 통째로 사라진다(마커에서 실RHI 스크린샷 이진탐색으로 확인:
	 *  0.02/0.15/0.4 전부 무렌더, 등방 2.0 은 정상. ISM 극단 비등방 스케일의 컬링/
	 *  바운즈 이슈로 추정, 엔진 레벨·원인 미상 — REArcRenderProcessor.cpp 참조). */
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
	UInstancedStaticMeshComponent* CoreISM = RS ? RS->GetExplosionCoreISM() : nullptr;
	UInstancedStaticMeshComponent* RingISM = RS ? RS->GetExplosionRingISM() : nullptr;
	if (!CoreISM || !RingISM)
	{
		return;
	}

	const int32 M = Live.Num();
	TArray<FTransform> CoreXf;
	TArray<FTransform> RingXf;
	TArray<float> Cd;
	CoreXf.Reserve(M);
	RingXf.Reserve(M);
	Cd.Reserve(M);

	for (const REExplosionFx::FLiveExplosion& E : Live)
	{
		const float CoreR = FMath::Lerp(ExplosionCoreRadiusStart, ExplosionCoreRadiusEnd, E.Progress);
		const float RingR = FMath::Lerp(ExplosionRingRadiusStart, ExplosionRingRadiusEnd, E.Progress);

		// 구체는 등방 스케일이고 회전이 의미 없다. 카메라가 월드 고정(pitch -50 절대)이라
		// 빌보드 갱신도 필요 없다.
		const float CoreS = CoreR / REBulletGeometry::EngineSphereRadius;
		CoreXf.Add(FTransform(FRotator::ZeroRotator, E.Loc, FVector(CoreS)));

		// Plane 은 XY 평면(법선 +Z)이라 회전 없이 그대로 수평 원판이다.
		// Z 는 1.0 고정 — 위 ExplosionRingZScale 주석의 함정.
		const float RingS = RingR / ExplosionPlaneBaseRadius;
		RingXf.Add(FTransform(FRotator::ZeroRotator, E.Loc,
			FVector(RingS, RingS, ExplosionRingZScale)));

		Cd.Add(E.Progress);
	}

	SyncExplosionISM(CoreISM, CoreXf);
	SyncExplosionISM(RingISM, RingXf);

	// 인스턴스 수를 맞춘 뒤라야 SetCustomData 의 인덱스 범위가 유효하다.
	if (M > 0)
	{
		CoreISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
		RingISM->SetCustomData(0, M - 1, Cd, /*bMarkRenderStateDirty=*/true);
	}
}
```

- [ ] **Step 3: 빌드**

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REEditor Win64 Development \
  -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -DisableAdaptiveUnity -WaitMutex
```

기대: `Result: Succeeded`.

- [ ] **Step 4: headless 프로브 — 프로세서가 실제로 도는지 확인**

가장 중요한 게이트다. 프루닝으로 `Execute` 가 0회 도는 실패는 **에러 없이 조용하다.**

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Binaries/Win64/UnrealEditor.exe" \
  "E:/UnrealProjects/Project_RE/Project_RE.uproject" Main \
  -game -nullrhi -unattended -nosplash -NoSound -log=RE_expl_probe.log
```

그다음:

```bash
cd E:/UnrealProjects/Project_RE && grep -E "ExplosionProbe|M_REExplosion|has no registered queries" Saved/Logs/RE_expl_probe.log | head -20
```

기대:
- `[RE] ExplosionProbe: 스폰=N 버림=0 / ...초 (.../s, 동시=M)` 이 **한 줄 이상** — 프로세서가 돌고 있다는 증거다. 한 줄도 없으면 프루닝됐거나 `Execute` 가 안 불린 것이다(`QueryBasedPruning` 설정을 확인)
- `M_REExplosionCore 로드 실패` / `M_REExplosionRing 로드 실패` 가 **없어야** 한다
- `has no registered queries` 경고가 보이면 `QueryBasedPruning = Never` 가 안 걸린 것이다 — 프루닝 위험 신호이므로 반드시 고쳐라

- [ ] **Step 5: 실 RHI 스크린샷 — 화면에 폭발이 보이는지 확인**

`-nullrhi` 로는 판정할 수 없다(ISM Bounds=0 오진 전례). 실 RHI 창으로 찍는다. PowerShell 에서:

```powershell
$UE='E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$a='"E:\UnrealProjects\Project_RE\Project_RE.uproject" Main -game -windowed -ResX=1600 -ResY=900 -nosplash -NoSound ' +
   '-log=RE_expl_shot.log -ExecCmds="re.Profiling.KeepFiring 1,re.Cheat.PlayerInvincible 1,re.Debug.FakeHitTargets 16,re.Debug.ScreenshotFrame 900"'
$p = Start-Process $UE -ArgumentList $a -PassThru
if (-not $p.WaitForExit(180000)) { $p.Kill() }
```

산출물: `Saved/Screenshots/WindowsEditor/`. **파일이 새로 생겼는지 타임스탬프로 확인해라** — `ls -t` 만 믿으면 직전 런의 이미지를 집어 "안 변했다"고 오진한다.

눈으로 볼 것:
1. 플레이어 주변에 폭발(주황 구체 + 퍼지는 링)이 보이는가
2. 링이 바닥에 묻혀 사라지지 않았는가 (착지 폭발은 이미 Z+55 가 적용돼 있다)
3. 링이 **아예 안 보이지** 않는가 — 안 보이면 Z 스케일 함정이다(`ExplosionRingZScale` 확인)
4. 반투명 정렬 아티팩트(겹친 폭발의 앞뒤가 뒤집혀 깜빡임)가 눈에 띄는가

4번이 문제면 폴백은 `make_explosion_material.py` 의 코어를 `BLEND_MASKED` 로 바꾸는 것이다(정렬 문제 소멸, 대가로 소멸이 뚝 끊긴다). 판단이 서지 않으면 스크린샷을 사람에게 보여주고 물어라.

- [ ] **Step 6: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add Source/Project_RE/Mass/REExplosionRenderProcessor.h Source/Project_RE/Mass/REExplosionRenderProcessor.cpp && git commit -m "$(cat <<'EOF'
feat(fx): 폭발 ISM 렌더 프로세서 — 드로우콜을 개수와 무관하게 (#149)

살아있는 폭발 목록을 두 ISM(코어 구체 / 수평 링)에 프레임당 한 번 배치 반영한다.
폭발 1개 = 인스턴스 2개라 드로우콜이 층 수로 고정된다.

쿼리가 없는 프로세서라 QueryBasedPruning 을 Never 로 꺼야 한다. 기본값 Prune 이면
런타임에 통째로 프루닝돼 Execute 가 한 번도 안 돈다 — 에러 없이 조용한 dead code다.

탄환 렌더 프로세서에 얹지 않았다. 그 함수는 CSV_SCOPED_TIMING_STAT(REBullet,
BulletRender) 안쪽이라 폭발 비용이 BulletRender 수치를 오염시킨다. 별 스코프
(RE_ExplosionRender / ExplosionRender)를 가지므로 폭발 원가를 따로 잰다 — 선행
설계 §7.1 이 "폭발 원가를 CSV 로 재려면 별도 하네스가 필요하다"고 남긴 구멍이
이걸로 메워진다.

만료 제거를 ISM 확인보다 먼저 한다. 뒤에 두면 ISM 이 없는 월드에서 목록이 안 비고
예산이 가득 찬 채 잠겨 폭발이 조용히 전량 버려진다.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

---

### Task 5: 상수성 측정 + 설계 문서에 결과 기록

**Files:**
- Modify: `docs/superpowers/specs/2026-09-13-explosion-ism-o1-design.md` (§9 에 실측 표 추가)

**Interfaces:**
- Consumes: Task 4 까지의 동작하는 빌드
- Produces: 측정 수치(PR 본문이 그대로 쓴다)

- [ ] **Step 1: 기준선 — 폭발 OFF**

```powershell
cd E:\UnrealProjects\Project_RE
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Debug.FakeHitTargets 16" -Label e149_off
```

하네스 기본이 `re.Fx.Explosions 0` 이므로 이 런은 폭발이 꺼진 기준선이다. #147 기준 예상: Draws mean 228 / p99 259.

- [ ] **Step 2: 예산 OFF — 상수성 핵심 게이트**

```powershell
cd E:\UnrealProjects\Project_RE
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.FakeHitTargets 16,re.Fx.ExplosionBudget 0" -Label e149_nobudget
```

**판정:** 드로우콜이 기준선(228) + 상수여야 한다. 같은 조건에서 변경 전은 **680 / 780** 이었다.

`run.log` 에서 동시 개수를 읽어 무상관을 확인한다:

```bash
cd E:/UnrealProjects/Project_RE && grep "ExplosionProbe" $(ls -td Saved/Profiling/RE_Mass_5000_e149_nobudget_*/ | head -1)run.log | tail -10
```

`동시=` 가 수십으로 오르는데 Draws 가 그만큼 안 오르면 상수화 성공이다. 동시 개수와 드로우콜이 같이 오르면 **실패** — 층이 인스턴스로 배칭되지 않고 있다는 뜻이므로 ISM 설정(머티리얼 `used_with_instanced_static_meshes`)을 다시 봐라.

- [ ] **Step 3: 기본 예산(256) 런**

```powershell
cd E:\UnrealProjects\Project_RE
scripts\profile.ps1 -Bullets 5000 -Frames 720 -ExtraExec "re.Fx.Explosions 1,re.Debug.FakeHitTargets 16" -Label e149_budget256
```

출하 설정에서의 프레임·RT 를 본다. 오버드로우가 문제면 여기서 RT ms 가 뛴다 — 그 값이 예산 256 의 근거가 맞는지 판단 재료다.

- [ ] **Step 4: CSV 수치 추출**

전용 스크립트가 있다 — 손으로 CSV 를 파싱하지 말 것. 세 런을 한 번에 넘기면 마크다운 표로 나온다:

```powershell
cd E:\UnrealProjects\Project_RE
scripts\profile-stats.ps1 -RunDir (Get-ChildItem Saved\Profiling\RE_Mass_5000_e149_* -Directory).FullName
```

표에 나오는 컬럼 중 이 작업에서 볼 것:
- **`Draws`** (`RHI/DrawCalls`) — 상수성 판정의 핵심
- **`Translu`** (`Exclusive/RenderThread/RenderTranslucency`) — **오버드로우 축이다.** 반투명 구체·링이 겹쳐 같은 픽셀을 여러 번 칠하는 비용이 여기 잡힌다. 동시 개수가 늘 때 `Draws` 는 고정인데 `Translu` 만 오르면 설계 §8 의 예측이 맞은 것이다
- `Frame` / `RT` / `Instances`

- [ ] **Step 5: 설계 문서 §9 에 결과 기록**

`docs/superpowers/specs/2026-09-13-explosion-ism-o1-design.md` §9 표 **아래**에 `### 9.1 측정 결과 (2026-XX-XX)` 절을 만들어 위 표와 판정을 적는다. 반드시 포함할 것:
- 드로우콜이 동시 개수와 무상관임을 보이는 수치(동시 N 이 커져도 Draws 가 고정)
- 실제 층당 드로우콜 실측값(예상 2, 반투명 패스 구성에 따라 다를 수 있음 — **예상과 다르면 실측값을 적고 예상이 틀렸다고 쓴다**)
- 오버드로우 관측: 동시 개수가 늘 때 RT ms 가 어떻게 움직였는지
- 스크린샷 판정(정렬 아티팩트 유무, 폴백을 썼는지)

- [ ] **Step 6: 커밋**

```bash
cd E:/UnrealProjects/Project_RE && git add docs/superpowers/specs/2026-09-13-explosion-ism-o1-design.md && git commit -m "$(cat <<'EOF'
docs: 폭발 드로우콜 상수화 실측 결과 (#149)

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
EOF
)"
```

---

### Task 6: 풀 유니티 빌드 게이트 + PR

- [ ] **Step 1: Server 타깃 풀 유니티 빌드**

유니티 섀도잉(C4459)은 이 구성에서만 잡힌다. PR 빌드 게이트를 통과해도 여기서 깨진 전례가 있다.

```bash
cd E:/UnrealProjects/Project_RE && MSYS_NO_PATHCONV=1 \
  "E:/UnrealEngine-5.8/UnrealEngine-5.8/Engine/Build/BatchFiles/Build.bat" \
  Project_REServer Win64 Development \
  -Project="E:/UnrealProjects/Project_RE/Project_RE.uproject" \
  -DisableAdaptiveUnity -WaitMutex
```

기대: `Result: Succeeded`. 데디서버 타깃이므로 폭발 코드가 실행되지는 않지만 **컴파일은 된다** — 그게 이 게이트의 목적이다.

- [ ] **Step 2: 푸시**

```bash
cd E:/UnrealProjects/Project_RE && git push origin feature/M8-explosion-ism-o1
```

- [ ] **Step 3: PR 생성**

base 는 **`dev`** 다(Gitflow — `main` 직접 금지). 이슈 #149 의 메타를 전부 미러링한다: label `enhancement`/`architecture`/`C++`, milestone `M8: 포트폴리오 하드닝 — 동작 불변 리팩터링`, assignee `leejimin3`, project `Project_RE 개발 로드맵`.

PR 본문은 긴 마크다운이라 **파일로 쓴 뒤 `--body-file` 로 넘긴다** — 인라인 문자열은 따옴표 매칭이 깨진다. 스크래치패드에 쓰고(리포를 더럽히지 않는다) 생성한다:

```bash
cd E:/UnrealProjects/Project_RE && gh pr create --base dev \
  --title "[M8] 폭발 FX 드로우콜 상수화 — ISM 다층으로 개수 무관하게 (#149)" \
  --body-file "$TMPDIR/pr149.md" \
  --label enhancement --label architecture --label "C++" \
  --milestone "M8: 포트폴리오 하드닝 — 동작 불변 리팩터링" \
  --assignee leejimin3
```

생성 후 프로젝트를 붙인다(`create` 에서 프로젝트 지정이 까다롭다). PR 번호는 물어보지 말고 브랜치에서 얻는다:

```bash
cd E:/UnrealProjects/Project_RE && PR=$(gh pr view --json number --jq .number) && \
  gh pr edit "$PR" --add-project "Project_RE 개발 로드맵" && \
  gh pr view "$PR" --json labels,milestone,assignees,projectItems --jq '{labels:[.labels[].name],milestone:.milestone.title,assignees:[.assignees[].login],projects:[.projectItems[].title]}'
```

기대: 네 필드 전부 비어있지 않고 이슈 #149 와 같다.

PR 본문에 반드시 넣을 것:
- 문제(드로우콜이 개수에 선형, #147 수치)
- 구조(층 2개, 왜 드로우콜이 상수인지)
- **Task 5 의 실측 표** — 예산 OFF 에서 드로우콜이 상수임을 보이는 수치
- 기각한 대안(NDC / 플립북)과 이유 — 설계 문서 §4.2 요약
- 게이트 표(Editor 빌드 / Server 풀 유니티 빌드 / 상수성 / 스크린샷)
- 한계(오버드로우는 선형으로 남음, 연출이 스타일라이즈로 바뀜)
- 팩 의존 해소(부수 이득: 팩 없는 클론에서도 폭발이 나온다)
- 끝에:
  ```
  🤖 Generated with [Claude Code](https://claude.com/claude-code)

  https://claude.ai/code/session_01VwKwm4y9AfXdPig49zdVcB
  ```

- [ ] **Step 4: 남은 정리 항목은 손대지 말고 보고**

이 PR 범위 밖이다(설계 문서 §10). 건드리지 말고 PR 본문에 후속으로 적기만 해라:
- `scripts/convert_explosion_fx.py` / `Content/FX/NS_REBulletExplosion.uasset` 가 고아가 된다 — 폴백 여지를 남겨 이 작업에서는 지우지 않는다
- `docs/profiling/M7-pattern-p99.md` 재측정, `README.md:143` · `docs/portfolio/portfolio-source.md:246` 의 "곡사 패턴은 렌더 스레드 병목 — 착지 폭발이 비용의 대부분" 결론 재검토
- 3번째 층(연기 대역), 연출 종류 분기

## 하지 말 것 (스코프 밖)

- **테스트 프레임워크 도입.** 이 리포에 자동화 테스트 인프라가 없다. 게이트는 빌드 + 프로브 로그 + 스크린샷 + CSV 다
- **`Content/FX/NS_REBulletExplosion.uasset` 삭제, `convert_explosion_fx.py` 삭제, `Build.cs` 에서 Niagara 제거.** Niagara 는 `RECharacterBase`·`REBossCharacter` 가 아직 쓴다
- **`M_ArenaMarker` / `make_arena_marker.py` 수정.** 링 로직을 참고는 하되 기존 마커 에셋에 회귀를 만들지 말 것 — 새 스크립트로 분리한다
- **3번째 ISM 층(연기 구체) 추가.** 스크린샷이 납작하다고 말할 때만, 별 작업으로
- **피격/착지 폭발 분기.** 호출부가 갈려 있어 쉽지만 지금 문제는 연출이 아니다
- **M7 프로파일 문서·README·포트폴리오 수치 갱신.** 별 이슈
- **다른 렌더 프로세서 리팩터링.** `SyncISM` 이 세 파일에 비슷한 형태로 있지만 공용 헬퍼로 빼지 말 것 — 이 작업의 범위가 아니다
