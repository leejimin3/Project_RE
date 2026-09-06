"""
M_ArenaFloor 부트스트랩 생성 (#122).

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 정본은 Content/Materials/M_ArenaFloor.uasset 이다.
재실행하면 수동 튜닝이 날아가므로, 이미 존재하면 만들지 않고 중단한다(--force 로 강제).

왜 팩 MI 를 안 쓰고 텍스처만 빌려오는가:
  Ithris 팩의 타일러블 MI 를 그대로 붙여봤고 두 후보 모두 탈락했다.
    - MI_Tiles_Concrete_Painted_01a : 장식 타일 무늬가 강해 탄이 묻힌다(#122 제약, 실측)
    - MI_StoneSurface_01            : 부모 MM_Base_POM 의 HUE 파라미터가 사실상 안 먹는다.
                                      HUE 2.5 -> 8.0 으로 올려도 화면 밝기가 거의 안 변했다(실측)
  바닥은 화면의 거의 전부이고(카메라 pitch -50 / arm 1500) 탄 대비의 기준면이라 밝기를 직접
  제어할 수 있어야 한다. 그래서 팩의 '텍스처'만 가져다 쓰고 곱셈은 우리가 한다 -
  디테일(노말/ORM)과 밝기 통제를 둘 다 가진다.

팩 의존:
  Content/IthrisCemetery/ 는 gitignore 대상이다(유료). 포트폴리오 영상 산출이 목적이라
  의존을 허용한 판단이다. 팩이 없는 환경에서는 텍스처 파라미터가 비어 흰 바닥이 된다 -
  Albedo/Normal/ORM 은 전부 '파라미터'라 에디터에서 다른 텍스처로 갈아끼울 수 있다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
"""
import unreal

PKG = "/Game/Materials"
NAME = "M_ArenaFloor"
FULL = PKG + "/" + NAME

TEX_DIR = "/Game/IthrisCemetery/Textures/Tilable/StoneSurface_01"
TEX = {
    "Albedo": TEX_DIR + "/T_StoneSurface_01_basecolor",
    "Normal": TEX_DIR + "/T_StoneSurface_01_normal",
    "ORM":    TEX_DIR + "/T_StoneSurface_01_ORM",
}

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    # split() 로 토큰 비교하면 안 된다 - -ExecutePythonScript="<경로> --force" 로 넘길 때
    # 닫는 따옴표가 마지막 토큰에 붙어 '--force"' 가 되어 조용히 안 먹는다(실측).
    if "--force" not in unreal.SystemLibrary.get_command_line():
        unreal.log_error("RE_FLOOR: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % FULL)
        unreal.log_error("RE_FLOOR: 재생성이 정말 필요하면 커맨드라인에 --force 를 넣어라.")
        raise SystemExit(1)
    # 파생 인스턴스가 참조 중이면 부모 삭제가 조용히 실패하고 create_asset 이 None 을
    # 돌려준다("생성 실패"). 인스턴스를 먼저 지운다 - setup_level_lighting.py 가 다시
    # 만들어 레벨 바닥에 붙인다.
    DERIVED = PKG + "/MI_ArenaFloor"
    if unreal.EditorAssetLibrary.does_asset_exist(DERIVED):
        unreal.log_warning("RE_FLOOR: 파생 인스턴스 선삭제 %s" % DERIVED)
        unreal.EditorAssetLibrary.delete_asset(DERIVED)
    unreal.log_warning("RE_FLOOR: --force - 기존 에셋을 지우고 재생성한다: %s" % FULL)
    unreal.EditorAssetLibrary.delete_asset(FULL)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    unreal.log_error("RE_FLOOR: 생성 실패")
    raise SystemExit(1)

mel = unreal.MaterialEditingLibrary


def expr(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def link(frm, frm_out, to, to_in):
    """입력 핀 이름은 노드마다 제각각이고, 실패해도 False 만 조용히 돌아온다."""
    if mel.connect_material_expressions(frm, frm_out, to, to_in):
        return
    if to_in and mel.connect_material_expressions(frm, frm_out, to, ""):
        return
    unreal.log_error("RE_FLOOR: 연결 실패 %s.%s -> %s.%s" % (
        frm.get_class().get_name(), frm_out, to.get_class().get_name(), to_in))
    raise SystemExit(1)


def prop(frm, frm_out, p):
    if not mel.connect_material_property(frm, frm_out, p):
        unreal.log_error("RE_FLOOR: 머티리얼 속성 연결 실패 %s" % str(p))
        raise SystemExit(1)


def tex_param(name, path, x, y, srgb=True):
    t = expr(unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    t.set_editor_property("parameter_name", name)
    asset = unreal.load_asset(path)
    if asset is None:
        unreal.log_warning("RE_FLOOR: 텍스처 없음(파라미터는 남는다) %s" % path)
    else:
        t.set_editor_property("texture", asset)
    if not srgb:
        t.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return t


# --- UV -------------------------------------------------------------------
# 바닥은 Cube(100uu)를 40배 늘린 4000uu 판이라 UV 가 그만큼 늘어난다. 타일링을 크게 잡아야
# 돌 결이 보인다. 다만 너무 잘게 쪼개면 무늬가 노이즈가 되어 탄을 먹는다(실측 - Tiling 20 은
# 화면이 지글거렸다). 8 근처가 디테일과 가독성의 균형점이다.
uv = expr(unreal.MaterialExpressionTextureCoordinate, -1600, 0)
tiling = expr(unreal.MaterialExpressionScalarParameter, -1600, 160)
tiling.set_editor_property("parameter_name", "Tiling")
# 대리석 한 장이 커야 대리석으로 읽힌다. 8.0 은 타일이 잘아서 '자갈 바닥'이 됐다(실측).
tiling.set_editor_property("default_value", 2.5)
uv_mul = expr(unreal.MaterialExpressionMultiply, -1400, 40)
link(uv, "", uv_mul, "A")
link(tiling, "", uv_mul, "B")

# --- BaseColor -------------------------------------------------------------
albedo = tex_param("Albedo", TEX["Albedo"], -1150, -260)
link(uv_mul, "", albedo, "UVs")

# 팩 알베도는 어두운 석재다. 밝은 돌바닥으로 끌어올리려면 곱해야 한다.
# 중요: tint 는 '중성'이어야 한다. 여기에 보라를 섞으면 바닥이 빛을 반사하는 게 아니라
# 스스로 보라색인 플라스틱처럼 보인다(실측 - 1.7/1.6/1.9 로 밀었더니 대리석이 아니라
# 자체발광 보라판이 됐다). 화면의 색은 전부 조명에서 와야 한다.
tint = expr(unreal.MaterialExpressionVectorParameter, -1150, -80)
tint.set_editor_property("parameter_name", "BaseColor")
tint.set_editor_property("default_value", unreal.LinearColor(1.55, 1.52, 1.58, 1.0))

bc = expr(unreal.MaterialExpressionMultiply, -820, -200)
link(albedo, "RGB", bc, "A")
link(tint, "", bc, "B")

# --- 얼룩(대리석 무늬) -----------------------------------------------------
# 타일 텍스처만으로는 4000uu 판이 균일해 보인다. 타일링과 무관한 '월드 좌표' 노이즈를
# 곱해 큰 얼룩을 만든다 - UV 기반으로 하면 타일마다 같은 무늬가 반복돼 오히려 규칙적으로
# 보인다. 강도는 파라미터로 뺀다(무늬가 세면 탄이 묻힌다, #122 제약).
blotch_scale = expr(unreal.MaterialExpressionScalarParameter, -1600, 900)
blotch_scale.set_editor_property("parameter_name", "BlotchScale")
blotch_scale.set_editor_property("default_value", 0.0016)
wpos = expr(unreal.MaterialExpressionWorldPosition, -1600, 1040)
bpos = expr(unreal.MaterialExpressionMultiply, -1360, 960)
link(wpos, "", bpos, "A")
link(blotch_scale, "", bpos, "B")

bnoise = expr(unreal.MaterialExpressionNoise, -1150, 960)
bnoise.set_editor_property("scale", 1.0)
bnoise.set_editor_property("levels", 4)
bnoise.set_editor_property("output_min", 0.0)
bnoise.set_editor_property("output_max", 1.0)
bnoise.set_editor_property("turbulence", True)
link(bpos, "", bnoise, "Position")

blotch_amt = expr(unreal.MaterialExpressionScalarParameter, -1150, 1160)
blotch_amt.set_editor_property("parameter_name", "BlotchAmount")
blotch_amt.set_editor_property("default_value", 0.45)
one = expr(unreal.MaterialExpressionConstant, -1150, 1260)
one.set_editor_property("r", 1.0)
blotch = expr(unreal.MaterialExpressionLinearInterpolate, -900, 1040)
link(one, "", blotch, "A")
link(bnoise, "", blotch, "B")
link(blotch_amt, "", blotch, "Alpha")

bc2 = expr(unreal.MaterialExpressionMultiply, -600, -160)
link(bc, "", bc2, "A")
link(blotch, "", bc2, "B")
prop(bc2, "", unreal.MaterialProperty.MP_BASE_COLOR)

# --- Normal ----------------------------------------------------------------
nrm_tex = tex_param("Normal", TEX["Normal"], -1150, 200, srgb=False)
nrm_tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
link(uv_mul, "", nrm_tex, "UVs")

# 노말 세기. 탑다운 1500 거리에서는 세게 줘야 결이 읽힌다.
nrm_int = expr(unreal.MaterialExpressionScalarParameter, -1150, 400)
nrm_int.set_editor_property("parameter_name", "NormalIntensity")
nrm_int.set_editor_property("default_value", 1.6)
flat = expr(unreal.MaterialExpressionConstant3Vector, -1150, 500)
flat.set_editor_property("constant", unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
nrm = expr(unreal.MaterialExpressionLinearInterpolate, -820, 300)
link(flat, "", nrm, "A")
link(nrm_tex, "RGB", nrm, "B")
link(nrm_int, "", nrm, "Alpha")
prop(nrm, "", unreal.MaterialProperty.MP_NORMAL)

# --- Roughness / AO --------------------------------------------------------
# ORM: R=AmbientOcclusion, G=Roughness, B=Metallic 규약이다.
orm = tex_param("ORM", TEX["ORM"], -1150, 640, srgb=False)
link(uv_mul, "", orm, "UVs")

rough_scale = expr(unreal.MaterialExpressionScalarParameter, -1150, 860)
rough_scale.set_editor_property("parameter_name", "Roughness")
# 대리석은 반사가 있어야 대리석으로 읽힌다. ORM 의 러프니스를 그대로 쓰면 팩의 거친 석재라
# 무광이 된다 - 곱해서 내린다.
rough_scale.set_editor_property("default_value", 0.30)
rough = expr(unreal.MaterialExpressionMultiply, -820, 700)
link(orm, "G", rough, "A")
link(rough_scale, "", rough, "B")
prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
prop(orm, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

# 스펙큘러. 올리면 하늘/랜턴이 바닥에 비쳐 '광택 있는 돌'이 된다.
spec = expr(unreal.MaterialExpressionScalarParameter, -820, 900)
spec.set_editor_property("parameter_name", "Specular")
spec.set_editor_property("default_value", 0.8)
prop(spec, "", unreal.MaterialProperty.MP_SPECULAR)

mel.recompile_material(mat)
# only_if_is_dirty=False. MaterialEditingLibrary 로 만든 변경은 패키지를 dirty 로
# 표시하지 않아서, 기본값(True)으로 부르면 "생성 완료" 로그만 찍히고 디스크에는
# 아무것도 안 써진다. 실제로 재생성이 여러 번 조용히 버려졌다(uasset 타임스탬프로 확인).
unreal.EditorAssetLibrary.save_asset(FULL, False)
unreal.log("RE_FLOOR: 생성 완료 %s" % FULL)
