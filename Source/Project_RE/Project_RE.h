// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Main log category used across the project */
DECLARE_LOG_CATEGORY_EXTERN(LogProject_RE, Log, All);

/**
 *  로그 카테고리를 **서브시스템 경계와 일치시킨다.** 검증 스크립트가 그 경계로 필터하기 때문이다.
 *
 *  전에는 77곳이 전부 LogTemp 였다. LogTemp 는 엔진 전체가 같이 쓰는 카테고리라
 *  `-LogCmds="LogTemp Verbose"` 가 곧 **엔진 전체 Verbose** 였다 — 대쉬 무적 판정 로그
 *  하나를 보려고 수만 줄을 뒤져야 했다. 이제 `-LogCmds="LogREBullet Verbose"` 로 딱 그것만 켠다.
 *
 *  기본 verbosity `Log`, 컴파일 상한 `All` — LogTemp 와 동일하게 맞춘다.
 *  다르게 잡으면 기존 Verbose 로그가 조용히 사라진다.
 *
 *  **메시지 본문과 `[RE]` 접두어는 카테고리 도입으로 바뀌지 않는다.**
 *  scripts/dedi-verify.ps1 과 헤드리스 프로브가 그 문자열을 grep 해 PASS/FAIL 을 낸다.
 */
/** 게임루프 공통 — GameMode / CharacterBase / PlayerController / 보스 페이즈·어빌리티. */
DECLARE_LOG_CATEGORY_EXTERN(LogRE, Log, All);
/** Mass 탄막 — Sim / Render / Hit / Arc / Fx / SpawnSubsystem / Actor 비교군. */
DECLARE_LOG_CATEGORY_EXTERN(LogREBullet, Log, All);
/** 복제·RPC 경로 — Multicast 구현, 서버 RPC, 헤드리스 프로브. */
DECLARE_LOG_CATEGORY_EXTERN(LogRENet, Log, All);
