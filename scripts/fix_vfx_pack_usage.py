"""
NS_REBulletExplosion 이 참조하는 팩 머티리얼에 Niagara usage 플래그를 켠다 (#119).

왜 필요한가:
  팩(Realistic Starter VFX Pack Vol2)은 Cascade 용이라 머티리얼에
  `bUsedWithParticleSprites` 만 켜져 있고 `bUsedWithNiagaraSprites` 는 꺼져 있다.
  Niagara 스프라이트 렌더러가 이 플래그 없는 머티리얼을 받으면 **조용히 엔진 기본
  머티리얼로 폴백한다.** 화면에 텍스처 없는 불투명 회색 쿼드가 뜬다 — 로그도 에러도
  안 남고 화면으로만 잡힌다. ISM 쪽 `bUsedWithInstancedStaticMeshes` 와 같은 함정이다.

왜 스크립트인가:
  팩은 재배포 불가라 gitignore 대상이다. 즉 이 수정은 리포에 안 남는다.
  새 환경에서는 팩 임포트 → convert_explosion_fx.py → 이 스크립트 순으로 돌려야
  폭발이 정상으로 보인다.

대상은 하드코딩하지 않고 **변환 결과물의 실제 의존성에서 뽑는다.** 원본을 바꾸면
(`convert_explosion_fx.py --src=...`) 머티리얼 목록도 같이 바뀌기 때문이다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일>" \
      -unattended -nosplash -nop4
"""
import unreal

TARGET = "/Game/FX/NS_REBulletExplosion"

if not unreal.EditorAssetLibrary.does_asset_exist(TARGET):
    unreal.log_error("RE_USAGE: %s 없음 - convert_explosion_fx.py 를 먼저 돌려라" % TARGET)
    raise SystemExit(1)

ar = unreal.AssetRegistryHelpers.get_asset_registry()
deps = ar.get_dependencies(TARGET, unreal.AssetRegistryDependencyOptions()) or []

changed = 0
seen = set()
for dep in deps:
    path = str(dep)
    if not path.startswith("/Game/"):
        continue   # 엔진/Niagara 내장 애셋은 이미 플래그가 서 있다
    asset = unreal.load_asset(path)
    if asset is None or not isinstance(asset, unreal.MaterialInterface):
        continue   # 텍스처·벡터필드·메시는 대상 아님

    # 플래그는 부모 Material 이 들고 있다. MaterialInstance 에 세팅하면 아무 일도 안 일어난다.
    base = asset.get_editor_property("parent") if isinstance(asset, unreal.MaterialInstance) else asset
    base_path = unreal.SystemLibrary.get_path_name(base).split(".")[0]
    if base_path in seen:
        continue
    seen.add(base_path)

    wants = [("used_with_niagara_sprites", True)]
    # 메시 파티클(파편 등)로도 쓰이는 머티리얼은 별도 플래그가 필요하다. 어느 쪽인지
    # 스크립트로는 못 가리므로, 메시 파티클 지원이 이미 켜져 있던 것만 유지하고
    # 나머지는 스프라이트만 켠다 — 필요하면 화면 보고 여기 추가해라.
    if base.get_editor_property("used_with_mesh_particles"):
        wants.append(("used_with_niagara_mesh_particles", True))

    dirty = False
    for prop, val in wants:
        if base.get_editor_property(prop) != val:
            base.set_editor_property(prop, val)
            dirty = True
            unreal.log("RE_USAGE: %s.%s = %s" % (base.get_name(), prop, val))

    if dirty:
        # only_if_is_dirty=False. 스크립트 변경은 패키지를 dirty 로 안 세우는 경우가 있어
        # 기본값(True)으로 부르면 저장이 조용히 건너뛰어진다 (#122).
        unreal.EditorAssetLibrary.save_asset(base_path, False)
        changed += 1

unreal.log("RE_USAGE: 검사 %d개 / 갱신 %d개" % (len(seen), changed))
