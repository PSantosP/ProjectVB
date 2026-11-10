// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VBLoadingProcess.generated.h"

UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UVBLoadingProcess : public UInterface
{
	GENERATED_BODY()
};

/**
 * 로딩 화면을 붙잡을 수 있는 준비 과정(Lyra ILoadingProcessInterface 의 묻기 쪽).
 * 로딩 화면 담당 클래스가 매 판정마다 묻는다. 네이티브만 구현하고, 묻는 동안 상태를 바꾸지 않는다.
 */
class VB_API IVBLoadingProcess
{
	GENERATED_BODY()

public:
	// 참이면 로딩 화면을 띄워 둔다. OutReason 은 로그, 디버그용 사유 이름.
	virtual bool ShouldShowLoadingScreen(FString& OutReason) const = 0;
};
