"""
M_REBeam 부트스트랩 생성 (#120).

히트스캔 사격의 표시용 빔 + 총구 섬광이 공유하는 머티리얼이다.

왜 Niagara 가 아닌가:
    엔진에 빔 Niagara **시스템**이 없다 — 모듈(`Niagara/Content/Modules/Beams/`)과
    이미터 템플릿(`StaticBeam`/`DynamicBeam`)뿐이라 복제할 대상이 없고, 시스템을
    파이썬으로 조립하는 경로는 막혀 있다(`EmitterHandles` 가 protected, #119).
    반면 머티리얼 저작은 이 리포에 이미 굴러가는 경로다(make_bullet_material.py).
    빔은 '머즐에서 피격점까지 늘린 실린더' 하나면 되므로 메시+머티리얼로 끝난다.

룩: 언릿 + Translucent(불투명도 1). 색은 탄막(빨강/흰색)과 보스(회색/청록/주황)
    양쪽에서 떨어지는 따뜻한 노랑 — 플레이어 것임이 한눈에 읽혀야 한다.

    두 번 되돌린 이력이 있다. (1) Additive 는 아레나 바닥이 밝은 라벤더라 흰색으로
    포화돼 노란 빔이 흰 실선이 됐다. (2) Strength 를 6 으로 두니 Translucent 에서도
    톤매퍼를 넘겨 다시 흰색이 됐다 — 색을 남기려면 1 근처여야 한다.

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 생성 후 에디터에서 손보면 낡는다.
정본은 Content/Materials/M_REBeam.uasset 이다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
  선택 인자: --force --strength=1.6
"""
import unreal


def _arg(name, default):
    """커맨드라인에서 --name=값 을 읽는다. 마지막 토큰에는 닫는 따옴표가 붙어 오므로 벗긴다."""
    for tok in unreal.SystemLibrary.get_command_line().split():
        if tok.startswith("--%s=" % name):
            return float(tok.split("=", 1)[1].strip("\"'"))
    return default


PKG = "/Game/Materials"
NAME = "M_REBeam"
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

mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
# 깊이 테스트는 켠 채로 둔다. 껐더니 빔이 캐릭터 몸통 위에 덧그려져 플레이어를 가렸다.
# 빔은 머즐(Z≈154)에서 시작해 바닥(윗면 Z 40) 위를 지나므로 잘릴 구간이 없다.
# 실린더 안쪽에서 카메라가 들어오는 각이 생긴다(머즐이 카메라 쪽을 향할 때).
mat.set_editor_property("two_sided", True)

mel = unreal.MaterialEditingLibrary

color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -600, -80)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.85, 0.40, 1.0))

strength = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 120)
strength.set_editor_property("parameter_name", "Strength")
strength.set_editor_property("default_value", _arg("strength", 1.6))

emis = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -320, 0)
if not mel.connect_material_expressions(color, "", emis, "A"):
    unreal.log_error("RE_MAT: Color -> Multiply.A 연결 실패")
    raise SystemExit(1)
if not mel.connect_material_expressions(strength, "", emis, "B"):
    unreal.log_error("RE_MAT: Strength -> Multiply.B 연결 실패")
    raise SystemExit(1)

if not mel.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    unreal.log_error("RE_MAT: 이미시브 연결 실패")
    raise SystemExit(1)

# Translucent 는 Opacity 를 연결하지 않으면 기본 0(완전 투명)으로 컴파일돼 화면에서 사라진다.
opacity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -600, 300)
opacity.set_editor_property("parameter_name", "Opacity")
opacity.set_editor_property("default_value", 1.0)
if not mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
    unreal.log_error("RE_MAT: 오퍼시티 연결 실패")
    raise SystemExit(1)

mel.recompile_material(mat)
# only_if_is_dirty=False. MaterialEditingLibrary 변경은 패키지를 dirty 로 세우지 않아
# 기본값으로 부르면 로그만 찍히고 디스크에는 아무것도 안 써진다.
unreal.EditorAssetLibrary.save_asset(FULL, False)
unreal.log("RE_MAT: 생성 완료 %s" % FULL)
