// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/CommonUIActionRouterBase.h"
#include "VBUIActionRouter.generated.h"

/**
 * CommonUI 액션 라우터의 프로젝트 판. 패드 A 를 클릭으로 바꾸는 것은 메뉴가 입력을 잡을 때만 한다.
 * 게임 모드에서는 A 가 게임 입력으로 간다(기준은 UVBUIManagerSubsystem::UsesUIInputMapping).
 */
UCLASS()
class VB_API UVBUIActionRouter : public UCommonUIActionRouterBase
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<FCommonAnalogCursor> MakeAnalogCursor() const override;
};
