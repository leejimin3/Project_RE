# HP바 월드스페이스 위젯 설계 (#29)

날짜: 2026-07-13 | 이슈: #29 | 마일스톤: M2 (마지막 이슈)

## 목표

플레이어·보스 머리 위 월드스페이스 HP바. 피격 시 실시간 갱신. M4 데디 전환 시 코드 수정 없이 클라 갱신 동작.

## 결정 사항 (브레인스토밍 확정)

| 항목 | 결정 | 이유 |
|------|------|------|
| 갱신 경로 | `ReplicatedUsing=OnRep_Health` + 서버 경로 직접 호출 | Tick 폴링은 탄막 프로젝트 성격(M3 프로파일링)에 역행. TakeDamage 직접 호출만으로는 M4 데디에서 클라 갱신 불가 |
| 위젯 비주얼 | 순수 C++ `WidgetTree`로 `UProgressBar` 생성 | BP 자산 0개 — 에디터 수작업 없이 코드만으로 완결, 스크린샷 프로브 검증 가능 |
| 공간 모드 | World Space | 탑뷰 절대회전 고정 카메라 — 위젯을 카메라 방향으로 한 번만 회전시키면 끝 |
| 배선 구조 | 커스텀 `UWidgetComponent` 서브클래스 (접근법 A) | 소비자 2명(플레이어+보스), 공통 베이스 없음 — 셋업 캡슐화 실익 |

## 신규 파일

### `Source/Project_RE/UI/REHealthBarWidget.{h,cpp}`

`UUserWidget` 상속. `NativeConstruct`에서 `WidgetTree`로 `UProgressBar` 루트 생성.

- `SetPercent(float)` — 프로그레스바 percent 설정 (0~1)
- `SetBarColor(FLinearColor)` — `SetFillColorAndOpacity`
- 초기값: percent 1.0

### `Source/Project_RE/UI/REHealthBarComponent.{h,cpp}`

`UWidgetComponent` 상속. 생성자에서 셋업 완결:

- `SetWidgetSpace(EWidgetSpace::World)`
- `SetWidgetClass(UREHealthBarWidget::StaticClass())`
- `SetDrawSize(FVector2D(100, 10))`
- `SetUsingAbsoluteRotation(true)` + `SetWorldRotation(FRotator(50, 180, 0))` — 카메라(피치 -50 고정)를 정면으로 봄. **절대회전 필수**: 캐릭터가 `bOrientRotationToMovement`로 회전해도 바는 고정
- 상대위치 Z +120 (캡슐 위)

API:

- `SetHealthPercent(float)` — 위젯 생성 전 호출 대비 값 캐시. 위젯 있으면 즉시 적용, 없으면 `InitWidget` 완료 후 적용
- `BarColor` (`EditAnywhere`) — 위젯 초기화 시 적용. 플레이어 초록, 보스 빨강 (각 캐릭터 생성자에서 지정)

## 기존 파일 수정

### `RECharacterBase.{h,cpp}` / `REBossCharacter.{h,cpp}` (동일 패턴, 각 ~5줄)

1. `Health` → `UPROPERTY(ReplicatedUsing = OnRep_Health, ...)`
2. `UFUNCTION() void OnRep_Health();` → `HealthBar->SetHealthPercent(Health / MaxHealth)`
3. `TakeDamage` 서버 경로에서 동일 호출 (싱글/리슨 서버는 OnRep 미발화)
4. 생성자: `HealthBar = CreateDefaultSubobject<UREHealthBarComponent>(...)` + 부착 + 색 지정

### `Project_RE.Build.cs`

`UMG`·`Slate` 모듈 이미 존재 — 모듈 추가 불필요. `PublicIncludePaths`에 `Project_RE/UI`만 추가 (플랫 include 컨벤션).

## 데이터 흐름

```
서버 TakeDamage → Health 차감 → SetHealthPercent()          (서버/싱글 경로)
                              ↘ Health 복제 → 클라 OnRep_Health → SetHealthPercent()  (M4 데디 경로)
```

데디 서버: 엔진이 `UWidgetComponent` 위젯 생성 스킵 — 별도 가드 불필요.

## 검증

1. 빌드 통과
2. headless 실RHI `-windowed` + `FScreenshotRequest` 스크린샷: 플레이어/보스 머리 위 바 표시 확인
3. 자동사격으로 보스바 감소 + 탄막 피격으로 플레이어바 감소 → 시간차 스크린샷 2장 비교
4. PIE 육안 최종 1회

## 스코프 제외

사망 시 바 숨김, 바 애니메이션, 데미지 숫자, 팀색 구분 로직 — 이슈 범위 밖.
