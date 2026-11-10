// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "VBFinisherCameraAuthoringSubsystem.h"

#include "VBFinisherCameraAuthoringSession.h"
#include "Data/VBFinisherConfig.h"

UVBFinisherCameraAuthoringSession* UVBFinisherCameraAuthoringSubsystem::FindOrCreateSession(UVBFinisherConfig* Config)
{
	// 자산이 사라진 세션은 먼저 거둔다. 세션 스스로도 다음 동작에서 거두지만, 다시 안 불릴 수도 있다.
	Sessions.RemoveAll([](UVBFinisherCameraAuthoringSession* Session)
	{
		if (Session && !Session->IsAlive())
		{
			Session->ReleasePreview();
			return true;
		}

		return Session == nullptr;
	});

	if (!Config)
	{
		return nullptr;
	}

	for (UVBFinisherCameraAuthoringSession* Session : Sessions)
	{
		if (Session->GetConfig() == Config)
		{
			return Session;
		}
	}

	// RF_Transactional - 패널의 저작 칸 변경이 되돌리기에 오르게 한다(외부 객체 칸 세션에는 에셋 편집기가 안 붙여 준다).
	//   만든 '뒤에' 붙인다 - 만들 때부터 붙이면 열린 되돌리기 기록에 세션 생성이 잡혀 Ctrl+Z 가 세션을 지울 수 있다.
	UVBFinisherCameraAuthoringSession* Session =
		NewObject<UVBFinisherCameraAuthoringSession>(this, NAME_None, RF_Transient);
	Session->SetFlags(RF_Transactional);
	Session->Initialize(Config);
	Sessions.Add(Session);
	return Session;
}

void UVBFinisherCameraAuthoringSubsystem::Deinitialize()
{
	// 에디터가 내려간다. 레벨의 임시 액터와 재생 타이머를 남기지 않는다.
	for (UVBFinisherCameraAuthoringSession* Session : Sessions)
	{
		if (Session)
		{
			Session->ReleasePreview();
		}
	}

	Sessions.Reset();
	Super::Deinitialize();
}
