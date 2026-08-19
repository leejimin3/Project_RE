"""
보스 후보 스켈레탈 메시의 실측치를 덤프한다 (#118).

왜 필요한가:
  보스 캡슐은 엔진 기본값(HalfHeight 88)이고 **불변이다** — 자동사격 트레이스
  시작 높이 Z+20, 보스 스폰 좌표 (600,0,90), 곡사탄 착지 평면 Z 2 가 전부 이
  지오메트리에서 파생됐다(#54/#55/#56/#57). 따라서 모델을 캡슐에 맞춰야 하고,
  그러려면 **원저작 높이를 먼저 알아야 한다.** 눈대중으로 스케일을 정하면
  발이 바닥에 묻히거나 머리가 HP바를 뚫는다.

  스켈레톤도 같이 찍는다. 보스는 플레이어와 `ABP_Unarmed` 를 공유 중이라
  (`REBossCharacter.cpp:70`) 스켈레톤이 다르면 애님BP 가 못 붙는다. 다만 보스는
  고정형이라 idle 하나면 되므로, 팩에 딸린 idle 을 찾아 single-node 로 돌리는
  경로가 리타겟보다 싸다 — 그 idle 후보도 여기서 같이 나열한다.

실행:
  UnrealEditor-Cmd.exe <uproject> \
      -ExecutePythonScript="<이 파일> --root=/Game/StoneGolem" \
      -unattended -nosplash -nop4 -NoSound -nullrhi

인자:
  --root=<경로>    스캔 루트 (기본 /Game). 팩 폴더를 주면 빠르다
  --filter=<문자열> 애셋 이름 부분일치 필터 (기본 없음)
"""
import unreal


def arg(name, default):
    # -ExecutePythonScript 는 마지막 토큰에 따옴표를 붙여 넘긴다 — 반드시 벗긴다.
    for tok in unreal.SystemLibrary.get_command_line().split():
        tok = tok.strip("\"'")
        if tok.startswith(name + "="):
            return tok[len(name) + 1:]
    return default


ROOT = arg("--root", "/Game")
FILTER = arg("--filter", "").lower()

CAPSULE_HALF_HEIGHT = 88.0            # AREBossCharacter 캡슐(엔진 기본) — 불변
CAPSULE_HEIGHT = CAPSULE_HALF_HEIGHT * 2.0

unreal.log("RE_INSPECT: root=%s filter=%s" % (ROOT, FILTER or "(없음)"))

paths = unreal.EditorAssetLibrary.list_assets(ROOT, recursive=True, include_folder=False)

meshes = []
anims = []
for path in paths:
    path = path.split(".")[0]
    if FILTER and FILTER not in path.lower():
        continue
    asset = unreal.load_asset(path)
    if asset is None:
        continue
    if isinstance(asset, unreal.SkeletalMesh):
        meshes.append((path, asset))
    elif isinstance(asset, unreal.AnimSequence):
        anims.append((path, asset))

if not meshes:
    unreal.log_error("RE_INSPECT: %s 아래에 스켈레탈 메시 없음 — 임포트 됐는지 확인해라" % ROOT)
    raise SystemExit(1)

for path, mesh in meshes:
    bounds = mesh.get_bounds()   # UE 5.8 파이썬은 imported_bounds 프로퍼티를 안 연다
    extent = bounds.box_extent
    height = extent.z * 2.0
    width = max(extent.x, extent.y) * 2.0
    skeleton = mesh.get_editor_property("skeleton")
    skel_path = skeleton.get_path_name().split(".")[0] if skeleton else "(없음)"

    unreal.log("RE_INSPECT: --- %s" % path)
    unreal.log("RE_INSPECT:   높이=%.1f uu  폭=%.1f uu  원점Z=%.1f" % (height, width, bounds.origin.z))
    unreal.log("RE_INSPECT:   스켈레톤=%s" % skel_path)
    if height > 0.0:
        # 캡슐(176uu)의 배수로 보이게 하려면 얼마를 곱해야 하는가.
        # 시각 크기만 키우고 캡슐은 그대로 둔다 — 판정은 캡슐이 한다.
        for mult in (1.0, 1.5, 2.0):
            unreal.log("RE_INSPECT:   캡슐 x%.1f (%.0f uu) 로 보이려면 scale=%.3f"
                       % (mult, CAPSULE_HEIGHT * mult, CAPSULE_HEIGHT * mult / height))

    # 이 메시 스켈레톤에 붙는 애님 후보 — idle 을 골라 single-node 로 돌린다.
    matched = [p for p, a in anims
               if a.get_editor_property("skeleton") == skeleton]
    if matched:
        unreal.log("RE_INSPECT:   애님 %d개:" % len(matched))
        for p in matched:
            unreal.log("RE_INSPECT:     %s" % p)
    else:
        unreal.log("RE_INSPECT:   애님 없음 — idle 이 없으면 T포즈로 선다")


# --- 머티리얼 ---------------------------------------------------------------
# 패턴별 외관 변화(#118 파생)를 머티리얼로 낼 수 있는지 판단용.
# 팩이 스킨 변형 MI 를 들고 있으면 SetMaterial 교체가 가장 싸고,
# 파라미터(Vector/Scalar)만 있으면 DynamicMaterialInstance 로 색을 굴린다.
for path in paths:
    path = path.split(".")[0]
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.MaterialInstanceConstant):
        parent = asset.get_editor_property("parent")
        unreal.log("RE_INSPECT: === MI %s (parent=%s)"
                   % (path, parent.get_path_name().split(".")[0] if parent else "(없음)"))
        for kind in ("vector_parameter_values", "scalar_parameter_values", "texture_parameter_values"):
            for entry in (asset.get_editor_property(kind) or []):
                info = entry.get_editor_property("parameter_info")
                val = entry.get_editor_property("parameter_value")
                unreal.log("RE_INSPECT:     %s.%s = %s" % (kind[:6], info.get_editor_property("name"), val))
    elif isinstance(asset, unreal.Material):
        unreal.log("RE_INSPECT: === Material %s" % path)
        for getter, tag in ((unreal.MaterialEditingLibrary.get_vector_parameter_names, "vector"),
                            (unreal.MaterialEditingLibrary.get_scalar_parameter_names, "scalar"),
                            (unreal.MaterialEditingLibrary.get_texture_parameter_names, "texture")):
            names = getter(asset) or []
            if names:
                unreal.log("RE_INSPECT:     %s params: %s" % (tag, [str(n) for n in names]))
