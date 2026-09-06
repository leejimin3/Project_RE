# 탄막 머티리얼 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 겹친 탄의 경계가 보이게 한다 — 언릿 발광 + 프레넬 림 머티리얼 `M_REBullet` 을 만들고 ISM 에 연결하며, 퍼인스턴스 커스텀데이터 float 1개로 스폰 팝을 넣는다.

**Architecture:** 머티리얼 에셋은 Python 부트스트랩 스크립트가 헤드리스로 생성한다(`UnrealEditor-Cmd.exe -ExecutePythonScript=`). C++ 는 그 에셋을 로드해 3개 ISM 중 탄환 2개에 물리고, 렌더 프로세서가 트랜스폼과 나란히 스폰 팝 float 배열을 채워 `SetCustomData` 로 한 번에 넘긴다.

**Tech Stack:** UE 5.8, `PythonScriptPlugin` + `MaterialEditingLibrary`(에셋 저작), `UInstancedStaticMeshComponent` 퍼인스턴스 커스텀데이터, Mass Entity

## Global Constraints

- **불투명 유지.** `Blend Mode: Opaque`. additive/translucent 로 가면 early-Z 가 사라져 겹친 만큼 오버드로우가 누적되고 **현재 50,000발 상한이 무너진다.**
- **셰이딩 모델은 `MSM_Unlit`.** 툰 밴딩은 탄환 범위 밖이다(밴딩할 조명이 없다).
- **수명 페이드 금지.** 죽기 직전 탄이 흐려지면 치명성이 오독된다. 커스텀데이터는 **스폰 팝 전용**.
- **철회 기준:** 재측정에서 60fps 상한(p99 ≤ 16.6 ms)이 **45,000발 아래**로 떨어지면 스폰 팝(Task 3)을 되돌린다. 림이 본 목적이고 팝은 부가다.
- **`Project_RE.uproject` 를 절대 스테이징/커밋하지 않는다.** 머신 로컬 `EngineAssociation` GUID를 담고 있어 `git status` 에 항상 수정됨으로 뜨는 것이 정상이다.
- **worktree 금지 — 본체 작업 디렉터리에서 실행한다.** 에디터 실행과 프로파일 런이 머신 로컬 `uproject` 를 쓴다.
- 브랜치: `feature/M6-bullet-material` (이미 생성됨, 스펙 커밋 `250ea83` 존재)

---

## File Structure

| 파일 | 책임 | 상태 |
|---|---|---|
| `scripts/make_bullet_material.py` | `M_REBullet` 부트스트랩 생성 | **신규** |
| `Content/Materials/M_REBullet.uasset` | 머티리얼 에셋 (정본) | **신규 — 스크립트 산출물** |
| `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp` | ISM 생성·머티리얼 연결·커스텀데이터 슬롯 | 수정 |
| `Source/Project_RE/Mass/REBulletRenderProcessor.cpp` | 직선탄 트랜스폼 + 스폰 팝 전달 | 수정 |
| `Source/Project_RE/Mass/REArcRenderProcessor.cpp` | 곡사탄 트랜스폼 + 스폰 팝 전달 | 수정 |
| `docs/profiling/M6-gpu-breakdown.md` | 상한 재측정 기록 | 수정 |

---

### Task 1: Python 부트스트랩으로 `M_REBullet` 생성

**Files:**
- Create: `scripts/make_bullet_material.py`
- Create (산출물): `Content/Materials/M_REBullet.uasset`

**Interfaces:**
- Produces: 머티리얼 에셋 경로 **`/Game/Materials/M_REBullet`**. 파라미터 이름 **`Color`**(VectorParameter), **`RimPower`**(ScalarParameter), **`RimStrength`**(ScalarParameter). Task 2 가 이 경로와 `Color` 이름을 쓴다.

- [ ] **Step 1: 스크립트 작성**

`scripts/make_bullet_material.py` 를 만든다. 아래 API 호출은 전부 이 엔진에서 **실행 확인된 것**이다(임시 에셋으로 end-to-end 검증 완료).

```python
"""
M_REBullet 부트스트랩 생성 (#97).

이 스크립트는 '정본'이 아니라 '초안 생성기'다. 생성 후 에디터에서 수치를 손보면
이 스크립트는 낡는다. 정본은 Content/Materials/M_REBullet.uasset 이다.
재실행하면 기존 에셋을 지우고 다시 만들므로 수동 튜닝이 날아간다 — 확인 프롬프트 대신
이미 존재하면 중단한다.

실행:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="<이 파일 절대경로>" -unattended -nosplash -nop4
"""
import unreal

PKG  = "/Game/Materials"
NAME = "M_REBullet"
FULL = PKG + "/" + NAME

if unreal.EditorAssetLibrary.does_asset_exist(FULL):
    unreal.log_error("RE_MAT: 이미 존재한다 — 수동 튜닝을 덮어쓰지 않으려고 중단한다: %s" % FULL)
    raise SystemExit(1)

tools = unreal.AssetToolsHelpers.get_asset_tools()
mat = tools.create_asset(NAME, PKG, unreal.Material, unreal.MaterialFactoryNew())
if mat is None:
    unreal.log_error("RE_MAT: 생성 실패")
    raise SystemExit(1)

# 언릿 — 씬 조명과 무관하게 항상 같은 밝기. 탄막 가독성이 조명 방향에 흔들리지 않는다.
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)

mel = unreal.MaterialEditingLibrary

col = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -800, 0)
col.set_editor_property("parameter_name", "Color")
col.set_editor_property("default_value", unreal.LinearColor(1.0, 0.15, 0.15, 1.0))

# 프레넬 — 실루엣 가장자리를 밝힌다. 인접한 동일 색 구체 사이에 경계선이 생기는 원리.
fr = mel.create_material_expression(mat, unreal.MaterialExpressionFresnel, -800, 220)

rim_pow = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1050, 220)
rim_pow.set_editor_property("parameter_name", "RimPower")
rim_pow.set_editor_property("default_value", 2.5)
mel.connect_material_expressions(rim_pow, "", fr, "ExponentIn")

rim_str = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -800, 400)
rim_str.set_editor_property("parameter_name", "RimStrength")
rim_str.set_editor_property("default_value", 3.0)

# 림 기여: 1 + Fresnel * RimStrength  → 중심은 기본 밝기, 가장자리만 솟는다.
rim_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -560, 300)
mel.connect_material_expressions(fr, "", rim_mul, "A")
mel.connect_material_expressions(rim_str, "", rim_mul, "B")

one = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -560, 460)
one.set_editor_property("r", 1.0)

rim_add = mel.create_material_expression(mat, unreal.MaterialExpressionAdd, -380, 360)
mel.connect_material_expressions(one, "", rim_add, "A")
mel.connect_material_expressions(rim_mul, "", rim_add, "B")

col_rim = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 120)
mel.connect_material_expressions(col, "", col_rim, "A")
mel.connect_material_expressions(rim_add, "", col_rim, "B")

# 스폰 팝 — 퍼인스턴스 커스텀데이터 0번. 렌더 프로세서가 0~1 을 채운다(Task 3).
# Task 3 전에는 커스텀데이터가 0 이므로 탄이 검게 보인다 — Task 3 이 같은 브랜치에서 이어진다.
pop = mel.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -380, 620)
pop.set_editor_property("constant", 1.0)   # 데이터가 없을 때의 대체값

final_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 0, 300)
mel.connect_material_expressions(col_rim, "", final_mul, "A")
mel.connect_material_expressions(pop, "", final_mul, "B")

mel.connect_material_property(final_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_asset(FULL)
unreal.log("RE_MAT: 생성 완료 %s" % FULL)
```

- [ ] **Step 2: 헤드리스로 실행**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'E:\UnrealProjects\Project_RE\Project_RE.uproject' -ExecutePythonScript="E:/UnrealProjects/Project_RE/scripts/make_bullet_material.py" -unattended -nosplash -nop4 -abslog="E:\UnrealProjects\Project_RE\Saved\make_mat.log"
```

Expected: `EXIT=0`.

- [ ] **Step 3: 생성 확인**

Run:
```
Select-String -Path .\Saved\make_mat.log -Pattern 'RE_MAT|LogPython: Error' | ForEach-Object { $_.Line }
Test-Path .\Content\Materials\M_REBullet.uasset
```

Expected: `RE_MAT: 생성 완료 /Game/Materials/M_REBullet` 가 보이고, `LogPython: Error` 는 없으며, `Test-Path` 가 `True`.

**`LogPython: Error` 가 하나라도 있으면 멈춰라.** 노드 연결이 실패해도 에셋 자체는 만들어지므로 파일 존재만으로는 성공 판정이 안 된다.

- [ ] **Step 4: 커밋**

```bash
git add scripts/make_bullet_material.py Content/Materials/M_REBullet.uasset
git commit -m "feat(render): 탄막 머티리얼 M_REBullet 부트스트랩 생성 (#97)"
```

`.uasset` 은 바이너리다. UE 프로젝트에서 정상이며, 스크립트는 재현용 기록으로 함께 남긴다.

---

### Task 2: ISM 에 머티리얼 연결 + 커스텀데이터 슬롯

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletRenderSubsystem.cpp`

**Interfaces:**
- Consumes: Task 1 의 `/Game/Materials/M_REBullet`, 파라미터 이름 `Color`
- Produces: `ISM` 과 `ArcISM` 이 `NumCustomDataFloats == 1` 인 상태. Task 3 이 이 전제 위에서 `SetCustomData(0, M-1, ...)` 를 호출한다.

- [ ] **Step 1: 직선탄 ISM 의 머티리얼 교체**

현재(`REBulletRenderSubsystem.cpp` 42-48행 부근):

```cpp
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dyn = ISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);  // 탄환 빨강
		}
	}
```

이것으로 교체:

```cpp
	// 퍼인스턴스 커스텀데이터 0번 = 스폰 팝(Task 3). 머티리얼이 이 슬롯을 읽는다.
	ISM->SetNumCustomDataFloats(1);

	// 탄막 전용 머티리얼(#97) — 언릿 발광 + 프레넬 림. 겹친 탄 사이 경계가 보이게 한다.
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REBullet.M_REBullet")))
	{
		if (UMaterialInstanceDynamic* Dyn = ISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor::Red);  // 탄환 빨강
		}
	}
	else
	{
		// 조용한 폴백 금지 — 엔진 기본 머티리얼로 떨어지면 림이 사라진 것을 눈치채기 어렵다.
		UE_LOG(LogTemp, Error, TEXT("[RE] M_REBullet 로드 실패 — 탄환 머티리얼 없이 렌더된다 (#97)"));
	}
```

- [ ] **Step 2: 곡사탄 ISM 도 동일하게**

현재(61-67행 부근):

```cpp
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
	{
		if (UMaterialInstanceDynamic* Dyn = ArcISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.5f, 0.f));  // 주황
		}
	}
```

이것으로 교체:

```cpp
	ArcISM->SetNumCustomDataFloats(1);

	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_REBullet.M_REBullet")))
	{
		if (UMaterialInstanceDynamic* Dyn = ArcISM->CreateDynamicMaterialInstance(0, Base))
		{
			Dyn->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.5f, 0.f));  // 주황
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[RE] M_REBullet 로드 실패 — 곡사탄 머티리얼 없이 렌더된다 (#97)"));
	}
```

**마커 ISM(`MarkerISM`)은 건드리지 마라.** 바닥 디스크라 성격이 다르고 스펙 §10 에서 범위 밖으로 명시했다.

- [ ] **Step 3: 빌드**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
Expected: `Result: Succeeded`

- [ ] **Step 4: 머티리얼이 실제로 로드되는지 실행 확인**

Run:
```
.\scripts\profile.ps1 -Bullets 1000 -Label matcheck
Select-String -Path (Get-ChildItem .\Saved\Profiling\RE_Mass_1000_matcheck_* -Directory | Select-Object -Last 1).FullName\run.log -Pattern 'M_REBullet 로드 실패'
```

Expected: **매치 0건** (에러 로그가 안 찍혔다 = 로드 성공). `frames.csv` 도 정상 생성된다.

**에러가 찍히면 멈춰라.** 경로 오타면 탄환이 기본 머티리얼로 렌더되어 이 태스크의 목적이 사라진다.

- [ ] **Step 5: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletRenderSubsystem.cpp
git commit -m "feat(render): 탄환 ISM 에 M_REBullet 연결 + 커스텀데이터 슬롯 1개 (#97)"
```

---

### Task 3: 스폰 팝 커스텀데이터 전달

**Files:**
- Modify: `Source/Project_RE/Mass/REBulletRenderProcessor.cpp`
- Modify: `Source/Project_RE/Mass/REArcRenderProcessor.cpp`

**Interfaces:**
- Consumes: Task 2 가 설정한 `NumCustomDataFloats == 1`
- Produces: 매 프레임 인스턴스별 0~1 스폰 팝 값. 머티리얼의 `PerInstanceCustomData` 0번이 읽는다.

- [ ] **Step 1: 직선탄 — 팝 배열 채우기**

`REBulletRenderProcessor.cpp` 의 수집 루프를 고친다. 현재:

```cpp
	// 1) live 탄환 트랜스폼 수집 (청크를 가로질러 누적 → 전역 인스턴스 인덱스 연속).
	TArray<FTransform> Xf;
	EntityQuery.ForEachEntityChunk(Context, [&Xf](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(BulletScale));  // 탄환 크기 통일
			Xf.Add(B);
		}
	});
```

이것으로 교체:

```cpp
	// 스폰 팝 지속시간(s). 태어난 직후만 밝기가 솟았다가 정상으로 붙는다.
	// 수명 페이드는 넣지 않는다 — 죽기 직전 탄이 흐려지면 여전히 치명적인데 사라지는 중으로
	// 오독되고, 탄막에서 히트박스 가독성은 공정성 문제다 (#97 스펙 §6).
	constexpr float PopDuration = 0.1f;
	const float TotalLife = REBulletPattern::BulletLifetimeSec();

	// 1) live 탄환 트랜스폼 + 스폰 팝 수집 (청크를 가로질러 누적 → 전역 인스턴스 인덱스 연속).
	TArray<FTransform> Xf;
	TArray<float> Pop;
	EntityQuery.ForEachEntityChunk(Context, [&Xf, &Pop, TotalLife](FMassExecutionContext& Ctx)
	{
		const int32 Num = Ctx.GetNumEntities();
		const TConstArrayView<FTransformFragment> T = Ctx.GetFragmentView<FTransformFragment>();
		const TConstArrayView<FBulletSimFragment> S = Ctx.GetFragmentView<FBulletSimFragment>();
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(BulletScale));  // 탄환 크기 통일
			Xf.Add(B);

			// Lifetime 은 잔여시간(REBulletSimProcessor 가 Dt 만큼 감소) → 나이 = 총수명 - 잔여
			const float Age = TotalLife - S[i].Lifetime;
			Pop.Add(FMath::Clamp(Age / PopDuration, 0.f, 1.f));
		}
	});
```

`REBulletPattern::BulletLifetimeSec()` 은 `REBulletPatternGenerator.h` 에 선언돼 있고 이 파일이 이미 그 헤더를 포함한다. `FBulletSimFragment` 는 `REBulletFragments.h` 에 있으며 이 파일이 이미 포함한다.

- [ ] **Step 2: 직선탄 — 커스텀데이터 전송**

같은 파일의 배치 갱신부. 현재:

```cpp
	if (M > 0)
	{
		ISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace=*/true,
			/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	}
```

이것으로 교체:

```cpp
	if (M > 0)
	{
		// 커스텀데이터를 먼저 쓰고 트랜스폼을 나중에 쓴다 — dirty 마크는 트랜스폼 호출이 담당한다.
		ISM->SetCustomData(0, M - 1, Pop, /*bMarkRenderStateDirty=*/false);
		ISM->BatchUpdateInstancesTransforms(0, Xf, /*bWorldSpace=*/true,
			/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	}
```

- [ ] **Step 3: 곡사탄 — 팝 배열 채우기**

`REArcRenderProcessor.cpp` 의 수집 루프. 현재 `BulletXf` 와 `MarkerXf` 를 채우는 곳에 팝을 추가한다. 현재:

```cpp
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(ArcBulletScale));
			BulletXf.Add(B);
```

이것으로 교체 (`BulletXf.Add(B);` 바로 뒤에 한 줄 추가):

```cpp
		for (int32 i = 0; i < Num; ++i)
		{
			FTransform B = T[i].GetTransform();
			B.SetScale3D(FVector(ArcBulletScale));
			BulletXf.Add(B);

			// 곡사탄은 Elapsed 가 곧 나이다(0 에서 시작해 FlightTime 까지 증가).
			BulletPop.Add(FMath::Clamp(A[i].Elapsed / ArcPopDuration, 0.f, 1.f));
```

그리고 람다 캡처 위쪽에 배열과 상수를 선언한다. `TArray<FTransform> BulletXf;` 선언 근처에 추가:

```cpp
	// 스폰 팝 지속시간(s). 직선탄과 같은 값 — 수명 페이드는 넣지 않는다 (#97 스펙 §6).
	constexpr float ArcPopDuration = 0.1f;
	TArray<float> BulletPop;
```

람다 캡처 목록에 `&BulletPop` 을 추가한다.

- [ ] **Step 4: 곡사탄 — 커스텀데이터 전송**

`SyncISM` 람다가 `ArcISM` 과 `MarkerISM` 둘 다에 쓰이므로 **팝은 람다 밖에서 곡사탄 ISM 에만 준다.**

**호출 순서가 중요하다.** `SetCustomData` 는 인스턴스가 이미 존재해야 인덱스 범위가 유효하다(`ensureMsgf` 로 터진다). `SyncISM` 이 인스턴스 수를 맞추므로 그 **뒤**에 호출한다.

현재:

```cpp
	SyncISM(ArcISM, BulletXf);
	SyncISM(MarkerISM, MarkerXf);
```

이것으로 교체:

```cpp
	SyncISM(ArcISM, BulletXf);
	SyncISM(MarkerISM, MarkerXf);

	// 팝은 곡사탄에만. 마커는 바닥 디스크라 커스텀데이터를 쓰지 않는다(#97 스펙 §10).
	// SyncISM 이 인스턴스 수를 맞춘 뒤라야 SetCustomData 의 인덱스 범위가 유효하다.
	// 트랜스폼 배치가 이미 끝나 뒤따르는 플러시가 없으므로 dirty 를 여기서 true 로 준다.
	if (BulletXf.Num() > 0)
	{
		ArcISM->SetCustomData(0, BulletXf.Num() - 1, BulletPop, /*bMarkRenderStateDirty=*/true);
	}
```

**직선탄(Step 2)은 배치 순서가 반대라 dirty 를 `false` 로 준다** — 그쪽은 인스턴스 수 맞추기(`while` 루프)가 `SetCustomData` 보다 먼저 끝나고, 뒤이어 `BatchUpdateInstancesTransforms` 가 dirty 를 마크한다.

- [ ] **Step 5: 빌드**

Run:
```
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REEditor Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
& 'E:\UnrealEngine-5.8\UnrealEngine-5.8\Engine\Build\BatchFiles\Build.bat' Project_REServer Win64 Development -Project="E:\UnrealProjects\Project_RE\Project_RE.uproject" -WaitMutex
```
Expected: 양쪽 `Result: Succeeded`

- [ ] **Step 6: `ensure` 가 안 터지는지 실행 확인**

`SetCustomData` 는 인덱스 범위나 배열 크기가 어긋나면 `ensureMsgf` 로 터진다(`InstancedStaticMesh.cpp:3950/3955`). 조용히 넘어가지 않는다.

Run:
```
.\scripts\profile.ps1 -Bullets 1000 -Label popcheck
$d = (Get-ChildItem .\Saved\Profiling\RE_Mass_1000_popcheck_* -Directory | Select-Object -Last 1).FullName
Select-String -Path "$d\run.log" -Pattern 'Ensure condition failed|CustomDataFloats|InstanceIndexStart'
```

Expected: **매치 0건**, `frames.csv` 정상 생성.

- [ ] **Step 7: 커밋**

```bash
git add Source/Project_RE/Mass/REBulletRenderProcessor.cpp Source/Project_RE/Mass/REArcRenderProcessor.cpp
git commit -m "feat(render): 스폰 팝 퍼인스턴스 커스텀데이터 전달 (#97)"
```

---

### Task 4: 상한 재측정 + 철회 판정 + 문서

**Files:**
- Modify: `docs/profiling/M6-gpu-breakdown.md`

**Interfaces:**
- Consumes: Task 2·3 의 코드 변경
- Produces: 갱신된 60fps 상한 수치. 이 프로젝트의 포트폴리오 문구가 이 숫자를 쓴다.

- [ ] **Step 1: 데디 게이트**

Run:
```
.\scripts\dedi-verify.ps1
```
Expected: 전 항목 PASS, `EXIT=0`

- [ ] **Step 2: 상한 재탐색**

#95 기준 상한은 50,000발(p99 15.00 ms)이었다. 커스텀데이터가 붙었으니 다시 찾는다.

Run:
```
foreach ($n in 40000, 50000, 60000) {
  .\scripts\profile.ps1 -Bullets $n -Label mat | Out-Null
  $d = (Get-ChildItem ".\Saved\Profiling\RE_Mass_${n}_mat_*" -Directory | Select-Object -Last 1)
  .\scripts\profile-stats.ps1 -RunDir $d.FullName | Select-Object -Skip 2 | ForEach-Object {
    $c = $_ -split '\|'
    "  {0,-6} Frame mean={1,-6} p99={2,-6} GT={3,-6} GPU={4}" -f $n,$c[3].Trim(),$c[4].Trim(),$c[5].Trim(),$c[9].Trim()
  }
}
```

Expected: 각 런이 `frames.csv` 를 낸다. **p99 ≤ 16.6 ms 인 최대 탄환 수가 새 상한이다.**

- [ ] **Step 3: 철회 기준 판정**

**새 상한이 45,000발 미만이면 Task 3(스폰 팝)을 되돌린다.**

```bash
git revert --no-edit <Task 3 커밋 해시>
```

되돌린 뒤 Step 2 를 다시 돌려 상한이 50,000발로 복귀하는지 확인하고, 그 사실을 Step 4 문서에 기록한다. 림(Task 1·2)은 유지한다 — 경계 가독성이 본 목적이고 스폰 팝은 부가다.

새 상한이 45,000발 이상이면 그대로 진행한다.

- [ ] **Step 4: 리포트에 기록**

`docs/profiling/M6-gpu-breakdown.md` 에 **부록 B** 를 추가한다. 담을 것:

1. 변경 내용 — 언릿 발광 + 프레넬 림 머티리얼, 스폰 팝 커스텀데이터 float 1개
2. Step 2 의 측정표 (탄환 수 × Frame mean/p99/GT/GPU)
3. #95 상한(50,000발)과의 비교 — 유지됐는지, 깎였다면 얼마나
4. 철회 여부와 그 근거 (Step 3 결과)
5. GPU 가 얼마나 올랐는지 — 머티리얼 비용은 GPU 픽셀 축이므로 그쪽이 오르는 것이 정상이다

**측정하지 않은 값을 쓰지 마라.** 모든 숫자는 Step 2 산출물에서 와야 한다.

- [ ] **Step 5: 커밋**

```bash
git add docs/profiling/M6-gpu-breakdown.md
git commit -m "docs(profiling): 탄막 머티리얼 적용 후 상한 재측정 (부록 B, #97)"
```

---

## 사람 눈 확인 (자동화 불가)

**이 계획의 게이트는 "상한이 안 깎였다" 만 보증한다.** 경계 가독성이 실제로 나아졌는지는 판정할 수 없다 — 저장소에 스크린샷 수단이 없다(`FScreenshotRequest` 계열 0건).

Task 4 까지 끝나면 사람이 게임을 띄워 **겹친 탄 사이 경계가 보이는지** 확인해야 한다. 스펙 §9 가 이 한계를 명시적으로 감수한다.

확인 시 함께 볼 것:
- 림 굵기(`RimPower`) 와 밝기(`RimStrength`) 가 적절한가 — 에디터에서 파라미터로 튜닝 가능
- 스폰 팝이 눈에 띄는가, 혹은 너무 튀는가 (`PopDuration` 상수)

---

## Self-Review

**1. 스펙 커버리지**

| 스펙 절 | 담당 |
|---|---|
| §3 언릿 셰이딩 모델 | Task 1 Step 1 (`MSM_UNLIT`) |
| §4 머티리얼 구조 (Color/Fresnel/CustomData → Emissive) | Task 1 Step 1 |
| §4 불투명 유지 | Task 1 — `MaterialFactoryNew` 기본이 Opaque, 변경하지 않는다 |
| §4 ISM별 MID `Color` 유지 | Task 2 Step 1·2 |
| §5 스크립트 부트스트랩 + 수동 튜닝 | Task 1 (스크립트 주석에 명시, 재실행 시 중단) |
| §6 스폰 팝 float 1개 | Task 2(슬롯) + Task 3(값) |
| §6 수명 페이드 금지 | Task 3 Step 1 주석에 사유 포함 |
| §7 배선 순서 | Task 3 Step 2·4 (직선탄/곡사탄 순서가 다른 이유 명시) |
| §8 철회 기준 45,000발 | Task 4 Step 3 |
| §9 검증 게이트 4종 | Task 2 Step 3·4, Task 3 Step 5·6, Task 4 Step 1·2 |
| §9 사람 눈 확인 한계 | 위 "사람 눈 확인" 절 |
| §10 마커 ISM 제외 | Task 2 Step 2, Task 3 Step 4 |

빠진 요구사항 없음.

**2. 플레이스홀더 스캔**

TBD/TODO 없음. Python 코드는 이 엔진에서 end-to-end 실행 확인된 API 만 쓴다. 모든 검증 단계에 실제 명령과 기대 출력이 있다.

**3. 이름 일관성**

`/Game/Materials/M_REBullet` 경로가 Task 1(생성)·Task 2(로드)에서 일치한다. 파라미터 `Color` / `RimPower` / `RimStrength` 가 Task 1 에서 정의되고 Task 2 가 `Color` 만 쓴다. `PopDuration`(직선탄)과 `ArcPopDuration`(곡사탄)은 **의도적으로 다른 이름**이다 — 유니티 빌드에서 같은 익명 네임스페이스 상수명이 충돌한 전례가 있어(`ActorBulletScale` 주석 참조) 파일별로 구분한다.
