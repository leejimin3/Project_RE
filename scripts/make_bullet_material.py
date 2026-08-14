"""
M_REBullet 부트스트랩 생성 (#97).

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 생성 후 에디터에서 수치를 손보면
이 스크립트는 낡는다. 정본은 Content/Materials/M_REBullet.uasset 이다.
재실행하면 수동 튜닝이 날아가므로, 이미 존재하면 만들지 않고 중단한다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
"""
import unreal

def _arg(name, default):
    """커맨드라인에서 --name=값 을 읽는다. 튜닝 스윕을 리빌드 없이 돌리기 위한 것이다."""
    for tok in unreal.SystemLibrary.get_command_line().split():
        if tok.startswith("--%s=" % name):
            return float(tok.split("=", 1)[1])
    return default


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

# ISM 용도 플래그. 없으면 엔진이 조용히 기본 머티리얼로 대체한다 -
#   LogMaterial: Warning: Material ... missing usage flag InstancedStaticMeshes!
#                Default Material will be used in game.
# 이때 ISM->GetMaterial(0) 은 여전히 MID 를 돌려주므로 코드상으로는 정상으로 보이고,
# 화면만 라이팅된 회색 구체가 된다(언릿/색/프레넬이 전부 무시됨). 실제로 그렇게 한 번 속았다.
mat.set_editor_property("used_with_instanced_static_meshes", True)
if not mat.get_editor_property("used_with_instanced_static_meshes"):
    unreal.log_error("RE_MAT: InstancedStaticMeshes 용도 플래그 설정 실패")
    raise SystemExit(1)

mel = unreal.MaterialEditingLibrary

def _link(frm, frm_out, to, to_in):
    """연결 실패는 조용하다 - 핀 이름이 틀리면 False 만 돌아오고 그래프는 반쯤 빈 채로 저장된다."""
    if not mel.connect_material_expressions(frm, frm_out, to, to_in):
        unreal.log_error("RE_MAT: 연결 실패 %s -> %s.%s" % (frm.get_class().get_name(), to.get_class().get_name(), to_in))
        raise SystemExit(1)


def _link_prop(frm, frm_out, prop):
    if not mel.connect_material_property(frm, frm_out, prop):
        unreal.log_error("RE_MAT: 머티리얼 속성 연결 실패 %s" % str(prop))
        raise SystemExit(1)


# 두 색을 커스텀데이터 [1] 로 고른다. 인접 탄이 다른 색이 되어야 겹쳐도 개별 오브젝트로
# 읽힌다 - 탄 간격(30uu) < 지름(50uu) 이라 기하학적으로 겹치는 것을 색으로 가르는 것이다.
col_a = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1050, -80)
col_a.set_editor_property("parameter_name", "Color")
# 발광 세기. 스윕 실측(스크린샷 6종)으로 정한 값이다:
#   1.00 - 블룸이 코어를 하얗게 씻어 색 구분이 안 된다
#   0.50 - 파스텔로 뜬다. 구분은 되지만 채도가 약하다
#   0.15 - 선명한 빨강/파랑. 분리는 완벽하나 원색이라 톤이 강하다
#   0.18 + desat 0.40 - 부드러운 분홍/하늘. 분리 유지          <- 채택
#   0.08 - 선명하지만 약간 어둡다
# 주의: 밝기와 파스텔화는 곱해진다. sat 0.35 + desat 0.35 는 다시 흰색으로 씻긴다
# (실측). 파스텔은 밝기가 아니라 색 자체로 만들어야 한다.
# 씬 바닥이 검어서 낮은 값도 충분히 보인다. 올리면 블룸이 채도를 먹는다.
SAT = _arg("sat", 0.18)
# 파스텔 정도. 0 = 순색, 1 = 흰색. 색을 흰쪽으로 당겨 톤을 부드럽게 한다.
# 발광을 올려 흰색을 섞으면 블룸이 코어를 씻어 색 구분이 죽으므로, 밝기가 아니라
# 색 자체로 파스텔을 만든다.
DESAT = _arg("desat", 0.40)


def _pastel(r, g, b):
    return unreal.LinearColor((r + (1.0 - r) * DESAT) * SAT,
                              (g + (1.0 - g) * DESAT) * SAT,
                              (b + (1.0 - b) * DESAT) * SAT, 1.0)


col_a.set_editor_property("default_value", _pastel(1.0, 0.06, 0.06))

col_b = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -1050, 60)
col_b.set_editor_property("parameter_name", "ColorB")
col_b.set_editor_property("default_value", _pastel(0.06, 0.25, 1.0))

sel = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -1050, 200)
sel.set_editor_property("data_index", 1)
sel.set_editor_property("const_default_value", 0.0)

col = mel.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -800, 0)
_link(col_a, "", col, "A")
_link(col_b, "", col, "B")
_link(sel, "", col, "Alpha")

# 프레넬 - 실루엣 가장자리를 밝힌다. 인접한 동일 색 구체 사이에 경계선이 생기는 원리.
fr = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -800, 220)

rim_pow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1050, 220)
rim_pow.set_editor_property("parameter_name", "RimPower")
rim_pow.set_editor_property("default_value", 2.5)
_link(rim_pow, "", fr, "ExponentIn")

rim_str = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -800, 400)
rim_str.set_editor_property("parameter_name", "RimStrength")
rim_str.set_editor_property("default_value", _arg("rim", 1.0))   # 스윕 실측: 3.0 은 가장자리가 과하게 튄다

# 림 기여: 1 + Fresnel * RimStrength -> 중심은 기본 밝기, 가장자리만 솟는다.
rim_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -560, 300)
_link(fr, "", rim_mul, "A")
_link(rim_str, "", rim_mul, "B")

one = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -560, 460)
one.set_editor_property("r", 1.0)

rim_add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -380, 360)
_link(one, "", rim_add, "A")
_link(rim_mul, "", rim_add, "B")

col_rim = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 120)
_link(col, "", col_rim, "A")
_link(rim_add, "", col_rim, "B")

# 스폰 팝 - 퍼인스턴스 커스텀데이터 0번. 렌더 프로세서가 0~1 을 채운다.
# ConstDefaultValue 는 인스턴스 데이터가 없을 때의 대체값이다. 1.0 이라야 커스텀데이터가
# 아직 배선되지 않은 상태에서도 탄이 정상 밝기로 보인다(검게 사라지지 않는다).
pop = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -380, 620)
pop.set_editor_property("data_index", 0)
pop.set_editor_property("const_default_value", 1.0)

final_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 0, 300)
_link(col_rim, "", final_mul, "A")
_link(pop, "", final_mul, "B")

_link_prop(final_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log("RE_MAT: 생성 완료 %s" % FULL)
