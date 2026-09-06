"""
Main 레벨 아레나 프롭 배치 (#122).

라이팅/배경은 setup_level_lighting.py 가, 지오메트리는 이 파일이 담당한다. 나눠둔 이유는
프롭만 반복해서 갈아끼울 때 라이팅 튜닝을 다시 안 밟기 위해서다.

원칙:
  - 배치한 액터는 전부 라벨이 RE_Prop_ 로 시작한다. 재실행하면 먼저 싹 지우고 다시 놓는다
    (레벨이 바이너리라 '어디까지 내가 놓은 것인지'를 라벨 말고는 알 방법이 없다)
  - 전부 NoCollision 이다. 프롭이 캡슐을 밀면 SimProbe 의 이동/DEFEAT 판정 전제가 깨진다
    (#54/#55/#56/#57 이력). 장식이 게임플레이를 건드리면 안 된다
  - 고정 시드. 스크린샷 회귀 비교가 가능해야 한다
  - 전투 구역을 비운다. 보스는 (600,0,90), 플레이어는 원점이라 반경 CLEAR_RADIUS 안쪽에는
    아무것도 놓지 않는다

팩 의존:
  전부 Content/IthrisCemetery/ (gitignore, 유료). 포트폴리오 영상 산출이 목적이라 허용한
  판단이다. 팩이 없으면 이 스크립트는 경고만 남기고 해당 프롭을 건너뛴다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" \
      -unattended -nosplash -nop4 [--clear=1] [--moon_x=20000 ...]
"""
import math
import random

import unreal


def _sarg(name, default):
    for tok in unreal.SystemLibrary.get_command_line().split():
        if tok.startswith("--%s=" % name):
            return tok.split("=", 1)[1].strip('"\'')
    return default


def _arg(name, default):
    v = _sarg(name, None)
    return default if v is None else float(v)


LEVEL = "/Game/Level/Main"
PACK = "/Game/IthrisCemetery"
TAG = "RE_Prop_"

HALF = _arg("half", 1950.0)       # 바닥 반너비. 바닥 Cube 는 4000uu 라 ±2000 이 끝이다
CLEAR_RADIUS = _arg("clear", 1650.0)   # 이 안쪽은 전투 구역 - 비운다
# 구조물을 놓는 반경. 바닥(±2000) 바깥의 공허다.
#
# 왜 바닥 위가 아니라 공허인가:
#   Mass 탄은 월드 콜리전이 없다(50,000발 전제의 설계). 그래서 바닥 위에 뭘 놓으면
#   탄이 그대로 통과해 "장식을 붙인 티"만 난다. 탄 사거리는 Config/DefaultGame.ini 의
#   BulletSpeed 200 * BulletLifetime 15 = 3000uu 이고, 보스가 (600,0,90) 이므로
#   원점 기준 최대 약 3600uu 까지 날아간다. 그 바깥에 두면 물리적으로 겹칠 수 없다.
#   덤으로 콜리전도 네비메시 간섭도 우클릭 트레이스 간섭도 전부 사라진다.
VOID_RADIUS = _arg("void_r", 4400.0)
SEED = int(_arg("seed", 7))
# 프롭 밀도 계수. 개수는 곧 드로우콜이고 드로우콜은 곧 GT 시간이다.
# 50,000발 게이트(p99 <= 16.6ms)에서 프롭 94개는 드로우콜 96 -> 222 로 늘려
# p99 를 15.00 -> 16.98 로 밀어냈다(실측). 밀도로 되돌린다.
# 실측(50,000발, 게이트 p99 <= 16.6ms):
#   1.00 (프롭 94) -> p99 16.98  초과
#   0.75 (프롭 71) -> p99 16.64  경계 초과
#   0.50 (프롭 48) -> p99 14.60  통과 (기준선 15.00 보다도 낮다)
# 점광 8개만 따로 끄면 0.37ms 회수된다 - 나머지는 드로우콜(96 -> 222)이 만든 GT 비용이다.
DENSITY = _arg("density", 0.5)


def _n(count):
    """개수에 밀도를 곱한다. 최소 2개는 남겨 구도가 무너지지 않게 한다."""
    return max(2, int(round(count * DENSITY)))

L = unreal.log
LW = unreal.log_warning
LE = unreal.log_error

eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

les.load_level(LEVEL)

# --- 기존 프롭 제거 ---------------------------------------------------------
removed = 0
for a in eas.get_all_level_actors():
    if a.get_actor_label().startswith(TAG):
        eas.destroy_actor(a)
        removed += 1
L("RE_PROP: 기존 프롭 제거 %d개" % removed)

# --- 바닥 윗면 높이 실측 ----------------------------------------------------
# 하드코딩하면 바닥 스케일이 바뀔 때 프롭이 공중에 뜨거나 묻힌다.
FLOOR_Z = 40.0
for a in eas.get_all_level_actors():
    for c in a.get_components_by_class(unreal.StaticMeshComponent):
        sm = c.static_mesh
        if sm and "BasicShapes/Cube" in sm.get_path_name():
            org, ext = a.get_actor_bounds(False)
            FLOOR_Z = org.z + ext.z
L("RE_PROP: 바닥 윗면 Z=%.1f" % FLOOR_Z)

rnd = random.Random(SEED)
_placed = [0]
_missing = set()


def mesh(path):
    m = unreal.load_asset(path)
    if m is None and path not in _missing:
        _missing.add(path)
        LW("RE_PROP: 메시 없음, 건너뜀 %s" % path)
    return m


def place(sm, loc, yaw=0.0, scale=1.0, mat=None, pitch=0.0, roll=0.0, collide=False):
    if sm is None:
        return None
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc)
    _placed[0] += 1
    a.set_actor_label("%s%03d" % (TAG, _placed[0]))
    a.set_actor_rotation(unreal.Rotator(roll, pitch, yaw), False)
    a.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    c = a.static_mesh_component
    c.set_editor_property("static_mesh", sm)
    if mat is not None:
        c.set_material(0, mat)
    # 콜리전 채널을 골라서 막는다. BlockAll 을 주면 ECC_Visibility 까지 막는데,
    # 우클릭 이동이 GetHitResultUnderCursor(ECC_Visibility) 라서 클릭이 바닥 대신
    # 구조물에 걸린다(REPlayerController.cpp:283/308/371). 실제로 그 버그가 났다.
    # 그래서 폰만 막고 시야/카메라 트레이스는 통과시킨다.
    #
    # Mass 탄은 애초에 월드 콜리전이 없다(50,000발 전제). 다만 탄 사거리는
    # BulletSpeed 300 * BulletLifetime 3s = 900uu 라 CLEAR_RADIUS(1650) 밖 구조물에는
    # 정상 플레이에서 닿지 않는다. 스트레스 cvar 로 넓게 뿌릴 때만 겹쳐 보인다.
    # 엔진 기본 프로파일 InvisibleWall 을 쓴다: 전부 막되 ECC_Visibility 만 통과시킨다.
    # BlockAll 을 주면 Visibility 까지 막는데, 우클릭 이동이
    # GetHitResultUnderCursor(ECC_Visibility) 라서 클릭이 바닥 대신 구조물에 걸린다
    # (REPlayerController.cpp:283/308/371). 실제로 그 버그가 났다.
    # 열거형으로 채널을 직접 조합하는 방법도 있으나 UE 파이썬 바인딩의 열거형 이름이
    # 버전마다 달라(CollisionResponse.ECR_IGNORE / .IGNORE 둘 다 없었다) 프로파일이 안전하다.
    #
    # Mass 탄은 애초에 월드 콜리전이 없다(50,000발 전제). 다만 탄 사거리는
    # BulletSpeed 300 * BulletLifetime 3s = 900uu 라 CLEAR_RADIUS(1650) 밖 구조물에는
    # 정상 플레이에서 닿지 않는다. 스트레스 cvar 로 넓게 뿌릴 때만 겹쳐 보인다.
    c.set_collision_profile_name("InvisibleWall" if collide else "NoCollision")
    return a


def point_light(loc, color, intensity, radius):
    """따뜻한 점광. 씬이 보라 일색이라 색 대비를 만들 유일한 수단이다.
    개수는 최소로 유지한다 - 동적 점광은 Lumen 에서 싸지 않고, 이 프로젝트의 병목은 GT 다(#50)."""
    a = eas.spawn_actor_from_class(unreal.PointLight, loc)
    _placed[0] += 1
    a.set_actor_label("%s%03d_Light" % (TAG, _placed[0]))
    c = a.point_light_component
    c.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
    c.set_editor_property("intensity", intensity)
    c.set_editor_property("light_color", color.to_rgbe())
    c.set_editor_property("attenuation_radius", radius)
    # 그림자를 끈다. 8개 점광이 각자 그림자를 치면 프레임 예산(15.00ms)을 그냥 먹는다.
    c.set_editor_property("cast_shadows", False)
    return a


def ring(sm, count, radius, z, scale=1.0, yaw_offset=180.0, jitter=0.0, start_deg=0.0,
         collide=False, z_jitter=0.0, tumble=0.0, ang_jitter=0.0):
    """원형 배치. 아레나 경계를 만드는 가장 싼 방법이다 - 각도 루프 한 줄이면 된다.

    yaw_offset 은 메시의 '정면'이 로컬 어느 축이냐에 따라 다르다. 바운즈로 확인해야 한다:
      +X 가 정면(대부분의 프롭)        -> 180 (안쪽을 본다)
      +Y 로 몸통이 뻗는다(버트레스)    -> -90 (몸통이 바깥을 향한다)
    """
    for i in range(count):
        ang = math.radians(start_deg + 360.0 * i / count
                           + (rnd.uniform(-ang_jitter, ang_jitter) if ang_jitter else 0.0))
        r = radius + (rnd.uniform(-jitter, jitter) if jitter else 0.0)
        x, y = r * math.cos(ang), r * math.sin(ang)
        zz = z + (rnd.uniform(-z_jitter, z_jitter) if z_jitter else 0.0)
        # 우주에 떠 있는 잔해라 '똑바로 선' 상태가 오히려 어색하다. 3축을 흔든다.
        place(sm, unreal.Vector(x, y, zz),
              yaw=math.degrees(ang) + yaw_offset + (rnd.uniform(-60, 60) if tumble else 0.0),
              pitch=rnd.uniform(-tumble, tumble) if tumble else 0.0,
              roll=rnd.uniform(-tumble, tumble) if tumble else 0.0,
              scale=scale * (rnd.uniform(0.7, 1.4) if tumble else 1.0), collide=collide)


def edge_line(sm, z, inset, scale=1.0, collide=False):
    """바닥 네 변을 따라 메시를 이어 붙인다.

    두 가지를 바운즈에서 읽어야 한다 - 둘 다 하드코딩하면 조용히 어긋난다:
      1) 길이축과 길이. SM_Wall_02_Straight_* 는 길이축이 +X 다
      2) 피벗 위치. 이 벽은 피벗이 '한쪽 끝'이라(org.x == ext.x) 중심 기준으로 놓으면
         반 칸씩 밀린다. 그래서 코너에서 코너로 걸으며 시작점마다 놓는다
    """
    if sm is None:
        return
    b = sm.get_bounds()
    step = b.box_extent.x * 2.0 * scale
    if step <= 1.0:
        LW("RE_PROP: 바운즈가 0 이라 변 배치를 건너뛴다")
        return
    h = HALF - inset
    corners = [(h, -h), (h, h), (-h, h), (-h, -h)]
    for i in range(4):
        sx, sy = corners[i]
        ex, ey = corners[(i + 1) % 4]
        dx, dy = ex - sx, ey - sy
        length = math.hypot(dx, dy)
        ux, uy = dx / length, dy / length
        yaw = math.degrees(math.atan2(uy, ux))   # 로컬 +X 를 변 방향에 맞춘다
        n = int(length // step)
        for j in range(n):
            d = step * j
            place(sm, unreal.Vector(sx + ux * d, sy + uy * d, z), yaw=yaw, scale=scale, collide=collide)


def scatter(paths, count, r_min, r_max, z, s_min=1.0, s_max=1.0, collide=False,
            z_jitter=0.0, tumble=0.0):
    """전투 구역 밖에만 흩뿌린다."""
    meshes = [m for m in (mesh(p) for p in paths) if m is not None]
    if not meshes:
        return
    for _ in range(count):
        ang = rnd.uniform(0, 2 * math.pi)
        r = rnd.uniform(r_min, r_max)
        place(rnd.choice(meshes),
              unreal.Vector(r * math.cos(ang), r * math.sin(ang),
                            z + (rnd.uniform(-z_jitter, z_jitter) if z_jitter else 0.0)),
              yaw=rnd.uniform(0, 360),
              pitch=rnd.uniform(-tumble, tumble) if tumble else 0.0,
              roll=rnd.uniform(-tumble, tumble) if tumble else 0.0,
              scale=rnd.uniform(s_min, s_max), collide=collide)


# --props=0 이면 장식을 하나도 놓지 않는다(기존 것은 위에서 이미 지웠다).
# 탄이 구조물을 통과하는 게 거슬리면 통째로 비우는 쪽이 낫다는 판단을 위한 스위치다.
if _arg("props", 1.0) <= 0.5:
    les.save_current_level()
    L("RE_PROP: --props=0 - 장식 없이 저장 완료")
    raise SystemExit(0)


# --- 1. 경계벽 -------------------------------------------------------------
# 네 변을 벽으로 두르면 '떠 있는 판'이 '아레나'로 읽힌다. 카메라가 지평선 아래를 보므로
# 이 벽은 화면 위쪽에 실제로 잡힌다(하늘 대신 벽이 보인다 = 실내감).
# --wall=0 이면 벽을 안 세운다. 카메라가 지평선 아래를 보는 구조라, 하늘(은하수)이 나올
# 자리는 '바닥 끝 너머의 띠' 하나뿐인데 이 벽이 정확히 거기를 막는다. 우주감과 폐쇄감은
# 이 게임의 카메라에서 양립하지 않는다 - 기둥만 남기면 사이로 하늘이 보인다.
if _arg("wall", 1.0) > 0.5:
    edge_line(mesh(PACK + "/Geometry/Construction/Wall_01_02/SM_Wall_02_Straight_200_600_01_A"),
              FLOOR_Z, inset=60.0, collide=True)

# --- 2. 고딕 버트레스 기둥 --------------------------------------------------
# 6관의 수직 실루엣. 벽보다 안쪽에 세워 벽과 겹쳐 보이게 한다.
ring(mesh(PACK + "/Geometry/Construction/Chapel/Buttress/SM_Chapel_01_Buttress_01_A"),
     count=_n(16), radius=VOID_RADIUS, z=FLOOR_Z - 200.0, scale=2.2, yaw_offset=-90.0,
     z_jitter=1400.0, jitter=900.0, ang_jitter=12.0, tumble=55.0)

# --- 3. 기도상 -------------------------------------------------------------
# 카메라 정면(+X)을 포함한 네 방향. 가장 강한 단일 실루엣이다.
ring(mesh(PACK + "/Geometry/Prop/Statue_Praying_01/SM_Statue_Praying_01a"),
     count=_n(5), radius=VOID_RADIUS * 1.06, z=FLOOR_Z - 150.0, scale=4.0, start_deg=45.0,
     z_jitter=1200.0, jitter=700.0, ang_jitter=20.0, tumble=45.0)

# --- 4. 묘비 / 석관 --------------------------------------------------------
scatter([PACK + "/Geometry/Prop/TombStone_01/SM_TombStone_01",
         PACK + "/Geometry/Prop/TombStone_02/SM_TombStone_02",
         PACK + "/Geometry/Prop/TombStone_03/SM_TombStone_03",
         PACK + "/Geometry/Prop/CoffinStone_01/SM_StoneCoffin_01a"],
        count=_n(26), r_min=VOID_RADIUS * 0.90, r_max=VOID_RADIUS * 1.5, z=FLOOR_Z - 300.0,
        s_min=1.4, s_max=3.2, z_jitter=1800.0, tumble=80.0)

# --- 5. 바위 --------------------------------------------------------------
scatter([PACK + "/Geometry/Rocks/Rock02/SM_Rock_02",
         PACK + "/Geometry/GroundScatter/Stones_01a/SM_GS_StoneCluster_01a",
         PACK + "/Geometry/GroundScatter/Stones_01a/SM_GS_StoneCluster_02a"],
        count=_n(22), r_min=VOID_RADIUS * 0.88, r_max=VOID_RADIUS * 1.55, z=FLOOR_Z - 400.0,
        s_min=1.3, s_max=3.8, z_jitter=2200.0, tumble=90.0)

# --- 6. 고사목 ------------------------------------------------------------
# 잎 없는 나무 실루엣. 벽 너머로 가지가 삐져나와 윤곽을 흐트러뜨린다.
ring(mesh(PACK + "/Geometry/Foliage/Tree_01/SM_Tree_01a"),
     count=_n(7), radius=VOID_RADIUS * 1.02, z=FLOOR_Z - 100.0, scale=2.0, jitter=800.0,
     start_deg=22.0, z_jitter=1500.0, ang_jitter=18.0, tumble=70.0)

# --- 7. 랜턴 스탠드 --------------------------------------------------------
# 이미시브 점광. 보라 일색인 화면에 따뜻한 색 점을 찍어 단조로움을 깬다.
LANTERN_R = 1720.0
LANTERN_N = _n(8)
ring(mesh(PACK + "/Geometry/Prop/Lantern_Stand_01/SM_Lanter_Stand_01a"),
     count=LANTERN_N, radius=VOID_RADIUS * 0.98, z=FLOOR_Z - 120.0, scale=2.4,
     start_deg=22.5, z_jitter=1000.0, jitter=600.0, ang_jitter=15.0, tumble=50.0)
# 점광만 바닥 링에 남긴다. 스탠드 메시는 위에서 공허로 내보냈지만, 바닥을 데우는
# 따뜻한 빛 웅덩이는 화면 인상의 핵심이라 유지한다. 빛은 탄과 안 부딪힌다.
for i in range(LANTERN_N):
    ang = math.radians(22.5 + 360.0 * i / LANTERN_N)
    point_light(unreal.Vector(LANTERN_R * math.cos(ang), LANTERN_R * math.sin(ang), FLOOR_Z + 380.0),
                unreal.LinearColor(1.0, 0.62, 0.28, 1.0),
                intensity=_arg("lantern_i", 900.0),
                radius=_arg("lantern_r", 650.0))

# --- 8. 달 ----------------------------------------------------------------
# 6관의 서명 같은 요소다. 카메라는 yaw 고정(+X 를 본다)이라 +X 먼 곳에 두면 항상 화면에 든다.
moon = mesh(PACK + "/VFX/SM_Moon01")
moon_mi = unreal.load_asset(PACK + "/Materials/Emissives/MI_Moon_Blue_01")
if moon:
    place(moon,
          unreal.Vector(_arg("moon_x", 17000.0), _arg("moon_y", 1500.0), _arg("moon_z", -4500.0)),
          yaw=_arg("moon_yaw", 90.0), scale=_arg("moon_s", 1.2), mat=moon_mi,
          pitch=_arg("moon_pitch", 0.0), roll=_arg("moon_roll", 0.0))

# --- 9. 배경 백드롭 ---------------------------------------------------------
# 이 게임은 카메라 yaw 가 고정이다(RECharacterBase.cpp:110 bInheritYaw=false) - 항상 +X 를
# 본다. 그래서 하늘 구가 아니라 +X 쪽 거대 평면 한 장이면 배경이 전부 덮인다.
# setup_level_lighting.py 에서 스폰했을 때는 어떤 설정으로도 렌더되지 않았고, 같은 API 를
# 쓰는 이 스크립트에서는 프롭 83개가 정상 렌더된다. 그래서 여기로 옮겼다.
sky_mat = unreal.load_asset("/Game/Materials/M_ArenaSky")
sky_mesh = mesh(_sarg("sky_mesh", "/Engine/BasicShapes/Sphere"))
if sky_mat and sky_mesh and _arg("backdrop", 1.0) > 0.5:
    # 구는 카메라를 통째로 감싸므로 방향을 안 따진다. 원점에 두면 끝.
    b = eas.spawn_actor_from_class(unreal.StaticMeshActor,
                                   unreal.Vector(0.0, 0.0, _arg("sky_z", 0.0)))
    _placed[0] += 1
    b.set_actor_label("%s%03d_Backdrop" % (TAG, _placed[0]))
    # 스케일은 양수로 둔다. 머티리얼이 two_sided 라 구 안쪽(백페이스)도 그려지므로 반전이
    # 필요 없고, 음수 스케일은 디버깅 중 변수만 늘렸다.
    r = sky_mesh.get_bounds().box_extent.x
    sc = _arg("sky_half", 20000.0) / r if r > 0 else 400.0
    # 구 UV 는 극점에서 수렴한다. 카메라가 아래를 보므로 아래쪽 극이 화면 하단 중앙에 와서
    # 별이 방사형 소용돌이로 뭉친다(실측). 구를 눕혀 극을 좌우로 보내면 시야에서 빠진다.
    b.set_actor_rotation(unreal.Rotator(0.0, 0.0, _arg("sky_roll", 90.0)), False)
    b.set_actor_scale3d(unreal.Vector(sc, sc, sc))
    bc = b.static_mesh_component
    bc.set_editor_property("static_mesh", sky_mesh)
    bc.set_material(0, sky_mat)
    bc.set_editor_property("cast_shadow", False)
    bc.set_editor_property("affect_distance_field_lighting", False)
    bc.set_editor_property("affect_dynamic_indirect_lighting", False)
    bc.set_collision_profile_name("NoCollision")
    # Plane 은 Z 두께가 0 이라 바운즈가 납작하다. 여기에 스케일 400 을 곱하면 컬링/바운즈
    # 계산이 깨져 화면에서 통째로 사라지는 사례가 이 리포에 이미 기록돼 있다
    # (REArcRenderProcessor.cpp:16 - ISM 비등방 스케일 무렌더). 바운즈를 부풀려 회피한다.
    bc.set_editor_property("bounds_scale", _arg("sky_bounds", 12.0))
    L("RE_PROP: 하늘 구 scale=%.1f" % sc)


les.save_current_level()
L("RE_PROP: 배치 %d개, 저장 완료 (누락 메시 %d종)" % (_placed[0], len(_missing)))
