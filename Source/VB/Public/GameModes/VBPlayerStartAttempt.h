// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

class AActor;
class APawn;
class APlayerController;
class UVBCinematicDefinition;
class UVBPlayerReadinessComponent;
class UWorld;

// 플레이어 한 명의 시작 시도. 초기화를 열고 세이브 적용이나 첫 시네마틱으로 보낸 뒤 초기화를 닫는다.
// 세이브 알림이 폰을 바꾸거나 다른 게임을 시작할 수 있어, 시작할 때 본 사실이 그대로일 때만 초기화를 닫는다.
class VB_API FVBPlayerStartAttempt
{
public:
	// Mode 는 이 시도를 연 GameMode 다. 시도는 그 함수 안에서만 산다.
	FVBPlayerStartAttempt(const AActor& Mode, APlayerController* NewPlayer, UVBSaveGameSubsystem* Save);

	void Run(bool bPlayInitialCinematic, UVBCinematicDefinition* InitialCinematic);
	// 세이브 시도, 월드, 폰, 컨트롤러가 시작할 때 본 그대로면 true.
	bool IsCurrent() const;

private:
	// 첫 시네마틱을 시작했으면 true. 이때 초기화는 시네마틱 정책이 닫는다.
	bool TryStartInitialCinematic(bool bPlayInitialCinematic, UVBCinematicDefinition* InitialCinematic);
	// Continue 세이브를 저장 참여자에게 건네고 초기화를 닫는다.
	void ApplyContinueSave();
	void CompleteIfCurrent(bool bContinueDispatched);

	const AActor& Mode;
	FVBGameStartContext Start;
	TWeakObjectPtr<UVBSaveGameSubsystem> Save;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<UVBPlayerReadinessComponent> Readiness;
	TWeakObjectPtr<UWorld> World;
};
