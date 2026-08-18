"""
M_ArenaMarker 부트스트랩 생성 (#122).

곡사탄 착지 예고 표시. 게임의 '경고 표시'처럼 아주 얇고 반투명한 링이어야 한다.

이전 상태가 왜 원기둥으로 보였나:
  REBulletRenderSubsystem.cpp 가 /Engine/BasicShapes/Cylinder 를 쓰고,
  REArcRenderProcessor.cpp 의 MarkerThickness 가 1.0 이었다. Cylinder 는 높이가 100uu 라
  스케일 1.0 = 두께 100uu 짜리 실린더다. 납작한 디스크가 아니라 낮은 원기둥이 맞았다.
  → 메시를 Plane 으로 바꾸고(두께 개념 자체가 사라진다) 이 머티리얼을 씌운다.

Translucent 를 쓰는 이유:
  경고 표시는 바닥이 비쳐야 한다. 다만 반투명은 오버드로우가 있으므로 마커에만 쓴다 -
  탄환 본체(M_REBullet)는 50,000발 상한을 지탱하려고 Opaque 를 유지한다(#50).

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
  인자: --force --radius=0.42 --edge=0.06 --opacity=0.45
"""
import unreal

PKG = "/Game/Materials"
NAME = "M_ArenaMarker"
FULL = PKG + "/" + NAME
CMD = unreal.SystemLibrary.get_command_line()


def _arg(name, default):
    for tok in CMD.split():
        if tok.startswith("--%s=" % name):
            return float(tok.split("=", 1)[1].strip("\"'"))
    return default


if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    if "--force" not in CMD:
        unreal.log_error("RE_MARK: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % FULL)
        raise SystemExit(1)
    unreal.log_warning("RE_MARK: --force - 기존 에셋을 지우고 재생성한다: %s" % FULL)
    unreal.EditorAssetLibrary.delete_asset(FULL)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    unreal.log_error("RE_MARK: 생성 실패 - 아직 누가 참조 중일 수 있다")
    raise SystemExit(1)

mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("two_sided", True)
# ISM 으로 그려진다. 이 플래그가 없으면 엔진이 조용히 기본 머티리얼로 대체한다(#97 이력).
mat.set_editor_property("used_with_instanced_static_meshes", True)

mel = unreal.MaterialEditingLibrary


def expr(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def link(frm, frm_out, to, to_in):
    if mel.connect_material_expressions(frm, frm_out, to, to_in):
        return
    if to_in and mel.connect_material_expressions(frm, frm_out, to, ""):
        return
    unreal.log_error("RE_MARK: 연결 실패 %s.%s -> %s.%s" % (
        frm.get_class().get_name(), frm_out, to.get_class().get_name(), to_in))
    raise SystemExit(1)


def scalar(name, value, x, y):
    e = expr(unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


# --- UV 중심으로부터의 거리 -------------------------------------------------
uv = expr(unreal.MaterialExpressionTextureCoordinate, -1200, 0)
center = expr(unreal.MaterialExpressionConstant2Vector, -1200, 160)
center.set_editor_property("r", 0.5)
center.set_editor_property("g", 0.5)
dist = expr(unreal.MaterialExpressionDistance, -960, 60)
link(uv, "", dist, "A")
link(center, "", dist, "B")

# --- 원판 마스크 -----------------------------------------------------------
# 속이 꽉 찬 원이다. 링(테두리만)으로 만들었더니 가운데가 비어 경고 표시로 안 읽혔다(#122 피드백).
# dist 가 Radius 안이면 1, 밖으로 Edge 만큼 걸쳐 0 으로 떨어진다 - 아웃라인 없음.
radius = scalar("Radius", _arg("radius", 0.42), -1200, 320)
width = scalar("Edge", _arg("edge", 0.06), -1200, 420)

delta = expr(unreal.MaterialExpressionSubtract, -760, 120)
link(dist, "", delta, "A")
link(radius, "", delta, "B")

norm = expr(unreal.MaterialExpressionDivide, -440, 160)
link(delta, "", norm, "A")
link(width, "", norm, "B")

inv = expr(unreal.MaterialExpressionOneMinus, -280, 160)
link(norm, "", inv, "")
ring = expr(unreal.MaterialExpressionClamp, -140, 160)
link(inv, "", ring, "Input")

# --- 색 / 불투명도 ---------------------------------------------------------
# 파라미터 이름 "Color" 는 코드가 MID 로 덮어쓰는 이름이다
# (REBulletRenderSubsystem.cpp 의 SetVectorParameterValue(TEXT("Color"), ...)).
# 바꾸면 조용히 안 먹는다.
col = expr(unreal.MaterialExpressionVectorParameter, -600, 380)
col.set_editor_property("parameter_name", "Color")
col.set_editor_property("default_value", unreal.LinearColor(1.6, 0.10, 0.10, 1.0))

emissive = expr(unreal.MaterialExpressionMultiply, 40, 240)
link(col, "", emissive, "A")
link(ring, "", emissive, "B")
if not mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    unreal.log_error("RE_MARK: 이미시브 연결 실패")
    raise SystemExit(1)

opacity_p = scalar("Opacity", _arg("opacity", 0.55), -600, 520)
opacity = expr(unreal.MaterialExpressionMultiply, 40, 460)
link(ring, "", opacity, "A")
link(opacity_p, "", opacity, "B")
if not mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
    unreal.log_error("RE_MARK: 오파시티 연결 실패")
    raise SystemExit(1)

mel.recompile_material(mat)
# only_if_is_dirty=False - MaterialEditingLibrary 변경은 dirty 플래그를 안 세워서
# 기본값으로 부르면 로그만 찍히고 디스크에는 안 써진다(실측).
unreal.EditorAssetLibrary.save_asset(FULL, False)
unreal.log("RE_MARK: 생성 완료 %s (blend=%s)" % (FULL, mat.get_editor_property("blend_mode")))
