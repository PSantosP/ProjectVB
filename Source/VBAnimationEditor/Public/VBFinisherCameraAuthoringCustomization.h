// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * 처형 설정 자산(UVBFinisherConfig)의 디테일 패널에 카메라 저작 칸과 버튼을 붙인다
 * 디테일 패널은 다시 그릴 때마다 이 객체를 새로 만들므로 상태는 UVBFinisherCameraAuthoringSession 이 쥔다
 * 버튼은 세션 함수를 부르기만 하고 판단은 세션에 둔다
 */
class FVBFinisherCameraAuthoringCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
