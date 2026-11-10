// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/EngineBaseTypes.h"
#include "VBGameInstance.generated.h"

class UNetDriver;
class UVBOnlineSession;
class UWorld;

/** 네트워크 실패와 이동 실패가 이 GameInstance 의 월드에서 났을 때 알린다. 실패 종류와 엔진이 준 문장이 같이 오고, 네트워크 실패는 서버 쪽에서 났는지도 온다. */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FVBNetworkFailureEvent, ENetworkFailure::Type /*Failure*/, const FString& /*Error*/, bool /*bIsServer*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBTravelFailureEvent, ETravelFailure::Type /*Failure*/, const FString& /*Error*/);

/**
 * 프로젝트 GameInstance. 엔진이 블루프린트에만 여는 실패 알림(HandleNetworkError, HandleTravelError)을
 * 엔진 이벤트 구독으로 받아 다시 알리고, 세션을 떠나는 문인 UVBOnlineSession 을 쓰게 한다.
 * 실패 때 할 행동은 구독자가 정한다. 패드 연결 변화는 기반 클래스의 OnInputDeviceConnectionChange 를 구독한다.
 */
UCLASS()
class VB_API UVBGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual TSubclassOf<UOnlineSession> GetOnlineSessionClass() override;

	// 커맨드릿에는 세션이 없어 null 이다.
	UVBOnlineSession* GetVBOnlineSession() const;

	FVBNetworkFailureEvent OnNetworkFailureObserved;
	FVBTravelFailureEvent OnTravelFailureObserved;

private:
	void HandleEngineNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Failure, const FString& Error);
	void HandleEngineTravelFailure(UWorld* World, ETravelFailure::Type Failure, const FString& Error);

	// 엔진 이벤트는 같은 프로세스의 모든 GameInstance(PIE 여러 개)에 방송된다. 내 것만 받는다.
	//   접속 중 실패는 World 가 비어 오므로 그때는 NetDriver 가 내 월드 컨텍스트의 접속 중 것인지로 가린다.
	bool OwnsFailure(const UWorld* World, const UNetDriver* NetDriver) const;

	// 막혀 있으면 팝업이 이미 떠 있고, 오는 실패는 엔진이 매 프레임 다시 알리는 같은 끊김이다.
	bool IsDisconnectHeld() const;

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
