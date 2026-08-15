"""
NS_REBulletExplosion 부트스트랩 생성 (#98).

엔진 템플릿 SimpleExplosion 을 복제해 프로젝트 소유로 만든다. 이 스크립트는 '정본'이
아니라 '초안 생성기'다 - 생성 후 에디터에서 손보면 낡는다. 정본은
Content/FX/NS_REBulletExplosion.uasset 이다.

Niagara 는 머티리얼과 달리 노드 그래프를 스크립트로 짜기가 비현실적이라, 처음부터
저작하지 않고 엔진 템플릿을 복제한다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
  재생성하려면 커맨드라인에 --force 를 더한다(수동 튜닝이 날아간다).
"""
import unreal

SRC  = "/Niagara/DefaultAssets/Templates/Systems/SimpleExplosion"
PKG  = "/Game/FX"
NAME = "NS_REBulletExplosion"
FULL = PKG + "/" + NAME

if not unreal.EditorAssetLibrary.does_asset_exist(SRC):
    unreal.log_error("RE_FX: 엔진 템플릿이 없다: %s" % SRC)
    raise SystemExit(1)

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    if "--force" not in unreal.SystemLibrary.get_command_line().split():
        unreal.log_error("RE_FX: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % FULL)
        unreal.log_error("RE_FX: 재생성이 정말 필요하면 커맨드라인에 --force 를 넣어라.")
        raise SystemExit(1)
    unreal.log_warning("RE_FX: --force - 기존 에셋을 지우고 재생성한다: %s" % FULL)
    unreal.EditorAssetLibrary.delete_asset(FULL)

if not unreal.EditorAssetLibrary.duplicate_asset(SRC, FULL):
    unreal.log_error("RE_FX: 복제 실패 %s -> %s" % (SRC, FULL))
    raise SystemExit(1)

unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log("RE_FX: 생성 완료 %s" % FULL)
