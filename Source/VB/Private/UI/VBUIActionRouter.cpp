// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBUIActionRouter.h"

#include "Input/CommonAnalogCursor.h"
#include "UI/VBUIManagerSubsystem.h"

namespace
{
	// 패드 확인 키를 클릭으로 바꾸는 것을 메뉴 입력 모드로 제한한 아날로그 커서.
	class FVBAnalogCursor : public FCommonAnalogCursor
	{
	public:
		explicit FVBAnalogCursor(const UCommonUIActionRouterBase& InActionRouter) : FCommonAnalogCursor(InActionRouter) {}

		// 메뉴가 입력을 제어하면 엔진 판단을 따르고, 게임 모드(설정 없음 포함)면 false 를 돌려준다.
		virtual bool ShouldVirtualAcceptSimulateMouseButton(const FKeyEvent& InKeyEvent, EInputEvent InputEvent) const override
		{
			return UVBUIManagerSubsystem::UsesUIInputMapping(ActionRouter.GetActiveInputMode(ECommonInputMode::Game))
				&& FCommonAnalogCursor::ShouldVirtualAcceptSimulateMouseButton(InKeyEvent, InputEvent);
		}
	};
}

TSharedRef<FCommonAnalogCursor> UVBUIActionRouter::MakeAnalogCursor() const
{
	return FCommonAnalogCursor::CreateAnalogCursor<FVBAnalogCursor>(*this);
}
