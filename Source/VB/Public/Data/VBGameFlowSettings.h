// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBGameFlowSettings.generated.h"

/** 접속 실패, 이동 실패, 패드 끊김 때 화면이 보이는 반응. */
UENUM(BlueprintType)
enum class EVBConnectionReaction : uint8
{
	None,
	Notice,
	Popup,
	// 플레이어가 확인할 때까지 엔진의 메뉴 이동을 막는다. 나머지 값은 막지 않아 이동이 따르는 실패에서는 화면이 바로 넘어간다.
	PopupThenMenu,
};

/**
 * 게임 흐름의 프로젝트 전역 설정(Project Settings > Game > VB Game Flow).
 * 프로젝트에 하나뿐인 사실이라 소비자마다 가리키는 DataAsset 대신 GetDefault 로 닿는 설정에 둔다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Game Flow"))
class VB_API UVBGameFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 새 게임 시작 또는 세이브 로드 후 이동할 게임플레이 레벨.
	// UVBSaveGameSubsystem::TravelToGameplayLevel() 로만 읽는다 - 직접 OpenLevel 하지 않는다.
	UPROPERTY(config, EditAnywhere, Category="VB|Game Flow")
	TSoftObjectPtr<UWorld> GameplayLevel;

	// 서버와의 접속이 끊기거나 접속에 실패했을 때(클라이언트). 호스트 쪽 실패는 아래 서버 반응이 받는다.
	UPROPERTY(config, EditAnywhere, Category="VB|Game Flow|Connection")
	EVBConnectionReaction ClientNetworkFailureReaction = EVBConnectionReaction::PopupThenMenu;

	// 호스트(서버) 쪽 네트워크 실패. 나간 사람의 AI 대체는 GameMode 쪽 별도 기능이라 여기서는 반응하지 않는다.
	UPROPERTY(config, EditAnywhere, Category="VB|Game Flow|Connection")
	EVBConnectionReaction ServerNetworkFailureReaction = EVBConnectionReaction::None;

	UPROPERTY(config, EditAnywhere, Category="VB|Game Flow|Connection")
	EVBConnectionReaction TravelFailureReaction = EVBConnectionReaction::PopupThenMenu;

	UPROPERTY(config, EditAnywhere, Category="VB|Game Flow|Connection")
	EVBConnectionReaction PadDisconnectedReaction = EVBConnectionReaction::Notice;

	// 네트워크 실패가 서버 쪽에서 났는지에 따라 반응을 고른다.
	EVBConnectionReaction GetNetworkFailureReaction(bool bIsServer) const
	{
		return bIsServer ? ServerNetworkFailureReaction : ClientNetworkFailureReaction;
	}
};
