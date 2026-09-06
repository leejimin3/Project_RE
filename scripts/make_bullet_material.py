"""
M_REBullet 부트스트랩 생성 (#97, 룩 개편 #122).

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 생성 후 에디터에서 수치를 손보면
이 스크립트는 낡는다. 정본은 Content/Materials/M_REBullet.uasset 이다.
재실행하면 수동 튜닝이 날아가므로, 이미 존재하면 만들지 않고 중단한다(--force).

룩: 정통 탄막 슈팅(동방 계열) 규약을 따른다.
    중앙은 흰 코어로 타서 밝고, 그 바깥에 채도 높은 색 링, 맨 가장자리는 어두운 아웃라인.
    아웃라인이 핵심이다 - 탄이 겹쳐도 개체 경계가 살아서 탄막을 '읽을' 수 있다.
    이전 버전은 단색 + 프레넬로 가장자리만 밝히는 구조라 겹치면 한 덩어리로 뭉쳤다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
  선택 인자: --force --core=3.0 --ring=1.6 --sharp=2.2 --rim=1.6 --oastart=0.82 --oasharp=6.0
"""
import unreal


def _arg(name, default):
    """커맨드라인에서 --name=값 을 읽는다. 튜닝 스윕을 리빌드 없이 돌리기 위한 것이다."""
    for tok in unreal.SystemLibrary.get_command_line().split():
        if tok.startswith("--%s=" % name):
            # -ExecutePythonScript="<경로> --rim=1.8" 로 넘기면 마지막 토큰에 닫는 따옴표가
            # 붙어 온다('1.8"'). strip 이 없으면 float() 에서 죽는데, 그 시점엔 이미 기존
            # 에셋을 지운 뒤라 머티리얼이 사라진 채로 중단된다(실제로 한 번 날아갔다).
            return float(tok.split("=", 1)[1].strip("\"'"))
    return default


PKG = "/Game/Materials"
NAME = "M_REBullet"
FULL = PKG + "/" + NAME
CMD = unreal.SystemLibrary.get_command_line()

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    if "--force" not in CMD:
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
# 겹친 탄의 오버드로우가 누적되지 않는다(50,000발 상한을 지탱하는 전제, #50).
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

# ISM 용도 플래그. 없으면 엔진이 조용히 기본 머티리얼로 대체한다 -
#   LogMaterial: Warning: Material ... missing usage flag InstancedStaticMeshes!
# 이때 ISM->GetMaterial(0) 은 여전히 MID 를 돌려주므로 코드상으로는 정상으로 보이고,
# 화면만 라이팅된 회색 구체가 된다. 실제로 그렇게 한 번 속았다.
mat.set_editor_property("used_with_instanced_static_meshes", True)
if not mat.get_editor_property("used_with_instanced_static_meshes"):
    unreal.log_error("RE_MAT: InstancedStaticMeshes 용도 플래그 설정 실패")
    raise SystemExit(1)

mel = unreal.MaterialEditingLibrary


def expr(cls, x, y):
    return mel.create_material_expression(mat, cls, x, y)


def link(frm, frm_out, to, to_in):
    """연결 실패는 조용하다 - 핀 이름이 틀리면 False 만 돌아오고 그래프는 반쯤 빈 채로 저장된다.
    무명 입력 핀(Clamp 등)이 섞여 있어 이름으로 실패하면 무명으로 한 번 더 시도한다."""
    if mel.connect_material_expressions(frm, frm_out, to, to_in):
        return
    if to_in and mel.connect_material_expressions(frm, frm_out, to, ""):
        return
    unreal.log_error("RE_MAT: 연결 실패 %s.%s -> %s.%s" % (
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


# --- 링 색 -----------------------------------------------------------------
# 두 색을 퍼인스턴스 커스텀데이터[1] 로 고른다. 인접 탄이 다른 색이 되어야 겹쳐도 개별
# 오브젝트로 읽힌다 - 탄 간격(30uu) < 지름(50uu) 이라 기하학적으로 겹치는 것을 색으로 가른다.
# 코드가 MID 로 덮어쓰는 파라미터 이름이다(곡사탄=주황, 보스 마커=빨강). 이름을 바꾸면
# REBulletRenderSubsystem 쪽이 조용히 안 먹는다.
col_a = vector("Color", unreal.LinearColor(1.0, 0.10, 0.16, 1.0), -1400, -120)
col_b = vector("ColorB", unreal.LinearColor(0.16, 0.55, 1.0, 1.0), -1400, 40)

sel = expr(unreal.MaterialExpressionPerInstanceCustomData, -1400, 200)
sel.set_editor_property("data_index", 1)
sel.set_editor_property("const_default_value", 0.0)

ring_col = expr(unreal.MaterialExpressionLinearInterpolate, -1140, -20)
link(col_a, "", ring_col, "A")
link(col_b, "", ring_col, "B")
link(sel, "", ring_col, "Alpha")

ring_boost = scalar("RingBoost", _arg("ring", 1.0), -1140, 200)
ring = expr(unreal.MaterialExpressionMultiply, -900, 40)
link(ring_col, "", ring, "A")
link(ring_boost, "", ring, "B")

# --- 프레넬: 중심 0, 실루엣 가장자리 1 --------------------------------------
fr = expr(unreal.MaterialExpressionFresnel, -1400, 420)
rim_pow = scalar("RimPower", _arg("rim", 0.7), -1620, 420)
link(rim_pow, "", fr, "ExponentIn")

# --- 코어 -> 링 ------------------------------------------------------------
# t 를 1 보다 빨리 포화시켜 코어를 또렷한 원반으로 만든다. 그냥 프레넬을 알파로 쓰면
# 중심에서 가장자리로 부드럽게 번져 '흰 점'이 아니라 '흐린 공'이 된다.
sharp = scalar("RingSharpness", _arg("sharp", 3.0), -1140, 480)
t_raw = expr(unreal.MaterialExpressionMultiply, -900, 440)
link(fr, "", t_raw, "A")
link(sharp, "", t_raw, "B")
t = expr(unreal.MaterialExpressionClamp, -700, 440)
link(t_raw, "", t, "Input")

core_boost = scalar("CoreBoost", _arg("core", 2.4), -900, 620)
core_col = vector("CoreColor", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -900, 720)
core = expr(unreal.MaterialExpressionMultiply, -700, 660)
link(core_col, "", core, "A")
link(core_boost, "", core, "B")

body = expr(unreal.MaterialExpressionLinearInterpolate, -460, 300)
link(core, "", body, "A")
link(ring, "", body, "B")
link(t, "", body, "Alpha")

# --- 아웃라인 --------------------------------------------------------------
# 맨 바깥 얇은 띠를 어둡게 눌러 탄끼리 경계를 만든다. 이게 없으면 탄막이 겹칠 때
# 한 덩어리 발광체로 뭉쳐 개수도 궤적도 안 읽힌다(레퍼런스의 핵심).
oa_start = scalar("OutlineStart", _arg("oastart", 0.55), -900, 860)
oa_sharp = scalar("OutlineSharpness", _arg("oasharp", 4.0), -900, 960)
oa_sub = expr(unreal.MaterialExpressionSubtract, -700, 880)
link(fr, "", oa_sub, "A")
link(oa_start, "", oa_sub, "B")
oa_mul = expr(unreal.MaterialExpressionMultiply, -520, 900)
link(oa_sub, "", oa_mul, "A")
link(oa_sharp, "", oa_mul, "B")
oa = expr(unreal.MaterialExpressionClamp, -340, 900)
link(oa_mul, "", oa, "Input")

# 아웃라인을 고정 검정으로 두면 블룸이 덮어 사라진다. 링 색을 그대로 어둡게 낮춘 값이라야
# 색상은 같고 명도만 뚝 떨어져서 경계가 또렷하게 읽힌다 - 레퍼런스(동방 계열)가 그 구조다.
oa_dark = scalar("OutlineDarkness", _arg("oadark", 0.12), -700, 1060)
oa_col = expr(unreal.MaterialExpressionMultiply, -520, 1060)
link(ring_col, "", oa_col, "A")
link(oa_dark, "", oa_col, "B")
shaded = expr(unreal.MaterialExpressionLinearInterpolate, -160, 500)
link(body, "", shaded, "A")
link(oa_col, "", shaded, "B")
link(oa, "", shaded, "Alpha")

# --- 스폰 팝 ---------------------------------------------------------------
# 퍼인스턴스 커스텀데이터 0번. 렌더 프로세서가 0~1 을 채운다.
# ConstDefaultValue 는 인스턴스 데이터가 없을 때의 대체값이다. 1.0 이라야 커스텀데이터가
# 아직 배선되지 않은 상태에서도 탄이 정상 밝기로 보인다(검게 사라지지 않는다).
pop = expr(unreal.MaterialExpressionPerInstanceCustomData, -340, 1200)
pop.set_editor_property("data_index", 0)
pop.set_editor_property("const_default_value", 1.0)

final = expr(unreal.MaterialExpressionMultiply, 40, 700)
link(shaded, "", final, "A")
link(pop, "", final, "B")

if not mel.connect_material_property(final, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    unreal.log_error("RE_MAT: 이미시브 연결 실패")
    raise SystemExit(1)

mel.recompile_material(mat)
# only_if_is_dirty=False. MaterialEditingLibrary 로 만든 변경은 패키지를 dirty 로
# 표시하지 않아서, 기본값(True)으로 부르면 "생성 완료" 로그만 찍히고 디스크에는
# 아무것도 안 써진다. 실제로 재생성이 여러 번 조용히 버려졌다(uasset 타임스탬프로 확인).
unreal.EditorAssetLibrary.save_asset(FULL, False)
unreal.log("RE_MAT: 생성 완료 %s" % FULL)
