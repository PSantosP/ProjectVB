// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBStationSessionComponent.h"

#include "GameFramework/Controller.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VB/VB.h"

bool UVBStationSessionComponent::OpenSession(const UObject* Definition, AActor* Counterpart)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		VB_LOG(Warning, "%s 열기를 권한 없는 쪽에서 불렀다 - 무시한다(서버만)", GetSessionLabel());
		return false;
	}

	if (!Definition)
	{
		VB_LOG(Warning, "%s 열기: 정의가 비었다(%s)", GetSessionLabel(), *GetNameSafe(Counterpart));
		return false;
	}

	if (IsSessionOpen())
	{
		VB_LOG(Log, "%s 열기 거절: 이미 %s 가 열려 있다", GetSessionLabel(), *GetNameSafe(SessionDefinition));
		return false;
	}

	if (!CanOpenSession(Definition))
	{
		return false;
	}

	SessionDefinition = Definition;
	SessionCounterpart = Counterpart;
	VB_LOG(Log, "%s 열림: %s (상대 %s)", GetSessionLabel(), *GetNameSafe(Definition), *GetNameSafe(Counterpart));
	SendView();
	return true;
}

void UVBStationSessionComponent::CloseSession()
{
	if (!IsSessionOpen())
	{
		return;
	}

	VB_LOG(Log, "%s 닫힘: %s", GetSessionLabel(), *GetNameSafe(SessionDefinition));
	SessionDefinition = nullptr;
	SessionCounterpart.Reset();
	SendSessionClosed();
}

bool UVBStationSessionComponent::ValidateSessionRequest(const UObject* Definition, const TCHAR* What)
{
	if (!IsSessionOpen())
	{
		// 닫힌 직후에 도착한 요청(닫기 버튼을 두 번 누름). 정상이라 조용히 버린다.
		VB_LOG(Verbose, "%s %s 요청: 열린 세션이 없다 - 버린다", GetSessionLabel(), What);
		return false;
	}

	if (SessionCounterpart.IsStale())
	{
		VB_LOG(Log, "%s %s 요청: 상대가 사라졌다 - 닫는다", GetSessionLabel(), What);
		CloseSession();
		return false;
	}

	if (Definition != SessionDefinition)
	{
		VB_LOG(Log, "%s %s 요청: 지난 세션 %s 에 대한 요청(지금 %s) - 버린다", GetSessionLabel(), What,
			*GetNameSafe(Definition), *GetNameSafe(SessionDefinition));
		return false;
	}

	return true;
}

FVBRuleContext UVBStationSessionComponent::MakeRuleContext() const
{
	const AController* Controller = Cast<AController>(GetOwner());
	FVBRuleContext Context = FVBRuleContext::Make(this, Controller ? Controller->PlayerState.Get() : nullptr);
	Context.Counterpart = SessionCounterpart.Get();
	return Context;
}

void UVBStationSessionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레벨을 떠나거나 PC 가 사라진다. 보낼 곳이 없으니 상태만 비운다.
	SessionDefinition = nullptr;
	SessionCounterpart.Reset();
	Super::EndPlay(EndPlayReason);
}
