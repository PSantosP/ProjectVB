// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Data/VBGameFlowSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameModes/VBGameInstance.h"
#include "GameModes/VBOnlineSession.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/VBUIMessagingSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace VBConnectionTests
{
	// 시험 프로세스의 실제 게임 월드. 엔진의 진짜 이동 경로를 보려고 쓴다.
	FWorldContext* FindGameContext()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World() && Cast<UVBGameInstance>(Context.OwningGameInstance))
			{
				return GEngine->GetWorldContextFromWorld(Context.World());
			}
		}
		return nullptr;
	}

	// 시험이 건 이동을 그 자리에서 치운다. 다음 프레임에 프로세스가 실제로 맵을 옮기면 뒤따르는 시험이 깨진다.
	bool TakeTravel(FWorldContext& Context)
	{
		const bool bClosed = Context.TravelURL == TEXT("?closed");
		Context.TravelURL.Empty();
		return bClosed;
	}
}

// 네트워크 실패 반응은 서버 쪽인지 클라이언트 쪽인지로 갈린다. 설정 값이 바뀌면 반응도 따라 바뀌어야 한다(반대 대조).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBNetworkFailureReactionSplitTest, "VB.Unit.Connection.NetworkFailureReactionSplit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBNetworkFailureReactionSplitTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UVBGameFlowSettings> Settings(NewObject<UVBGameFlowSettings>());
	Settings->ClientNetworkFailureReaction = EVBConnectionReaction::PopupThenMenu;
	Settings->ServerNetworkFailureReaction = EVBConnectionReaction::None;
	TestEqual(TEXT("client side follows the client value"), Settings->GetNetworkFailureReaction(false), EVBConnectionReaction::PopupThenMenu);
	TestEqual(TEXT("server side follows the server value"), Settings->GetNetworkFailureReaction(true), EVBConnectionReaction::None);

	// 반대 대조: 두 값을 맞바꾸면 반응도 맞바뀐다 - 서버 여부를 무시하는 구현이면 둘 다 같은 값이라 여기서 걸린다.
	Settings->ClientNetworkFailureReaction = EVBConnectionReaction::None;
	Settings->ServerNetworkFailureReaction = EVBConnectionReaction::Popup;
	TestEqual(TEXT("swapped: client side is now None"), Settings->GetNetworkFailureReaction(false), EVBConnectionReaction::None);
	TestEqual(TEXT("swapped: server side is now Popup"), Settings->GetNetworkFailureReaction(true), EVBConnectionReaction::Popup);
	return true;
}

// 사용자가 정한 기본 반응:클라이언트 네트워크 실패와 이동 실패 = 확인 전까지 화면 유지 뒤 메뉴, 패드 끊김 = 안내만, 서버 쪽 = 반응 없음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConnectionReactionDefaultsTest, "VB.Unit.Connection.ReactionDefaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBConnectionReactionDefaultsTest::RunTest(const FString& Parameters)
{
	// 프로젝트 설정(DefaultGame.ini 가 덮어쓴 값 포함)이 사용자 결정과 같은지 본다. 설정을 바꾸면 여기서 걸리게 두어 결정이 바뀐 것을 알린다.
	const UVBGameFlowSettings* Code = GetDefault<UVBGameFlowSettings>();
	if (!TestNotNull(TEXT("settings default exists"), Code))
	{
		return false;
	}
	TestEqual(TEXT("client network failure"), Code->ClientNetworkFailureReaction, EVBConnectionReaction::PopupThenMenu);
	TestEqual(TEXT("server network failure"), Code->ServerNetworkFailureReaction, EVBConnectionReaction::None);
	TestEqual(TEXT("travel failure"), Code->TravelFailureReaction, EVBConnectionReaction::PopupThenMenu);
	TestEqual(TEXT("pad disconnected"), Code->PadDisconnectedReaction, EVBConnectionReaction::Notice);
	return true;
}

// 연결 알림 서브시스템은 UVBGameInstance 아래에서만 생긴다. 설정이 다른 클래스를 가리키면 알림이 조용히 사라지므로 여기서 막는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConnectionGameInstanceClassTest, "VB.Unit.Connection.ProjectGameInstance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBConnectionGameInstanceClassTest::RunTest(const FString& Parameters)
{
	FString Configured;
	const bool bFound = GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameInstanceClass"), Configured, GEngineIni);
	TestTrue(TEXT("GameInstanceClass is configured"), bFound);
	TestEqual(TEXT("project uses UVBGameInstance"), Configured, UVBGameInstance::StaticClass()->GetPathName());
	// 반대 대조: 엔진 기본 클래스였다면 위 비교가 걸린다.
	TestNotEqual(TEXT("the engine default is a different class"), UGameInstance::StaticClass()->GetPathName(), UVBGameInstance::StaticClass()->GetPathName());
	return true;
}

// 막는 문의 전부다. 막지 않았을 때 실제로 이동이 걸려야(대조) 막혔다는 말이 뜻을 가진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConnectionSessionHoldTest, "VB.Unit.Connection.SessionHoldsDisconnect",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBConnectionSessionHoldTest::RunTest(const FString& Parameters)
{
	FWorldContext* Context = VBConnectionTests::FindGameContext();
	if (!TestNotNull(TEXT("the test process has a game world"), Context))
	{
		return false;
	}

	TStrongObjectPtr<UVBOnlineSession> Session(NewObject<UVBOnlineSession>());
	Session->HandleDisconnect(Context->World(), nullptr);
	TestTrue(TEXT("control: without a hold the engine travels"), VBConnectionTests::TakeTravel(*Context));

	TestTrue(TEXT("the first hold succeeds"), Session->TryHoldDisconnect());
	TestFalse(TEXT("a second hold is refused"), Session->TryHoldDisconnect());
	Session->HandleDisconnect(Context->World(), nullptr);
	TestFalse(TEXT("held: the engine does not travel"), VBConnectionTests::TakeTravel(*Context));

	Session->ReleaseDisconnect();
	Session->HandleDisconnect(Context->World(), nullptr);
	TestTrue(TEXT("released: the engine travels"), VBConnectionTests::TakeTravel(*Context));
	return true;
}

// 엔진이 먼저 구독하고 우리가 나중에 구독한다. 우리 구독이 먼저 돌아야 HandleDisconnect 전에 막을 수 있다.
// 계약이 아니라 Broadcast 구현이라, 엔진을 올릴 때 바뀌면 여기서 알린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConnectionFailureOrderTest, "VB.Unit.Connection.FailureOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBConnectionFailureOrderTest::RunTest(const FString& Parameters)
{
	TMulticastDelegate<void()> Event;
	TArray<FString> Calls;
	Event.AddLambda([&Calls] { Calls.Add(TEXT("engine")); });
	Event.AddLambda([&Calls] { Calls.Add(TEXT("game")); });
	Event.Broadcast();
	TestEqual(TEXT("the later binding runs first"), FString::Join(Calls, TEXT(",")), FString(TEXT("game,engine")));
	return true;
}

// 끊긴 연결은 막힌 동안 엔진이 매 프레임 다시 알린다. 두 프로세스 실측에서 32초에 7954번 왔다. 구독자에게는 처음 한 번만 간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConnectionRepeatedFailureTest, "VB.Unit.Connection.RepeatedFailureDropped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBConnectionRepeatedFailureTest::RunTest(const FString& Parameters)
{
	FWorldContext* Context = VBConnectionTests::FindGameContext();
	if (!TestNotNull(TEXT("the test process has a game world"), Context))
	{
		return false;
	}

	UVBGameInstance* GameInstance = Cast<UVBGameInstance>(Context->OwningGameInstance);
	UVBOnlineSession* Session = GameInstance->GetVBOnlineSession();
	if (!TestNotNull(TEXT("the project session is in use"), Session))
	{
		return false;
	}

	// NetDriver 없이 부르면 엔진 처리기는 바로 돌아가고 서버 쪽 실패로 보여 팝업도 뜨지 않는다.
	AddExpectedError(TEXT("BroadcastNetworkFailure"), EAutomationExpectedErrorFlags::Contains, 2);
	int32 Heard = 0;
	const FDelegateHandle Handle = GameInstance->OnNetworkFailureObserved.AddLambda(
		[&Heard](ENetworkFailure::Type, const FString&, bool) { ++Heard; });

	GEngine->BroadcastNetworkFailure(Context->World(), nullptr, ENetworkFailure::ConnectionLost, TEXT("test"));
	TestEqual(TEXT("control: without a hold subscribers hear the failure"), Heard, 1);

	TestTrue(TEXT("the hold is taken"), Session->TryHoldDisconnect());
	GEngine->BroadcastNetworkFailure(Context->World(), nullptr, ENetworkFailure::ConnectionLost, TEXT("test"));
	TestEqual(TEXT("held: the repeat is dropped"), Heard, 1);

	Session->ReleaseDisconnect();
	GameInstance->OnNetworkFailureObserved.Remove(Handle);
	return true;
}

// 실제 UVBGameInstance 와 로컬 플레이어로 끝까지 돈다. 두 실패 모두 팝업 하나를 띄워 이동을 막고, 팝업이 닫혀야 떠난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConnectionPopupThenMenuTest, "VB.Unit.Connection.PopupThenMenuFlow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FVBConnectionPopupThenMenuTest::RunTest(const FString& Parameters)
{
	FWorldContext* Context = VBConnectionTests::FindGameContext();
	if (!TestNotNull(TEXT("the test process has a game world"), Context))
	{
		return false;
	}

	UVBGameInstance* GameInstance = Cast<UVBGameInstance>(Context->OwningGameInstance);
	ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
	UVBOnlineSession* Session = GameInstance->GetVBOnlineSession();
	UVBUIMessagingSubsystem* Messaging = LocalPlayer ? LocalPlayer->GetSubsystem<UVBUIMessagingSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("the project session is in use"), Session) || !TestNotNull(TEXT("messaging exists"), Messaging))
	{
		return false;
	}

	const auto CheckFlow = [&](const TCHAR* What, TFunctionRef<void()> Fail)
	{
		Fail();
		TestTrue(FString::Printf(TEXT("%s: travel is held"), What), Session->IsDisconnectHeld());
		Session->HandleDisconnect(Context->World(), nullptr);
		TestFalse(FString::Printf(TEXT("%s: the engine call does not travel"), What), VBConnectionTests::TakeTravel(*Context));

		FVBConfirmationHandle Popup;
		const FVBConfirmationRequest* Presented = Messaging->GetPresented(Popup);
		if (!TestNotNull(FString::Printf(TEXT("%s: a popup is up"), What), Presented))
		{
			Session->ReleaseDisconnect();
			return;
		}
		TestFalse(FString::Printf(TEXT("%s: confirm is the only button"), What), Presented->bOfferDecline);

		Fail();
		Messaging->Cancel(Popup);
		TestFalse(FString::Printf(TEXT("%s: closing releases the hold"), What), Session->IsDisconnectHeld());
		TestTrue(FString::Printf(TEXT("%s: closing leaves the session"), What), VBConnectionTests::TakeTravel(*Context));

		FVBConfirmationHandle Next;
		TestNull(FString::Printf(TEXT("%s: the repeated failure added no popup"), What), Messaging->GetPresented(Next));
	};

	CheckFlow(TEXT("network"), [&] { GameInstance->OnNetworkFailureObserved.Broadcast(ENetworkFailure::ConnectionLost, TEXT("test"), false); });
	CheckFlow(TEXT("travel"), [&] { GameInstance->OnTravelFailureObserved.Broadcast(ETravelFailure::PackageMissing, TEXT("test")); });
	return true;
}

#endif
