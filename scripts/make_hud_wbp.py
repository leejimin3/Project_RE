"""
WBP_PlayerHud / WBP_Result 껍데기 생성 (#121).

## 무엇을 만들고, 무엇을 안 만드나

C++ 클래스를 부모로 한 **빈 위젯 블루프린트**만 만든다. **위젯 트리는 못 채운다** —
`UWidgetBlueprint` 의 `WidgetTree` 프로퍼티가 파이썬에 노출돼 있지 않고, 트리를 짜 줄
에디터 라이브러리도 없다(실측: `widget_tree` / `WidgetBlueprintLibrary` 둘 다 부재).
그래서 룩은 디자이너에서 사람이 만든다 — 그게 WBP 전환(#121 B안)을 고른 이유이기도 하다.

트리가 비어 있는 동안에도 화면은 안 비운다. C++ 쪽 `Initialize()` 가 예전 WidgetTree 를
그대로 구성하는 폴백을 유지한다. 디자이너에서 아래 이름의 위젯을 만들면 그 순간부터
바인딩된 위젯이 폴백을 대체한다.

## 디자이너에서 지켜야 하는 이름 계약

WBP_PlayerHud (부모 UREPlayerHudWidget)

  | 이름         | 타입          | 쓰임                                   |
  |--------------|---------------|----------------------------------------|
  | HealthBar    | ProgressBar   | HP 비율                                |
  | HealthText   | TextBlock     | "HP 100/100"                           |
  | DashBar      | ProgressBar   | 대쉬 쿨다운 비율                       |
  | DashText     | TextBlock     | "DASH" / 남은 초                       |
  | BulletText   | TextBlock     | 화면상 투사체 수                       |
  | BulletBox    | VerticalBox   | 투사체 표시 묶음 — HudBulletCount 0 이 통째로 숨긴다 |
  | PatternText  | TextBlock     | 현재 보스 패턴 이름                    |
  | PatternBox   | VerticalBox   | 패턴 표시 묶음 — HudPattern 0 이 통째로 숨긴다 |
  | CheatText    | TextBlock     | 켜진 데브 치트 나열                    |
  | CheatBox     | VerticalBox   | 치트 표시 묶음 — 켜진 치트가 없으면 스스로 접힌다 |

WBP_Result (부모 UREResultWidget)

  | 이름       | 타입      | 쓰임              |
  |------------|-----------|-------------------|
  | ResultText | TextBlock | VICTORY / DEFEAT  |

**전부 아니면 전무다.** 하나라도 만들면 C++ 폴백이 통째로 꺼진다 — UUserWidget 의 루트는
하나뿐이라 바인딩된 위젯과 C++ 가 만든 루트를 섞을 수 없다. 이름을 바꾸면 조용히 바인딩이
끊기고 그 값만 안 갱신된다(`BindWidgetOptional` 이라 컴파일은 통과한다).

## 왜 스크립트인가

만들어진 애셋은 `Content/UI/` 에 남아 버전관리된다. 이 스크립트는 새 환경에서 애셋이
날아갔을 때의 재생성 수단이자, 부모 클래스를 무엇으로 걸어야 하는지의 기록이다.
이미 있으면 덮지 않는다 — 디자이너 작업물을 지우면 안 된다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" \
      -unattended -nosplash -nop4 -NoSound -nullrhi
"""
import unreal

PKG = "/Game/UI"
TARGETS = [
    ("WBP_PlayerHud", "/Script/Project_RE.REPlayerHudWidget"),
    ("WBP_Result",    "/Script/Project_RE.REResultWidget"),
]

tools = unreal.AssetToolsHelpers.get_asset_tools()

for name, parent_path in TARGETS:
    full = "%s/%s" % (PKG, name)
    if unreal.EditorAssetLibrary.does_asset_exist(full):
        # 디자이너 작업물이 들어 있을 수 있다 — 절대 덮지 않는다.
        unreal.log("RE_WBP: 이미 존재 - 건너뜀 %s" % full)
        continue

    parent = unreal.load_class(None, parent_path)
    if parent is None:
        unreal.log_error("RE_WBP: 부모 클래스 없음 %s - 에디터 타겟을 먼저 빌드해라" % parent_path)
        raise SystemExit(1)

    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent)

    asset = tools.create_asset(name, PKG, None, factory)
    if asset is None:
        unreal.log_error("RE_WBP: 생성 실패 %s" % full)
        raise SystemExit(1)

    if not unreal.EditorAssetLibrary.save_asset(full, False):
        unreal.log_error("RE_WBP: 저장 실패 %s" % full)
        raise SystemExit(1)

    unreal.log("RE_WBP: 생성 완료 %s (부모 %s)" % (full, parent_path))

unreal.log("RE_WBP: 끝")
