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
