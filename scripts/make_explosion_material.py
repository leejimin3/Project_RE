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
