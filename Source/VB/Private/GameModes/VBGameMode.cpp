// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "GameModes/VBGameMode.h"
#include "GameModes/VBPlayerStartAttempt.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

AVBGameMode::AVBGameMode()
{
	// 기본 클래스 지정 BP에서 오버라이드 가능하게
}

void AVBGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this))
	{
		Save->EnsureDirectStartContext();
	}
}

void AVBGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// 기본 스폰(PlayerStart 배치 + Possess)을 먼저 수행 - 이 시점에 폰이 존재하고 PossessedBy 의 GAS 초기화가 끝난다.
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	FVBPlayerStartAttempt Attempt(*this, NewPlayer, UVBSaveGameSubsystem::Get(this));
	Attempt.Run(bPlayInitialNewGameCinematic, InitialNewGameCinematic);
}
