// Copyright Epic Games, Inc. All Rights Reserved.

#include "REGameMode.h"
#include "RECharacterBase.h"
#include "REPlayerController.h"

AREGameMode::AREGameMode()
{
	DefaultPawnClass = ARECharacterBase::StaticClass();
	PlayerControllerClass = AREPlayerController::StaticClass();
}
