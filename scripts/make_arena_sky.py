"""
M_ArenaSky 부트스트랩 생성 (#122).

밤하늘 배경. 구성은 다섯 층이다:
  1. 바탕   - 위(남색) -> 아래(보라) 세로 그라디언트
  2. 띠     - 은하수 위도 마스크
  3. 성운   - 띠 안쪽에 구름 텍스처를 깔아 부드러운 얼룩
  4. 미세별 - 촘촘한 타일링, 약한 밝기
  5. 큰별   - 성긴 타일링, 강한 밝기 (별 크기/밝기 변화를 만든다)

설계 제약 - 왜 이렇게 만들었나:
  이전 버전은 AbsoluteWorldPosition -> Normalize -> DotProduct -> Noise 로 은하수 띠를
  만들었는데, 연결도 에러도 없이 출력이 0 이었다. 구를 눈앞에 놓고 찍으면 순수 검정,
  --only=nebula 로 항을 분리해도 검정이었다(원인 미규명).
  그래서 그 체인을 통째로 버리고 화면으로 검증된 노드만 쓴다:
    - VertexNormalWS + ComponentMask(B)   : 세로 그라디언트. 구 법선이 곧 방향이라
                                            월드좌표 빼기/정규화가 필요 없다
    - TextureCoordinate + ComponentMask(G): 구 UV 의 V 가 곧 위도 -> 내적 없이 띠가 나온다
    - TextureSample / Multiply / Add / Lerp / Abs / OneMinus / Clamp / Subtract / Divide

  구는 setup_level_props.py 에서 roll 90 으로 눕혀 놓는다(극점을 시야 밖으로 빼려고).
  그래서 이 '위도 띠'가 화면을 비스듬히 가로지른다 - 은하수 구도와 맞는다.

디버그:
  --only=base|band|nebula|stars|bigstars 로 한 항만 이미시브에 직결한다.
  여러 항을 더해 놓으면 어느 항이 0 인지 화면으로 구분할 수 없다. 조용히 죽은 항을 찾는
  유일한 수단이라 남겨둔다.

실행 (재생성할 때는 순서가 있다):
  1) setup_level_props.py --backdrop=0   <- 레벨의 하늘 액터를 먼저 치운다
  2) make_arena_sky.py --force [--hor_r=..]  <- 안 치우면 레벨이 참조 중이라 삭제가 거부되고
                                                create_asset 이 None 을 돌려준다("생성 실패")
  3) setup_level_props.py                <- 하늘 구를 다시 세운다
"""
import unreal

PKG = "/Game/Materials"
NAME = "M_ArenaSky"
FULL = PKG + "/" + NAME
STAR_TEX = "/Engine/EngineSky/T_Sky_Stars"
CLOUD_TEX = "/Engine/EngineSky/T_Sky_Clouds_M"

CMD = unreal.SystemLibrary.get_command_line()


def _arg(name, default):
    """색/강도를 MaterialInstance 오버라이드가 아니라 머티리얼 기본값에 직접 굽는다.
    MI 경로로는 값이 저장되지 않았다 - set_material_instance_*_parameter_value 로 쓰고
    update_material_instance + save_asset(dirty 무시)까지 해도 MI 에는 직전 실행 값이
    그대로 남았다(실측). 파라미터는 그대로 남으니 에디터에서 인스턴스로 덮는 건 가능하다."""
    for tok in CMD.split():
        if tok.startswith("--%s=" % name):
            return float(tok.split("=", 1)[1].strip("\"'"))
    return default


if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    if "--force" not in CMD:
        unreal.log_error("RE_SKY: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % FULL)
        unreal.log_error("RE_SKY: 재생성이 정말 필요하면 커맨드라인에 --force 를 넣어라.")
        raise SystemExit(1)
    unreal.log_warning("RE_SKY: --force - 기존 에셋을 지우고 재생성한다: %s" % FULL)
    unreal.EditorAssetLibrary.delete_asset(FULL)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    unreal.log_error("RE_SKY: 생성 실패 - 아직 누가 참조 중일 수 있다(레벨의 하늘 액터부터 치워라)")
    raise SystemExit(1)

# Unlit  : 씬 조명을 안 받는다 -> 바닥 그림자가 하늘에 찍히지 않는다
# TwoSided: 구 안쪽에서 보므로 뒷면이 그려져야 한다
# IsSky   : 끈다. 켜면 엔진이 커버리지 검증 경고를 게임 화면에 그린다
#           ("SKYDOME MESH ... DOES NOT COVER THAT PART OF THE SCREEN"). SkyLight 실시간
#           캡처를 껐으므로 필요 없다.
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
mat.set_editor_property("is_sky", "--issky" in CMD)

mel = unreal.MaterialEditingLibrary


def expr(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def link(frm, frm_out, to, to_in):
    """입력 핀 이름이 노드마다 제각각이고(무명 핀이 흔하다), 실패해도 False 만 조용히 돌아온다."""
    if mel.connect_material_expressions(frm, frm_out, to, to_in):
        return
    if to_in and mel.connect_material_expressions(frm, frm_out, to, ""):
        return
    unreal.log_error("RE_SKY: 연결 실패 %s.%s -> %s.%s" % (
        frm.get_class().get_name(), frm_out, to.get_class().get_name(), to_in))
    raise SystemExit(1)


def scalar(name, value, x, y):
    e = expr(unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", value)
    return e


def vector(name, color, x, y):
    e = expr(unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", color)
    return e


def const(v, x, y):
    e = expr(unreal.MaterialExpressionConstant, x, y)
    e.set_editor_property("r", v)
    return e


def tex_layer(path, tiling_name, tiling_def, x, y):
    """텍스처를 지정 타일링으로 샘플링한다. 같은 텍스처를 다른 타일링으로 여러 번 쓰면
    별 크기가 달라진다 - 균일한 점 하나로는 하늘이 벽지처럼 보인다."""
    uv = expr(unreal.MaterialExpressionTextureCoordinate, x - 400, y)
    tl = scalar(tiling_name, tiling_def, x - 400, y + 120)
    uvm = expr(unreal.MaterialExpressionMultiply, x - 200, y + 40)
    link(uv, "", uvm, "A")
    link(tl, "", uvm, "B")
    tx = expr(unreal.MaterialExpressionTextureSample, x, y)
    a = unreal.load_asset(path)
    if a is None:
        unreal.log_warning("RE_SKY: 텍스처 없음 %s" % path)
    else:
        tx.set_editor_property("texture", a)
    link(uvm, "", tx, "UVs")
    return tx


# 밝기 주의: PostProcessVolume 이 자동노출을 EV 0 으로 고정한다.
# 그래서 이미시브 0.03 이 화면에서는 중간톤 보라로 뜬다 - 밤하늘을 만들려면 값이
# 한 자릿수 더 낮아야 한다(0.03 -> 0.005 수준). 실측으로 잡은 기본값이다.
# --- 1. 바탕 그라디언트 ------------------------------------------------------
n = expr(unreal.MaterialExpressionVertexNormalWS, -2000, 0)
nz = expr(unreal.MaterialExpressionComponentMask, -1800, 0)
nz.set_editor_property("r", False)
nz.set_editor_property("g", False)
nz.set_editor_property("b", True)
nz.set_editor_property("a", False)
link(n, "", nz, "")

half = const(0.5, -1800, 140)
t_mul = expr(unreal.MaterialExpressionMultiply, -1600, 40)
link(nz, "", t_mul, "A")
link(half, "", t_mul, "B")
t01 = expr(unreal.MaterialExpressionAdd, -1450, 40)      # -1..1 -> 0..1
link(t_mul, "", t01, "A")
link(half, "", t01, "B")

bottom = vector("HorizonColor", unreal.LinearColor(
    _arg("hor_r", 0.0055), _arg("hor_g", 0.0020), _arg("hor_b", 0.0135), 1.0), -1450, 200)
top = vector("ZenithColor", unreal.LinearColor(
    _arg("zen_r", 0.0010), _arg("zen_g", 0.0014), _arg("zen_b", 0.0055), 1.0), -1450, 340)
base = expr(unreal.MaterialExpressionLinearInterpolate, -1200, 140)
link(bottom, "", base, "A")
link(top, "", base, "B")
link(t01, "", base, "Alpha")

# --- 2. 은하수 띠 (위도 마스크) ----------------------------------------------
# 구 UV 의 V 가 위도다. 내적/노이즈 없이 띠가 나온다.
buv = expr(unreal.MaterialExpressionTextureCoordinate, -2000, 560)
bv = expr(unreal.MaterialExpressionComponentMask, -1800, 560)
bv.set_editor_property("r", False)
bv.set_editor_property("g", True)
bv.set_editor_property("b", False)
bv.set_editor_property("a", False)
link(buv, "", bv, "")

band_c = scalar("BandCenter", _arg("band_c", 0.40), -1800, 700)
band_w = scalar("BandWidth", _arg("band_w", 0.16), -1800, 800)
bsub = expr(unreal.MaterialExpressionSubtract, -1600, 600)
link(bv, "", bsub, "A")
link(band_c, "", bsub, "B")
babs = expr(unreal.MaterialExpressionAbs, -1450, 600)
link(bsub, "", babs, "")
bdiv = expr(unreal.MaterialExpressionDivide, -1300, 640)
link(babs, "", bdiv, "A")
link(band_w, "", bdiv, "B")
binv = expr(unreal.MaterialExpressionOneMinus, -1150, 640)
link(bdiv, "", binv, "")
band = expr(unreal.MaterialExpressionClamp, -1000, 640)
link(binv, "", band, "Input")

# --- 3. 성운 (띠 안쪽 구름) --------------------------------------------------
cloud = tex_layer(CLOUD_TEX, "NebulaTiling", _arg("neb_tile", 3.0), -1000, 900)
neb_col = vector("NebulaColor", unreal.LinearColor(
    _arg("neb_r", 0.40), _arg("neb_g", 0.22), _arg("neb_b", 0.85), 1.0), -1000, 1120)
neb_i = scalar("NebulaIntensity", _arg("neb_i", 0.07), -1000, 1220)
c1 = expr(unreal.MaterialExpressionMultiply, -700, 950)
link(cloud, "R", c1, "A")
link(band, "", c1, "B")                 # 띠 안쪽에만 얹는다
c2 = expr(unreal.MaterialExpressionMultiply, -550, 1010)
link(c1, "", c2, "A")
link(neb_col, "", c2, "B")
nebula = expr(unreal.MaterialExpressionMultiply, -400, 1070)
link(c2, "", nebula, "A")
link(neb_i, "", nebula, "B")

# --- 4. 미세 별 -------------------------------------------------------------
fine = tex_layer(STAR_TEX, "StarTiling", _arg("star_tile", 30.0), -1000, 1500)
fine_col = vector("StarColor", unreal.LinearColor(
    _arg("star_r", 0.78), _arg("star_g", 0.84), _arg("star_b", 1.0), 1.0), -1000, 1720)
fine_amt = scalar("StarBrightness", _arg("star", 0.30), -1000, 1820)
f1 = expr(unreal.MaterialExpressionMultiply, -700, 1550)
link(fine, "R", f1, "A")
link(fine_col, "", f1, "B")
stars = expr(unreal.MaterialExpressionMultiply, -550, 1610)
link(f1, "", stars, "A")
link(fine_amt, "", stars, "B")

# --- 5. 큰 별 ---------------------------------------------------------------
# 같은 텍스처를 성기게 감으면 점이 커진다. 밝기를 올려 '눈에 띄는 별 몇 개'를 만든다.
big = tex_layer(STAR_TEX, "BigStarTiling", _arg("big_tile", 11.0), -1000, 2100)
big_amt = scalar("BigStarBrightness", _arg("big", 0.55), -1000, 2320)
b1 = expr(unreal.MaterialExpressionMultiply, -700, 2150)
link(big, "R", b1, "A")
link(fine_col, "", b1, "B")
bigstars = expr(unreal.MaterialExpressionMultiply, -550, 2210)
link(b1, "", bigstars, "A")
link(big_amt, "", bigstars, "B")

# --- 합성 ------------------------------------------------------------------
a1 = expr(unreal.MaterialExpressionAdd, -250, 300)
link(base, "", a1, "A")
link(nebula, "", a1, "B")
a2 = expr(unreal.MaterialExpressionAdd, -120, 520)
link(a1, "", a2, "A")
link(stars, "", a2, "B")
final = expr(unreal.MaterialExpressionAdd, 20, 740)
link(a2, "", final, "A")
link(bigstars, "", final, "B")

_only = None
for _tok in CMD.split():
    if _tok.startswith("--only="):
        _only = _tok.split("=", 1)[1].strip("\"'")
final = {"base": base, "band": band, "nebula": nebula,
         "stars": stars, "bigstars": bigstars}.get(_only, final)
if _only:
    unreal.log_warning("RE_SKY: --only=%s (디버그)" % _only)

if not mel.connect_material_property(final, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    unreal.log_error("RE_SKY: 이미시브 연결 실패")
    raise SystemExit(1)

mel.recompile_material(mat)
# only_if_is_dirty=False. MaterialEditingLibrary 로 만든 변경은 패키지를 dirty 로 표시하지
# 않아서, 기본값(True)으로 부르면 "생성 완료" 로그만 찍히고 디스크에는 아무것도 안 써진다.
# 실제로 재생성이 여러 번 조용히 버려졌다(uasset 타임스탬프로 확인).
unreal.EditorAssetLibrary.save_asset(FULL, False)
unreal.log("RE_SKY: 생성 완료 %s" % FULL)
