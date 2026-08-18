"""
Main 레벨 라이팅/배경 셋업 (#122).

이 스크립트는 '정본'이 아니라 '초안 생성기'다. Main.umap 이 정본이다.
다만 레벨은 바이너리라 diff 가 안 되므로, 어떤 값을 왜 넣었는지는 여기에만 남는다.
수치를 에디터에서 손봤다면 이 파일의 기본값도 같이 고쳐라 - 안 그러면 재실행이 되돌린다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" \
      -unattended -nosplash -nop4 [--sky=pack|engine|none] [--hue_r=0.72 ...]

Ithris Cemetery 의존:
  바닥/하늘 머티리얼의 부모가 Content/IthrisCemetery/ 에 있고 이 폴더는 gitignore 대상이다
  (유료, 재배포 불가). 팩이 없는 환경에서는 --sky=engine 으로 돌리고 바닥은 폴백을 쓴다.
"""
import unreal


def _sarg(name, default):
    """커맨드라인에서 --name=값 을 읽는다. 색 스윕을 리빌드 없이 돌리기 위한 것이다.

    -ExecutePythonScript="<경로> --sky=pack" 형태로 넘기면 커맨드라인 토큰 끝에 닫는
    따옴표가 그대로 붙어 온다(pack" 로 읽힌다). strip 이 없으면 조용히 기본값으로 흘러간다.
    """
    for tok in unreal.SystemLibrary.get_command_line().split():
        if tok.startswith("--%s=" % name):
            return tok.split("=", 1)[1].strip('"\'')
    return default


def _arg(name, default):
    v = _sarg(name, None)
    return default if v is None else float(v)


LEVEL = "/Game/Level/Main"
PKG = "/Game/Materials"

# 팩 경로. 없으면 각 단계가 스스로 건너뛴다.
# 바닥은 자작 M_ArenaFloor 가 기본이다(scripts/make_arena_floor.py 로 먼저 만들어라).
# 팩 타일러블을 먼저 시도했고 둘 다 탈락했다 - 이유는 make_arena_floor.py 헤더에 적어뒀다.
# 비교용으로 남겨두는 팩 경로:
#   /Game/IthrisCemetery/Materials/Tilable/StoneSurface_01/MI_StoneSurface_01
#   /Game/IthrisCemetery/Materials/Tilable/StoneTiles/MI_Tiles_Concrete_Painted_01a
FLOOR_PARENT = _sarg("floor", "/Game/Materials/M_ArenaFloor")

SKY_MODE = _sarg("sky", "pack")

# --- 색 기준 ---------------------------------------------------------------
# 아브렐슈드 6관 레퍼런스: 흰~회색 바닥에 배경 보라가 스며들고, 조명은 어두운 보라.
# 탄 대비가 최우선 제약이다(#122 완료조건). 곡사탄은 주황(1,0.5,0) 이라 보라와 보색이라
# 색상 대비는 자동으로 확보된다 - 위험한 쪽은 명도다. 바닥이 밝으면 주황이 묻힌다.
# 그래서 알베도를 흰색(1.0)이 아니라 0.6 대 회색으로 두고, 밝기는 조명/노출에서 잡는다.
# 이 값은 MI 오버라이드라 M_ArenaFloor 의 기본값을 덮는다. 어두운 보라(0.60/0.57/0.66)를
# 팩 알베도에 곱하고 있었던 게 '바닥이 빛을 반사하는 게 아니라 스스로 보라색'이던 진짜
# 원인이다(실측). 중성 + 1.0 초과로 두고, 색은 조명에서만 오게 한다.
FLOOR_HUE = unreal.LinearColor(_arg("hue_r", 1.55), _arg("hue_g", 1.52), _arg("hue_b", 1.58), 1.0)
# 팩 타일러블을 --floor 로 쓸 때만 의미가 있다. 자작 M_ArenaFloor 에는 Tiling 파라미터가 없다.
FLOOR_TILING = _arg("tiling", 10.0)
# 거칠기. 내리면 Lumen 반사로 하늘 보라가 바닥에 비쳐 광택이 생긴다. 다만 반사 하이라이트가
# 탄과 밝기 경쟁을 하므로 0.2 아래로는 내리지 않는 편이 안전하다.
FLOOR_ROUGHNESS = _arg("rough", 0.55)

# 키라이트는 '거의 흰색, 살짝 차갑게'다. 여기에 보라를 넣으면 화면이 한 톤으로 붕괴한다
# (실측 - 보라 키라이트 + 보라 알베도 = 단색 판). 색은 필라이트와 랜턴이 만든다.
SUN_COLOR = unreal.LinearColor(_arg("sun_r", 0.86), _arg("sun_g", 0.91), _arg("sun_b", 1.0), 1.0)
SUN_INTENSITY = _arg("sun_i", 8.0)
# 반대편에서 넣는 마젠타 필. 실루엣 반대쪽 가장자리를 물들여 입체감과 '몽환적' 톤을 만든다.
# 키(차가움) 대 필(마젠타)의 색 대비가 단색을 깨는 핵심이다.
FILL_COLOR = unreal.LinearColor(_arg("fill_r", 0.62), _arg("fill_g", 0.18), _arg("fill_b", 0.55), 1.0)
FILL_INTENSITY = _arg("fill_i", 3.0)
FILL_PITCH = _arg("fill_pitch", -22.0)
FILL_YAW = _arg("fill_yaw", 150.0)
# 볼류메트릭 안개 - 빛줄기가 생겨야 '우주/몽환'이 된다. 평면 안개만으로는 색만 덮인다.
VOLUMETRIC = _arg("volumetric", 1.0) > 0.5
VOL_SCATTER = _arg("vol_scatter", 2.5)
SKYLIGHT_INTENSITY = _arg("skylight_i", 1.5)
SKYLIGHT_CAPTURE = _arg("skylight_capture", 0.0) > 0.5
# 자동노출 고정값. 프로젝트가 r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=True 라
# 이 필드는 밝기 배수가 아니라 EV100 이다. 기존 씬이 '어두운데도 밝게' 보였던 건 자동노출이
# 매 프레임 보정하고 있었기 때문이고, 고정하는 순간 조명 세기를 실제로 맞춰야 한다.
EXPOSURE_EV = _arg("exposure", 0.0)

FOG_DENSITY = _arg("fog_d", 0.02)
FOG_COLOR = unreal.LinearColor(_arg("fog_r", 0.045), _arg("fog_g", 0.055), _arg("fog_b", 0.105), 1.0)
FOG_MAX_OPACITY = _arg("fog_maxop", 0.55)

# 지평선(아래)에서 천정(위)으로 가는 그라디언트. 지평선을 밝게 두면 바닥 끝 안개와 이어져
# '공허에 떠 있는 아레나'로 읽힌다.
SKY_COLOR = unreal.LinearColor(_arg("sky_r", 0.07), _arg("sky_g", 0.09), _arg("sky_b", 0.17), 1.0)
ZENITH_COLOR = unreal.LinearColor(_arg("zen_r", 0.010), _arg("zen_g", 0.012), _arg("zen_b", 0.030), 1.0)
CLOUD_COLOR = unreal.LinearColor(_arg("cloud_r", 0.16), _arg("cloud_g", 0.17), _arg("cloud_b", 0.28), 1.0)
STAR_BRIGHTNESS = _arg("star", 1.4)
CLOUD_OPACITY = _arg("cloud_op", 0.6)
# 구 반경(uu). 바닥이 4000uu 라 그보다 충분히 크면 되고, 안개 거리와만 어긋나지 않으면 된다.
# 백드롭 평면의 반너비(uu). 화면을 확실히 덮을 만큼 크면 된다.
SKY_RADIUS = _arg("sky_radius", 20000.0)

L = unreal.log
LE = unreal.log_error
LW = unreal.log_warning

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def derive_mi(name, parent_path):
    """부모 MI 에서 파생 인스턴스를 만든다. 이미 있으면 재사용한다(수동 튜닝 보존)."""
    full = PKG + "/" + name
    if not eal.does_asset_exist(parent_path):
        LW("RE_LVL: 부모 없음, 건너뜀: %s" % parent_path)
        return None
    if eal.does_asset_exist(full):
        mi = unreal.load_asset(full)
    else:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mi = tools.create_asset(name, PKG, unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            LE("RE_LVL: MI 생성 실패 %s" % full)
            raise SystemExit(1)
    # 부모는 매번 다시 건다 - --floor 로 후보를 갈아끼울 때 기존 인스턴스가 옛 부모에
    # 붙은 채 남으면 스윕 결과가 조용히 이전 값이 된다.
    mel.set_material_instance_parent(mi, unreal.load_asset(parent_path))
    return mi


def _try_set(comp, names, value):
    """프로퍼티 이름이 엔진 버전마다 다르다(volumetric_fog / enable_volumetric_fog).
    없는 이름에 쓰면 예외라 스크립트가 통째로 죽으므로 후보를 돌린다."""
    for n in names:
        try:
            comp.set_editor_property(n, value)
            return n
        except Exception:
            continue
    LW("RE_LVL: 프로퍼티 못 찾음 %s" % (names,))
    return None


def save_mi(mi, path):
    """MaterialEditingLibrary 세터는 에셋을 dirty 로 표시하지 않는다. 그런데
    EditorAssetLibrary.save_asset() 의 기본값이 only_if_is_dirty=True 라, 그냥 부르면
    조용히 아무것도 저장되지 않고 머티리얼 기본값이 그대로 남는다.
    하늘 색이 아무리 바꿔도 안 변하던 원인이 이것이었다(실측 - MI 에 직전 실행 값이 그대로
    박혀 있었고, 이미 지워진 파라미터 이름까지 남아 있었다)."""
    mel.update_material_instance(mi)
    eal.save_asset(path, False)


les.load_level(LEVEL)
actors = eas.get_all_level_actors()
L("RE_LVL: 레벨 로드 %s (actors=%d)" % (LEVEL, len(actors)))


def by_class(cn):
    return [a for a in actors if a.get_class().get_name() == cn]


# --- 1. 중복 DirectionalLight 제거 -----------------------------------------
# Main 에는 위치/회전/강도/색이 완전히 동일한 DirectionalLight 가 2개 있었다.
# 광량이 그대로 2배가 되어 캐릭터가 흰색으로 날아가던 원인 중 하나다.
# 필라이트는 우리가 만든 것이라 '중복'이 아니다. 라벨로 갈라내지 않으면 재실행할 때마다
# 자기가 만든 필라이트를 자기가 지운다.
FILL_LABEL = "RE_FillLight"
for old_fill in by_class("DirectionalLight"):
    if old_fill.get_actor_label() == FILL_LABEL:
        eas.destroy_actor(old_fill)
suns = [a for a in eas.get_all_level_actors()
        if a.get_class().get_name() == "DirectionalLight"
        and a.get_actor_label() != FILL_LABEL]
for extra in suns[1:]:
    L("RE_LVL: 중복 DirectionalLight 삭제 %s" % extra.get_actor_label())
    eas.destroy_actor(extra)
sun = suns[0] if suns else None

if sun:
    # 원래 태양의 회전은 pitch=0, yaw=-30, roll=-46 이었다. pitch 0 은 빛을 수평으로 쏜다는 뜻이라
    # 법선이 위인 바닥에는 cos(90도)=0, 즉 직접광이 하나도 안 들어간다. 캐릭터(수직면)만 밝고
    # 바닥이 새까맣던 진짜 원인이 이것이었다 - 알베도도 노출도 아니었다.
    sun.set_actor_rotation(unreal.Rotator(_arg("sun_roll", 0.0),
                                          _arg("sun_pitch", -48.0),
                                          _arg("sun_yaw", -35.0)), False)
    lc = sun.get_components_by_class(unreal.LightComponent)[0]
    # 모빌리티가 Static/Stationary 면 라이트맵을 구워야 직접광이 나온다. 이 프로젝트는
    # r.AllowStaticLighting=False(Lumen 전제)이고 라이팅을 빌드한 적이 없어서, 바닥(스태틱 메시)에
    # 직접광이 통째로 빠지고 캐릭터 주변만 Lumen 바운스로 둥글게 밝던 원인이 이거였다(실측).
    lc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    lc.set_editor_property("intensity", SUN_INTENSITY)
    lc.set_editor_property("light_color", SUN_COLOR.to_rgbe())
    lc.set_editor_property("volumetric_scattering_intensity", VOL_SCATTER)
    # 디렉셔널 라이트가 2개 이상이면 엔진이 "어느 쪽을 포워드 셰이딩/볼류메트릭용
    # 단일 라이트로 쓸지 못 고르겠다"며 게임 화면에 노란 경고를 그린다:
    #   "Multiple directional lights are competing to be the single one used for
    #    forward shading, translucent, water or volumetric fog. Please adjust their
    #    ForwardShadingPriority."
    # 우선순위를 명시하면 사라진다. 키라이트가 이긴다.
    _try_set(lc, ["forward_shading_priority"], 10)
    L("RE_LVL: 태양 intensity=%.2f color=%s" % (SUN_INTENSITY, SUN_COLOR))

# 필라이트
fill = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000))
fill.set_actor_label(FILL_LABEL)
fill.set_actor_rotation(unreal.Rotator(0.0, FILL_PITCH, FILL_YAW), False)
flc = fill.get_components_by_class(unreal.LightComponent)[0]
flc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
flc.set_editor_property("intensity", FILL_INTENSITY)
flc.set_editor_property("light_color", FILL_COLOR.to_rgbe())
flc.set_editor_property("volumetric_scattering_intensity", VOL_SCATTER)
_try_set(flc, ["forward_shading_priority"], 0)
# 필라이트는 그림자를 안 친다. 키라이트 그림자와 교차하면 방향이 안 읽히고 비용만 든다.
flc.set_editor_property("cast_shadows", False)
L("RE_LVL: 필라이트 intensity=%.2f color=%s" % (FILL_INTENSITY, FILL_COLOR))

for sl in by_class("SkyLight"):
    slc = sl.get_components_by_class(unreal.SkyLightComponent)[0]
    slc.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    slc.set_editor_property("intensity", SKYLIGHT_INTENSITY)
    # 실시간 캡처는 하늘 커버리지를 검증하고, 부족하면 게임 화면에 대문짝만한 경고를 그린다
    # ("YOUR SCENE CONTAINS A SKYDOME MESH ... BUT IT DOES NOT COVER THAT PART OF THE SCREEN").
    # 포트폴리오 영상에 그대로 찍히므로 기본은 꺼둔다. 앰비언트는 intensity 로 맞춘다.
    slc.set_editor_property("real_time_capture", SKYLIGHT_CAPTURE)
    L("RE_LVL: SkyLight intensity=%.2f realtime_capture=%s" % (SKYLIGHT_INTENSITY, SKYLIGHT_CAPTURE))


# --- 2. 바닥 머티리얼 -------------------------------------------------------
# 화면의 거의 전부가 바닥이다(카메라 pitch -50, arm 1500). 배경색 = 바닥색이므로
# 여기가 #122 의 실질적인 유일한 레버다.
floor_mi = derive_mi("MI_ArenaFloor", FLOOR_PARENT)
if floor_mi:
    # 자작 M_ArenaFloor 는 BaseColor/Roughness, 팩 타일러블은 HUE/Tiling 을 쓴다.
    # 없는 파라미터에 쓰면 조용히 무시되므로 --floor 로 후보를 갈아끼워도 둘 다 그냥 넘긴다.
    mel.set_material_instance_vector_parameter_value(floor_mi, "BaseColor", FLOOR_HUE)
    mel.set_material_instance_vector_parameter_value(floor_mi, "HUE", FLOOR_HUE)
    mel.set_material_instance_scalar_parameter_value(floor_mi, "Tiling", FLOOR_TILING)
    mel.set_material_instance_scalar_parameter_value(floor_mi, "Roughness", FLOOR_ROUGHNESS)
    save_mi(floor_mi, PKG + "/MI_ArenaFloor")

    applied = 0
    for a in by_class("StaticMeshActor"):
        for c in a.get_components_by_class(unreal.StaticMeshComponent):
            sm = c.static_mesh
            if sm and "BasicShapes/Cube" in sm.get_path_name():
                c.set_material(0, floor_mi)
                applied += 1
    if applied == 0:
        LE("RE_LVL: 바닥(BasicShapes/Cube)을 못 찾았다 - WorldGridMaterial 이 그대로 남는다")
        raise SystemExit(1)
    L("RE_LVL: 바닥 머티리얼 적용 %d개 hue=%s tiling=%.1f" % (applied, FLOOR_HUE, FLOOR_TILING))


# --- 3. ExponentialHeightFog ------------------------------------------------
# 바닥이 4000uu 에서 끊기고 그 너머는 공허다(가장자리에서 화면 상단 47% 가 공허).
# 안개가 바닥 끝을 하늘색으로 녹여 그 경계를 지운다 - '공허에 떠 있는 아레나'가 여기서 나온다.
fogs = by_class("ExponentialHeightFog")
fog = fogs[0] if fogs else eas.spawn_actor_from_class(
    unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
fc = fog.get_components_by_class(unreal.ExponentialHeightFogComponent)[0]
# --fog=0 이면 안개 액터를 통째로 숨긴다. 배경이 안개인지 하늘인지 가르는 유일한 방법이다
# (밀도 0 으로 낮춰도 볼류메트릭이 남아 화면을 덮는다).
fog.set_actor_hidden_in_game(_arg("fog", 1.0) <= 0.5)
fc.set_editor_property("fog_density", FOG_DENSITY)
fc.set_editor_property("fog_height_falloff", 0.02)
fc.set_editor_property("fog_inscattering_luminance", FOG_COLOR)
# 상한을 1.0 으로 두면 5만uu 떨어진 하늘 구가 통째로 안개색으로 치환된다. 하늘이 균일한
# 보라 한 장으로 보이던 게 사실 전부 안개였다(실측 - 안개를 끄니 하늘이 검게 드러났다).
fc.set_editor_property("fog_max_opacity", FOG_MAX_OPACITY)
# 볼류메트릭을 켜야 빛이 공기 중에 보인다. 이게 없으면 안개는 '색 덮개'일 뿐이다.
_vf = _try_set(fc, ["volumetric_fog", "enable_volumetric_fog", "b_enable_volumetric_fog"], VOLUMETRIC)
_try_set(fc, ["volumetric_fog_scattering_distribution"], 0.4)
_try_set(fc, ["volumetric_fog_extinction_scale"], 1.2)
L("RE_LVL: 볼류메트릭 안개 %s (프로퍼티=%s)" % (VOLUMETRIC, _vf))
L("RE_LVL: Fog density=%.4f color=%s" % (FOG_DENSITY, FOG_COLOR))


# --- 4. PostProcessVolume - 자동노출 고정 -----------------------------------
# 지금은 EyeAdaptation 이 켜져 있어 화면 밝기가 탄 개수에 따라 널뛴다. 그래서 스크린샷마다
# 'showflag.EyeAdaptation 0' 을 붙여야 색이 보였다. min=max 로 잠그면 그 우회가 사라지고,
# 무엇보다 탄 대비가 프레임마다 흔들리지 않는다(#122 완료조건이 대비 판정이다).
ppvs = by_class("PostProcessVolume")
ppv = ppvs[0] if ppvs else eas.spawn_actor_from_class(
    unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
ppv.set_editor_property("unbound", True)
s = ppv.get_editor_property("settings")
s.set_editor_property("override_auto_exposure_min_brightness", True)
s.set_editor_property("auto_exposure_min_brightness", EXPOSURE_EV)
s.set_editor_property("override_auto_exposure_max_brightness", True)
s.set_editor_property("auto_exposure_max_brightness", EXPOSURE_EV)
ppv.set_editor_property("settings", s)
L("RE_LVL: PostProcessVolume unbound, 자동노출 EV=%.2f 고정" % EXPOSURE_EV)


# --- 5. 하늘 ----------------------------------------------------------------
# 배경 백드롭은 setup_level_props.py 로 옮겼다. 이 스크립트가 스폰한 StaticMeshActor 는
# 화면에 한 번도 안 그려졌는데(구/평면, 크기 250~5만, 안팎 반전, IsSky on/off 전부 시도)
# 프롭 스크립트가 같은 API 로 스폰한 메시 83개는 정상 렌더된다. 원인 미규명이라
# 되는 쪽으로 옮긴다. 여기서는 옛 하늘 액터 정리만 한다.
for old in by_class("StaticMeshActor"):
    if old.get_actor_label().startswith("RE_Sky"):
        eas.destroy_actor(old)
for old in actors:
    if old.get_class().get_name() == "BP_Sky_Sphere_C":
        eas.destroy_actor(old)
L("RE_LVL: 옛 하늘 액터 정리 완료 (백드롭은 setup_level_props.py 담당)")


les.save_current_level()
L("RE_LVL: 저장 완료")
