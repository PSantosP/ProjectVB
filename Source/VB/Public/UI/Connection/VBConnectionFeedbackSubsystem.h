// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/VBGameFlowSettings.h"
#include "Engine/EngineBaseTypes.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "VBConnectionFeedbackSubsystem.generated.h"

class UVBGameInstance;
class UVBUIMessagingSubsystem;
struct FVBConfirmationRequest;

/**
 * 접속 실패, 이동 실패, 패드 끊김을 화면에 알린다(로컬 플레이어마다 하나).
 * UVBGameInstance 의 실패 알림을 듣고, 어떻게 반응할지는 UVBGameFlowSettings 의 반응 값이 정한다.
 * 로컬 플레이어는 GameInstance 가 시작할 때 만들어져 맵 이동을 건너 살므로, 실패가 나기 전에 항상 붙어 있다.
 */
UCLASS()
class VB_API UVBConnectionFeedbackSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void HandleNetworkFailure(ENetworkFailure::Type Failure, const FString& Error, bool bIsServer);
	void HandleTravelFailure(ETravelFailure::Type Failure, const FString& Error);

	UFUNCTION()
	void HandleDeviceConnection(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId Device);

	// 반응 하나를 실행한다. Popup 계열은 확인 팝업을 띄운다.
	void React(EVBConnectionReaction Reaction, const FText& Header, const FText& Body, FName MergeKey);

	// PopupThenMenu. 엔진의 메뉴 이동을 막고 팝업을 띄운 뒤, 팝업이 닫히면 세션을 떠난다.
	void ShowThenLeave(UVBUIMessagingSubsystem& Messaging, const FVBConfirmationRequest& Request);
	void ReleaseDisconnect();

	TWeakObjectPtr<UVBGameInstance> Owner;
	FDelegateHandle NetworkHandle;
	FDelegateHandle TravelHandle;
	// 해체 때 대화 상자 콜백은 조용히 사라질 수 있어, 막은 쪽이 직접 푼다.
	bool bHoldsDisconnect = false;
};
