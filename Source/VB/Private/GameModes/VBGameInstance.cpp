// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "GameModes/VBGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameModes/VBOnlineSession.h"
#include "VB/VB.h"

void UVBGameInstance::Init()
{
	// 서브시스템은 Super::Init 안에서 모두 만들어진다. 구독은 그 뒤에 붙여야 첫 실패를 받는 쪽이 준비돼 있다.
	Super::Init();
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleEngineNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleEngineTravelFailure);
	}
}

void UVBGameInstance::Shutdown()
{
	// 붙인 것과 같은 이벤트에서 같은 핸들로 뗀다. 서브시스템이 해체되기 전에 먼저 끊는다.
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
	Super::Shutdown();
}

TSubclassOf<UOnlineSession> UVBGameInstance::GetOnlineSessionClass()
{
	return UVBOnlineSession::StaticClass();
}

UVBOnlineSession* UVBGameInstance::GetVBOnlineSession() const
{
	return Cast<UVBOnlineSession>(GetOnlineSession());
}

bool UVBGameInstance::OwnsFailure(const UWorld* World, const UNetDriver* NetDriver) const
{
	if (World)
	{
		return World->GetGameInstance() == this;
	}
	// 월드 없이 난 실패 - 엔진도 같은 방식으로 소유한 GameInstance 를 찾는다(UEngine::HandleNetworkFailure_NotifyGameInstance).
	if (!NetDriver || !GEngine)
	{
		return false;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.OwningGameInstance == this && Context.PendingNetGame && Context.PendingNetGame->NetDriver == NetDriver)
		{
			return true;
		}
	}
	return false;
}

bool UVBGameInstance::IsDisconnectHeld() const
{
	const UVBOnlineSession* Session = GetVBOnlineSession();
	return Session && Session->IsDisconnectHeld();
}

void UVBGameInstance::HandleEngineNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type Failure, const FString& Error)
{
	if (!OwnsFailure(World, NetDriver) || IsDisconnectHeld())
	{
		return;
	}
	// 서버 판정은 엔진(HandleNetworkFailure_NotifyGameInstance)과 같다 - NetDriver 가 없으면 서버로 본다.
	const bool bIsServer = !NetDriver || NetDriver->GetNetMode() != NM_Client;
	VB_LOG(Warning, "네트워크 실패: %s - %s (%s)", ENetworkFailure::ToString(Failure), *Error, bIsServer ? TEXT("서버") : TEXT("클라이언트"));
	OnNetworkFailureObserved.Broadcast(Failure, Error, bIsServer);
}

void UVBGameInstance::HandleEngineTravelFailure(UWorld* World, ETravelFailure::Type Failure, const FString& Error)
{
	// 이동 실패에는 NetDriver 가 없다 - 월드가 비어 오면 어느 GameInstance 의 실패인지 알 수 없어 알리지 않는다.
	if (!OwnsFailure(World, nullptr))
	{
		VB_LOG(Log, "이 GameInstance 의 것이 아닌 이동 실패를 받았다(월드 %s): %s - %s", World ? TEXT("있음") : TEXT("없음"), ETravelFailure::ToString(Failure), *Error);
		return;
	}
	if (IsDisconnectHeld())
	{
		return;
	}
	VB_LOG(Warning, "이동 실패: %s - %s", ETravelFailure::ToString(Failure), *Error);
	OnTravelFailureObserved.Broadcast(Failure, Error);
}
