// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Connection/VBConnectionFeedbackSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "GameModes/VBGameInstance.h"
#include "GameModes/VBOnlineSession.h"
#include "UI/Notice/VBNoticeSubsystem.h"
#include "UI/VBUIMessagingSubsystem.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBConnectionFeedback"

bool UVBConnectionFeedbackSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// UVBGameInstance 의 실패 알림을 듣는 서브시스템이다. 다른 GameInstance 아래에는 들을 곳이 없다. 설정은 시험이 지킨다.
	const ULocalPlayer* LocalPlayer = Cast<ULocalPlayer>(Outer);
	return LocalPlayer && Cast<UVBGameInstance>(LocalPlayer->GetGameInstance()) && Super::ShouldCreateSubsystem(Outer);
}

void UVBConnectionFeedbackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 반응에 쓰는 두 UI 서브시스템을 먼저 만든다 - 이 서브시스템이 실패를 받는 순간 바로 쓸 수 있어야 한다.
	Collection.InitializeDependency<UVBUIMessagingSubsystem>();
	Collection.InitializeDependency<UVBNoticeSubsystem>();

	UVBGameInstance* GameInstance = CastChecked<UVBGameInstance>(GetLocalPlayer()->GetGameInstance());
	Owner = GameInstance;
	NetworkHandle = GameInstance->OnNetworkFailureObserved.AddUObject(this, &ThisClass::HandleNetworkFailure);
	TravelHandle = GameInstance->OnTravelFailureObserved.AddUObject(this, &ThisClass::HandleTravelFailure);
	GameInstance->OnInputDeviceConnectionChange.AddDynamic(this, &ThisClass::HandleDeviceConnection);
	VB_LOG(Log, "[연결 알림] 구독 시작: %s", *GetNameSafe(GetLocalPlayer()));
}

void UVBConnectionFeedbackSubsystem::Deinitialize()
{
	ReleaseDisconnect();
	if (UVBGameInstance* GameInstance = Owner.Get())
	{
		GameInstance->OnNetworkFailureObserved.Remove(NetworkHandle);
		GameInstance->OnTravelFailureObserved.Remove(TravelHandle);
		GameInstance->OnInputDeviceConnectionChange.RemoveDynamic(this, &ThisClass::HandleDeviceConnection);
	}
	Owner.Reset();
	Super::Deinitialize();
}

void UVBConnectionFeedbackSubsystem::HandleNetworkFailure(ENetworkFailure::Type /*Failure*/, const FString& /*Error*/, bool bIsServer)
{
	React(GetDefault<UVBGameFlowSettings>()->GetNetworkFailureReaction(bIsServer),
		LOCTEXT("NetworkHeader", "연결 끊김"),
		LOCTEXT("NetworkBody", "서버와의 연결이 끊겼습니다."),
		TEXT("Connection.Network"));
}

void UVBConnectionFeedbackSubsystem::HandleTravelFailure(ETravelFailure::Type /*Failure*/, const FString& /*Error*/)
{
	React(GetDefault<UVBGameFlowSettings>()->TravelFailureReaction,
		LOCTEXT("TravelHeader", "이동 실패"),
		LOCTEXT("TravelBody", "다음 장소로 이동하지 못했습니다."),
		TEXT("Connection.Travel"));
}

void UVBConnectionFeedbackSubsystem::HandleDeviceConnection(EInputDeviceConnectionState NewState, FPlatformUserId User, FInputDeviceId /*Device*/)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	// 끊김만, 그리고 이 로컬 플레이어의 장치만 다룬다.
	if (NewState != EInputDeviceConnectionState::Disconnected || !LocalPlayer || User != LocalPlayer->GetPlatformUserId())
	{
		return;
	}
	React(GetDefault<UVBGameFlowSettings>()->PadDisconnectedReaction,
		LOCTEXT("PadHeader", "컨트롤러 끊김"),
		LOCTEXT("PadBody", "컨트롤러 연결이 끊겼습니다."),
		TEXT("Connection.Pad"));
}

void UVBConnectionFeedbackSubsystem::React(EVBConnectionReaction Reaction, const FText& Header, const FText& Body, FName MergeKey)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (Reaction == EVBConnectionReaction::None || !LocalPlayer)
	{
		return;
	}

	if (Reaction == EVBConnectionReaction::Notice)
	{
		if (UVBNoticeSubsystem* Notice = LocalPlayer->GetSubsystem<UVBNoticeSubsystem>())
		{
			Notice->PushText(EVBNoticeCategory::System, Body, MergeKey);
		}
		return;
	}

	UVBUIMessagingSubsystem* Messaging = LocalPlayer->GetSubsystem<UVBUIMessagingSubsystem>();
	if (!Messaging)
	{
		// 이동을 막지 않았으므로 이동이 따르는 실패라면 엔진이 그대로 메뉴로 옮긴다.
		VB_LOG(Error, "[연결 알림] 확인 팝업 서브시스템이 없다 - %s", *Body.ToString());
		return;
	}

	// 확인 버튼 하나만 둔다.
	FVBConfirmationRequest Request;
	Request.Header = Header;
	Request.Body = Body;
	Request.bOfferDecline = false;
	if (Reaction == EVBConnectionReaction::PopupThenMenu)
	{
		ShowThenLeave(*Messaging, Request);
		return;
	}
	Messaging->ShowConfirmation(Request, FVBConfirmationResultDelegate());
}

void UVBConnectionFeedbackSubsystem::ShowThenLeave(UVBUIMessagingSubsystem& Messaging, const FVBConfirmationRequest& Request)
{
	UVBGameInstance* GameInstance = Owner.Get();
	UVBOnlineSession* Session = GameInstance ? GameInstance->GetVBOnlineSession() : nullptr;
	if (!Session)
	{
		VB_LOG(Error, "[연결 알림] 세션이 없어 메뉴 이동을 막지 못한다 - %s", *Request.Body.ToString());
		return;
	}
	if (!Session->TryHoldDisconnect())
	{
		return;
	}
	bHoldsDisconnect = true;

	// 확인뿐 아니라 닫힘과 실패에서도 떠난다 - 끊긴 세션에 남기지 않는다.
	Messaging.ShowConfirmation(Request, FVBConfirmationResultDelegate::CreateWeakLambda(this, [this](EVBConfirmationResult)
	{
		ReleaseDisconnect();
		UVBGameInstance* Instance = Owner.Get();
		if (Instance && Instance->GetWorld())
		{
			Instance->ReturnToMainMenu();
		}
	}));
}

void UVBConnectionFeedbackSubsystem::ReleaseDisconnect()
{
	if (!bHoldsDisconnect)
	{
		return;
	}
	bHoldsDisconnect = false;
	UVBGameInstance* GameInstance = Owner.Get();
	if (UVBOnlineSession* Session = GameInstance ? GameInstance->GetVBOnlineSession() : nullptr)
	{
		Session->ReleaseDisconnect();
	}
}

#undef LOCTEXT_NAMESPACE
