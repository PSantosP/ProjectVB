// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonInputTypeEnum.h"
#include "Input/VBKeyBindingRules.h"
#include "Styling/SlateBrush.h"

class APlayerController;
class UImage;
class UInputAction;
class ULocalPlayer;
class UTextBlock;

// 화면에 띄울 누를 키 이름과 그림. 키 안내를 띄우는 화면들이 같이 쓴다.
namespace VBInputKeyText
{
	// 활성 IMC 에서 Action 에 매핑된 첫 유효 키의 짧은 이름. 터치나 Action 없음은 빈 글.
	// bOutUnmapped = 액션은 있는데 매핑된 키가 없다.
	VB_API FText ForAction(const APlayerController* Controller, const UInputAction* Action, ECommonInputType InputType,
		bool& bOutUnmapped);

	// 사용자 키 설정에서 지정 장치 줄의 키 이름.
	VB_API FText ForMapping(const ULocalPlayer* LocalPlayer, FName MappingName, EVBKeyDevice Device);

	// 지금 장치 줄의 키 이름. 터치는 빈 글.
	VB_API FText ForCurrentDevice(const ULocalPlayer* LocalPlayer, FName MappingName);

	// 지금 쓰는 입력 장치(CommonInput 기준). 없으면 빈 값.
	VB_API TOptional<ECommonInputType> CurrentInputType(const ULocalPlayer* LocalPlayer);

	// 키 하나의 짧은 이름. 무효 키는 빈 글.
	VB_API FText ForKey(const FKey& Key);

	// 지금 장치에서 Action 에 매핑된 키의 그림. 텍스처가 없으면 빈 값(엔진은 빈 그림 행도 성공이라 한다).
	VB_API TOptional<FSlateBrush> IconForAction(const APlayerController* Controller, const UInputAction* Action);

	// 사용자 키 설정에서 지금 장치 줄의 키 그림. 터치, 미배치 키, 그림 없는 키는 빈 값.
	VB_API TOptional<FSlateBrush> IconForMapping(const ULocalPlayer* LocalPlayer, FName MappingName);

	// 키 안내 칸 한 쌍을 그린다. 그림이 있으면 그림만, 아니면 글자만(글이 비면 둘 다 접는다).
	VB_API void ShowKey(UTextBlock* Label, UImage* Icon, const FText& Text, const TOptional<FSlateBrush>& Brush);
}
