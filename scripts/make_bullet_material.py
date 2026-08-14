"""
M_REBullet 부트스트랩 생성 (#97).

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 생성 후 에디터에서 수치를 손보면
이 스크립트는 낡는다. 정본은 Content/Materials/M_REBullet.uasset 이다.
재실행하면 수동 튜닝이 날아가므로, 이미 존재하면 만들지 않고 중단한다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
"""
import unreal

PKG = "/Game/Materials"
NAME = "M_REBullet"
FULL = PKG + "/" + NAME

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    if "--force" not in unreal.SystemLibrary.get_command_line().split():
        unreal.log_error("RE_MAT: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % FULL)
        unreal.log_error("RE_MAT: 재생성이 정말 필요하면 커맨드라인에 --force 를 넣어라.")
        raise SystemExit(1)
    unreal.log_warning("RE_MAT: --force - 기존 에셋을 지우고 재생성한다: %s" % FULL)
    unreal.EditorAssetLibrary.delete_asset(FULL)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    unreal.log_error("RE_MAT: 생성 실패")
    raise SystemExit(1)

# 언릿 - 씬 조명과 무관하게 항상 같은 밝기. 탄막 가독성이 조명 방향에 흔들리지 않는다.
# Blend Mode 는 팩토리 기본값(Opaque)을 그대로 둔다 - 불투명이라야 early-Z 가 살아
# 겹친 탄의 오버드로우가 누적되지 않는다(50,000발 상한을 지탱하는 전제).
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

mel = unreal.MaterialEditingLibrary

# 두 색을 커스텀데이터 [1] 로 고른다. 인접 탄이 다른 색이 되어야 겹쳐도 개별 오브젝트로
# 읽힌다 - 탄 간격(30uu) < 지름(50uu) 이라 기하학적으로 겹치는 것을 색으로 가르는 것이다.
col_a = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1050, -80)
col_a.set_editor_property("parameter_name", "Color")
col_a.set_editor_property("default_value", unreal.LinearColor(1.0, 0.12, 0.12, 1.0))

col_b = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1050, 60)
col_b.set_editor_property("parameter_name", "ColorB")
col_b.set_editor_property("default_value", unreal.LinearColor(0.12, 0.4, 1.0, 1.0))

sel = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -1050, 200)
sel.set_editor_property("data_index", 1)
sel.set_editor_property("const_default_value", 0.0)

col = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -800, 0)
mel.connect_material_expressions(col_a, "", col, "A")
mel.connect_material_expressions(col_b, "", col, "B")
mel.connect_material_expressions(sel, "", col, "Alpha")

# 프레넬 - 실루엣 가장자리를 밝힌다. 인접한 동일 색 구체 사이에 경계선이 생기는 원리.
fr = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -800, 220)

rim_pow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1050, 220)
rim_pow.set_editor_property("parameter_name", "RimPower")
rim_pow.set_editor_property("default_value", 2.5)
mel.connect_material_expressions(rim_pow, "", fr, "ExponentIn")

rim_str = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -800, 400)
rim_str.set_editor_property("parameter_name", "RimStrength")
rim_str.set_editor_property("default_value", 3.0)

# 림 기여: 1 + Fresnel * RimStrength -> 중심은 기본 밝기, 가장자리만 솟는다.
rim_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -560, 300)
mel.connect_material_expressions(fr, "", rim_mul, "A")
mel.connect_material_expressions(rim_str, "", rim_mul, "B")

one = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -560, 460)
one.set_editor_property("r", 1.0)

rim_add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -380, 360)
mel.connect_material_expressions(one, "", rim_add, "A")
mel.connect_material_expressions(rim_mul, "", rim_add, "B")

col_rim = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 120)
mel.connect_material_expressions(col, "", col_rim, "A")
mel.connect_material_expressions(rim_add, "", col_rim, "B")

# 스폰 팝 - 퍼인스턴스 커스텀데이터 0번. 렌더 프로세서가 0~1 을 채운다.
# ConstDefaultValue 는 인스턴스 데이터가 없을 때의 대체값이다. 1.0 이라야 커스텀데이터가
# 아직 배선되지 않은 상태에서도 탄이 정상 밝기로 보인다(검게 사라지지 않는다).
pop = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -380, 620)
pop.set_editor_property("data_index", 0)
pop.set_editor_property("const_default_value", 1.0)

final_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 0, 300)
mel.connect_material_expressions(col_rim, "", final_mul, "A")
mel.connect_material_expressions(pop, "", final_mul, "B")

mel.connect_material_property(final_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log("RE_MAT: 생성 완료 %s" % FULL)
