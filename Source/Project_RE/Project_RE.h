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

/**
 *  이 모듈의 **실패 처리 규약** (#141 R-03).
 *
 *  전에는 check / checkf / ensure / ensureMsgf 가 14,257 LOC 전체에서 **0건**이었다.
 *  그게 문제인 이유는 방어가 적어서가 아니라, **무엇이 불변식이고 무엇이 정상 폴백인지
 *  코드가 말하지 않기 때문**이다. 같은 파일 안에서 한 줄은 GetWorld() 를 막고 한 줄은
 *  안 막고 있었고, 어느 쪽이 의도인지 읽는 사람이 알 방법이 없었다.
 *
 *  1) **정상적으로 발생 가능한 부재** → if 폴백 + UE_LOG. 예외가 아니라 설계된 경로다.
 *     (데디에 ISM 없음 / 폰 없음 / 전원 사망 / 클라에는 AuthGameMode 없음)
 *     여기에 ensure 를 달면 데디 서버에서 매 발사마다 오탐이 뜬다.
 *
 *  2) **프로그래머 실수로만 발생** → ensureMsgf + 조기 반환.
 *     등록된 컴포넌트·소유된 액터에 월드가 없는 상황이 여기다.
 *     개발 빌드에선 콜스택 + 메시지가 남고, 쉬핑에선 컴파일 아웃되어 조기 반환만 남는다.
 *     그래서 ensureMsgf 는 **반드시 조기 반환과 짝**이어야 한다 — 쉬핑에서 그 반환이
 *     유일한 방어가 되기 때문이다.
 *
 *  3) **진행하면 데이터가 깨짐** → checkf. 현재 이 모듈엔 해당 경로가 없다.
 *     억지로 만들지 않는다.
 *
 *  금지 ①: **로그 없는 early-return.** 조용한 실패가 이 프로젝트의 측정을 두 번 망쳤다
 *          (#50, #88). 부류 1의 폴백에는 반드시 로그를 단다.
 *  금지 ②: **Mass 프로세서의 워커 스레드 경로에서 ensure.** 발화 순서가 보장되지 않는다.
 *          해당 프로세서: REBulletSimProcessor, REArcSimProcessor
 *          (bRequiresGameThreadExecution = false). 거기서는 if 폴백만 쓴다.
 *  금지 ③: **ensureAlways.** ensure 는 기본이 1회 발화다. 매 프레임 도는 경로에서
 *          ensureAlways 를 쓰면 첫 실패 후 로그가 프레임마다 쏟아진다.
 */
