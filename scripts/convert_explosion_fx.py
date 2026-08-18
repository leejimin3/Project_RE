"""
NS_REBulletExplosion 생성 — Cascade 팩 폭발을 Niagara 로 변환 (#119).

왜 변환인가:
  Realistic Starter VFX Pack Vol2 는 전부 Cascade(`P_*`) 다. Niagara 시스템이 하나도 없다.
  런타임 코드(`REExplosionFx.cpp`)는 `UNiagaraSystem` + `UNiagaraFunctionLibrary` 전용이라
  `P_` 애셋을 그대로 꽂으면 `LoadObject<UNiagaraSystem>` 이 null 을 돌려주고 폭발이 안 나온다.
  Cascade 는 Epic 이 걷어내는 중인 경로라 런타임을 Niagara 로 통일한다.

  엔진 `CascadeToNiagaraConverter` 플러그인(Beta, 에디터 전용)을 쓴다. .uproject 에 활성화가
  필요하다 — 기본 비활성이다.

산출물 이름을 기존 `NS_REBulletExplosion` 그대로 재사용한다. 그래야 C++ 경로 상수를
건드리지 않는다.

변환기 동작:
  `<원본이름>_Converted` 를 **원본과 같은 폴더**(= 팩 폴더)에 만든다. 그래서 만든 뒤
  /Game/FX 로 옮긴다. 팩 폴더는 gitignore 대상이라 거기 두면 리포에 안 남는다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일> --force" \
      -unattended -nosplash -nop4
"""
import unreal

DST   = "/Game/FX/NS_REBulletExplosion"
CMD   = unreal.SystemLibrary.get_command_line()


def _arg(name, default):
    """`--name=값`. 마지막 토큰에 닫는 따옴표가 붙어 오므로 strip 필수."""
    for tok in CMD.split():
        tok = tok.strip("\"'")
        if tok.startswith("--%s=" % name):
            return tok.split("=", 1)[1]
    return default


# 원본은 인자로 고른다. 팩 이펙트는 이미터 수가 곧 게임스레드 비용이라(#119 실측: Big_A 는
# 폭발 1회당 GT ~0.33ms, 엔진 SimpleExplosion 의 10배) 가벼운 원본으로 갈아타는 일이 잦다.
SRC   = _arg("src", "/Game/Realistic_Starter_VFX_Pack_Vol2/Particles/Explosion/P_Explosion_Big_C")

# 뺄 이미터 이름(쉼표 구분). 이미터 하나가 곧 게임스레드 비용이라 안 쓰는 건 애초에
# 변환하지 않는다. 기본값은 #119 에서 정한 것 — 왜곡(refraction)과 스파크(sparks_l) 제외,
# 화염 본체(main/trails)와 연기는 유지.
#   P_Explosion_Big_C 이미터: shockwave_smoke puff sparks_l impact refraction dummy trails main
SKIP  = set(s for s in _arg("skip", "refraction,sparks_l").split(",") if s)

# 파티클 수명 상한(초). 팩 폭발은 화염 본체(main)가 2.5~3.5초라 터진 자리에 불꽃이 오래
# 남는다. 탄막 게임에서는 잔상이 탄 가독성을 깎으므로 짧게 끊는다.
# Cascade 원본 모듈을 **인메모리로만** 깎고 변환한다 - 팩 애셋은 저장하지 않는다.
MAXLIFE = float(_arg("maxlife", "0.8"))
# `-ExecutePythonScript="<경로> --force"` 로 넘기면 마지막 토큰에 닫는 따옴표가 붙어 온다
# (`--force"`). strip 없이 비교하면 인자가 조용히 무시된다.
FORCE = "--force" in [t.strip("\"'") for t in CMD.split()]

if not unreal.EditorAssetLibrary.does_asset_exist(SRC):
    unreal.log_error("RE_FX2: Cascade 원본이 없다: %s" % SRC)
    unreal.log_error("RE_FX2: 팩(Realistic_Starter_VFX_Pack_Vol2)이 임포트돼 있어야 한다.")
    raise SystemExit(1)

if unreal.EditorAssetLibrary.does_asset_exist(DST):
    if not FORCE:
        unreal.log_error("RE_FX2: 이미 존재한다 - 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % DST)
        unreal.log_error("RE_FX2: 재생성이 정말 필요하면 커맨드라인에 --force 를 넣어라.")
    else:
        # delete_asset 은 인메모리 삭제라 .uasset 파일이 디스크에 남고, 그 경로를 점유한 채로는
        # rename_asset 이 실패한다(실측 - 변환은 성공했는데 이동만 두 번 실패했다).
        # 파일을 직접 지우는 쪽이 확실하다. M_ArenaSky 재생성도 같은 방식이다 (#122).
        unreal.log_error("RE_FX2: --force 만으로는 못 지운다 - 먼저 파일을 직접 지워라:")
        unreal.log_error("RE_FX2:   rm -f Content/FX/NS_REBulletExplosion.uasset")
    raise SystemExit(1)

cascade = unreal.load_asset(SRC)
if cascade is None:
    unreal.log_error("RE_FX2: 원본 로드 실패: %s" % SRC)
    raise SystemExit(1)

# 변환기 파이썬은 플러그인의 Content/Python 에 있다. 플러그인이 켜져 있으면 UE 가
# 그 디렉터리를 sys.path 에 넣어준다 — 안 켜져 있으면 여기서 ImportError 로 죽는다.
try:
    import CascadeToNiagaraConverter
except ImportError:
    unreal.log_error("RE_FX2: CascadeToNiagaraConverter 를 import 하지 못했다.")
    unreal.log_error("RE_FX2: .uproject 의 플러그인 활성화를 확인해라 (기본 비활성, Editor 전용).")
    raise SystemExit(1)

if MAXLIFE > 0:
    fx = unreal.FXConverterUtilitiesLibrary
    for em in fx.get_cascade_system_emitters(cascade):
        em_name = str(fx.get_cascade_emitter_name(em))
        lod = fx.get_cascade_emitter_lod_level(em, 0)
        for mod in fx.get_lod_level_modules(lod):
            if "Lifetime" not in type(mod).__name__:
                continue
            # FRawDistributionFloat 는 구조체 복사본이 돌아오지만 안의 distribution 은
            # UObject 포인터라 여기서 고치면 원본에 반영된다.
            dist = mod.get_editor_property("lifetime").get_editor_property("distribution")
            old_max = dist.get_editor_property("max")
            if old_max <= MAXLIFE:
                continue
            dist.set_editor_property("max", MAXLIFE)
            if dist.get_editor_property("min") > MAXLIFE:
                dist.set_editor_property("min", MAXLIFE)
            unreal.log("RE_FX2: 수명 상한 %s %.2f -> %.2f" % (em_name, old_max, MAXLIFE))

if SKIP:
    # 변환기에는 "이 이미터만 변환" 옵션이 없다. 대신 변환기 모듈이 들고 있는
    # FXConverterUtilitiesLibrary 참조를 가로채 이미터 목록에서 빼버린다.
    # 엔진 변환 로직을 복사하지 않으므로 엔진이 바뀌어도 같이 굴러간다.
    class _EmitterFilter(object):
        def __init__(self, inner, skip):
            self._inner, self._skip = inner, skip

        def __getattr__(self, name):
            return getattr(self._inner, name)   # 나머지 호출은 그대로 통과

        def get_cascade_system_emitters(self, system):
            kept = []
            for em in self._inner.get_cascade_system_emitters(system):
                name = str(self._inner.get_cascade_emitter_name(em))
                if name in self._skip:
                    unreal.log("RE_FX2: 이미터 제외 %s" % name)
                    continue
                kept.append(em)
            return kept

    CascadeToNiagaraConverter.ueFxUtils = _EmitterFilter(
        unreal.FXConverterUtilitiesLibrary, SKIP)

# 변환기는 결과 보고용 UObject 를 인자로 받는다. 엔진이 부를 때는 엔진이 만들어 넘기므로
# 스크립트에서 부를 때는 직접 만들어야 한다.
results = unreal.ConvertCascadeToNiagaraResults()
results.set_editor_property("cancelled_by_user", False)
results.set_editor_property("cancelled_by_python_error", True)

unreal.log("RE_FX2: 변환 시작 %s" % SRC)
CascadeToNiagaraConverter.convert_cascade_to_niagara(cascade, results)

if results.get_editor_property("cancelled_by_python_error"):
    unreal.log_error("RE_FX2: 변환기가 파이썬 에러로 중단됐다 - 위 로그를 봐라.")
    raise SystemExit(1)

# 변환기는 원본 폴더에 <이름>_Converted 를 만든다. 이름 충돌 시 뒤에 숫자가 붙으므로
# 고정 이름으로 단정하지 않고 실제로 생긴 것을 찾는다.
folder = SRC.rsplit("/", 1)[0]
base = SRC.rsplit("/", 1)[1] + "_Converted"
made = [p for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=False)
        if p.split(".")[0].rsplit("/", 1)[1].startswith(base)]
if not made:
    unreal.log_error("RE_FX2: 변환 결과물을 못 찾았다 (%s/%s*)" % (folder, base))
    raise SystemExit(1)
# 여러 개면 가장 최근 것 = 이름이 사전순 최대(숫자 접미사가 커진다).
converted = sorted(made)[-1].split(".")[0]
unreal.log("RE_FX2: 변환 결과물 %s" % converted)

# 변환 결과물을 먼저 디스크에 굳힌다. 저장 안 된 신규 애셋은 rename_asset 이 못 옮긴다.
unreal.EditorAssetLibrary.save_asset(converted, False)

if not unreal.EditorAssetLibrary.rename_asset(converted, DST):
    unreal.log_error("RE_FX2: 이동 실패 %s -> %s" % (converted, DST))
    raise SystemExit(1)

# only_if_is_dirty=False. 스크립트로 만든 변경은 패키지를 dirty 로 표시하지 않는 경우가 있어
# 기본값(True)으로 부르면 저장이 조용히 건너뛰어진다 (#122 에서 여러 시간 날린 함정).
unreal.EditorAssetLibrary.save_asset(DST, False)

made_asset = unreal.load_asset(DST)
unreal.log("RE_FX2: 생성 완료 %s (class=%s)" % (DST, type(made_asset).__name__))
