// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "VBButtonBase.generated.h"

class UCommonTextBlock;

/**
 * 모든 UI 버튼의 부모. 모양은 UCommonButtonStyle 이 정하고 이 클래스는 글자만 붙인다.
 * UCommonButtonBase 는 상태가 바뀌어도 글자 스타일을 안 입혀서 NativeOnCurrentTextStyleChanged 에서 직접 입힌다.
 */
UCLASS(Abstract)
class VB_API UVBButtonBase : public UCommonButtonBase
{
	GENERATED_BODY()

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeOnCurrentTextStyleChanged() override;

	// 버튼에 적힐 글. 디자이너에서 바로 보이도록 PreConstruct 에서도 적용한다.
	UPROPERTY(EditAnywhere, Category="VB|UI|Button")
	FText ButtonText;

private:
	// UMG 이름 바인딩. 아이콘 버튼도 있어 선택이다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UCommonTextBlock> ButtonLabel;

	void ApplyText();
};
