// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Math/Interval.h"
#include "Settings/VBOptionCatalog.h"
#include "VBOptionRowWidget.generated.h"

class UButton;
class UTextBlock;

DECLARE_DELEGATE_TwoParams(FVBOnOptionValueChanged, int32 /*RowIndex*/, float /*Value*/);
DECLARE_DELEGATE_OneParam(FVBOnOptionAction, int32 /*RowIndex*/);

/**
 * 옵션 한 줄 = 이름 + 좌우 단추 사이의 값(할 일 줄은 단추 하나).
 * 값이 바뀌면 자기 번호와 새 값을 알리고, 화면이 초안에 쓴 뒤 SetValue 로 돌려준다.
 */
UCLASS(Abstract)
class VB_API UVBOptionRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void Setup(int32 InRowIndex, const FVBOptionEntry& Entry);
	// 표시만 바꾼다(알림 없음).
	void SetValue(float InValue);
	UWidget* GetFocusTarget() const;

	FVBOnOptionValueChanged OnValueChanged;
	FVBOnOptionAction OnAction;

protected:
	virtual void NativeOnInitialized() override;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 모두 필수.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtLabel;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtValue;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnPrev;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnNext;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnAction;

private:
	UFUNCTION() void HandlePrev();
	UFUNCTION() void HandleNext();
	UFUNCTION() void HandleAction();

	void Step(int32 Direction);
	void RefreshValueText();

	int32 RowIndex = INDEX_NONE;
	EVBOptionKind Kind = EVBOptionKind::Toggle;
	TArray<FText> Choices;
	FFloatInterval Range;
	float StepSize = 0.05f;
	bool bPercent = false;
	float Value = 0.0f;
};
