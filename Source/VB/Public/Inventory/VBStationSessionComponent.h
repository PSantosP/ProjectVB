// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBStationSessionComponent.generated.h"

struct FVBRuleContext;

DECLARE_MULTICAST_DELEGATE(FVBOnStationViewChanged);

/**
 * 상대(상인, 제작대)와 여는 서버 세션의 공통 부분. 열림과 닫힘, 상대가 사라졌는지, 지난 세션에 온 요청인지를 여기서 판단한다.
 * 정의 타입과 RPC, 보기는 자식이 갖고, 자식은 표시 이름과 추가 열림 조건, 보기와 닫힘을 보내는 법만 답한다.
 */
UCLASS(Abstract, ClassGroup=(VB))
class VB_API UVBStationSessionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	bool IsSessionOpen() const { return SessionDefinition != nullptr; }

	// 표시가 부른다. 닫아 달라고 서버에 요청한다.
	virtual void RequestClose() PURE_VIRTUAL(UVBStationSessionComponent::RequestClose, );

	// 소유 클라이언트에서 보기가 바뀌었을 때(열림, 행동 뒤, 닫힘).
	FVBOnStationViewChanged OnViewChanged;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 서버에서만. 이미 열려 있거나 정의가 없거나 CanOpenSession 이 거절하면 false.
	bool OpenSession(const UObject* Definition, AActor* Counterpart);

	// 서버에서만. 열려 있지 않으면 아무것도 안 한다.
	void CloseSession();

	// 서버: 요청이 지금 세션에 대한 것이고 상대가 아직 있나. 상대가 사라졌으면 세션을 닫는다.
	bool ValidateSessionRequest(const UObject* Definition, const TCHAR* What);

	FVBRuleContext MakeRuleContext() const;

	const UObject* GetSessionDefinition() const { return SessionDefinition; }

	// 로그에 쓰는 세션 이름.
	virtual const TCHAR* GetSessionLabel() const PURE_VIRTUAL(UVBStationSessionComponent::GetSessionLabel, return TEXT(""););

	// 자식이 더하는 열림 조건. 거절하면 이유는 자식이 로그로 남긴다.
	virtual bool CanOpenSession(const UObject* Definition) const { return true; }

	// 서버: 현재 세션의 보기를 소유 클라이언트에 보낸다.
	virtual void SendView() PURE_VIRTUAL(UVBStationSessionComponent::SendView, );

	// 서버: 세션이 닫혔다고 소유 클라이언트에 알린다.
	virtual void SendSessionClosed() PURE_VIRTUAL(UVBStationSessionComponent::SendSessionClosed, );

private:
	UPROPERTY()
	TObjectPtr<const UObject> SessionDefinition;

	TWeakObjectPtr<AActor> SessionCounterpart;
};
