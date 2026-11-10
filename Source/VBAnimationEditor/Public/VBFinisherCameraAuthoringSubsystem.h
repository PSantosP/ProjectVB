// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "VBFinisherCameraAuthoringSubsystem.generated.h"

class UVBFinisherConfig;
class UVBFinisherCameraAuthoringSession;

/**
 * 처형 카메라 저작 세션의 주인. 자산 하나에 세션 하나라 패널 둘에 열어도 상태가 갈리지 않는다.
 * 세션은 다시 그릴 때마다 버려지는 디테일 패널보다 오래 살아야 하고, 자산이 쥐면 런타임 데이터가 에디터 상태를 품는다.
 */
UCLASS()
class UVBFinisherCameraAuthoringSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// 그 자산의 세션을 주거나 새로 만든다. 자산이 사라진 세션은 이때 거두고 뺀다.
	UVBFinisherCameraAuthoringSession* FindOrCreateSession(UVBFinisherConfig* Config);

	virtual void Deinitialize() override;

private:
	// GC 참조는 여기 하나다. 세션은 자산을 약참조로만 본다 - 이 목록이 자산을 붙들지 않는다.
	UPROPERTY()
	TArray<TObjectPtr<UVBFinisherCameraAuthoringSession>> Sessions;
};
