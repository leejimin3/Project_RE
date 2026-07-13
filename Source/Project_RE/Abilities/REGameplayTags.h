// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

// 대쉬 관련 네이티브 게임플레이 태그 (ini 편집 없이 C++로 정의)
UE_DECLARE_GAMEPLAY_TAG_EXTERN(RETag_Cooldown_Dash); // Cooldown.Dash — 대쉬 쿨다운 중
UE_DECLARE_GAMEPLAY_TAG_EXTERN(RETag_State_Dashing); // State.Dashing — 대쉬 이동 중 (#27 무적판정이 읽음)
