// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VBSaveParticipant.generated.h"

class UVBSaveGame;
class UWorld;
class APawn;
class APlayerController;
class APlayerState;

/**
 * 저장, 적용 한 번의 문맥. 저장 서브시스템이 만들어 모든 참여자에게 같은 것을 준다.
 * 리슨 서버에서는 PlayerState 가 여럿이라, 플레이어의 것을 쓰는 참여자는 문맥의 PlayerState 가 자기인지 보고 움직인다.
 */
struct FVBSaveContext
{
	UWorld* World = nullptr;
	APawn* Pawn = nullptr;
	APlayerController* PlayerController = nullptr;
	APlayerState* PlayerState = nullptr;

	// 체크포인트: 위치 구역이 폰의 지금 자리 대신 이 자리를 쓴다. 비면 지금 자리.
	TOptional<FTransform> RestoreTransformOverride;
};

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UVBSaveParticipant : public UInterface
{
	GENERATED_BODY()
};

/**
 * 게임 슬롯에 자기 구역을 쓰고 읽는 것. 참여자가 UVBSaveParticipantSubsystem 에 스스로 등록해 저장 쪽은 누가 있는지 모른다.
 * 적용이 BeginPlay 보다 먼저 돌므로 BeginPlay 보다 이른 자리(InitializeComponent 등)에서 등록한다.
 * 서버에서만 불리고, 순서에 기대지 않게 자기 구역만 건드린다.
 */
class VB_API IVBSaveParticipant
{
	GENERATED_BODY()

public:
	// 저장 직전. Save 의 자기 구역을 지금 상태로 채운다.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) = 0;

	// 불러온 세이브를 적용할 때(Continue). 새 게임이면 불리지 않는다.
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) = 0;
};
