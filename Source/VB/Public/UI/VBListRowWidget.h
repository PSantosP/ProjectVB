// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBListRowWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;

DECLARE_DELEGATE_OneParam(FVBOnListRowClicked, int32);

/**
 * 목록 화면의 공용 한 줄: 글 + 옆 글 + 켜짐. 누르면 자기 번호만 알리고 뜻은 화면이 안다.
 * 옛 두 줄 클래스는 ClassRedirects(DefaultEngine.ini)로 이리 이어진다.
 */
UCLASS(Abstract)
class VB_API UVBListRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 옆 글이 비면 그 칸을 접는다. 꺼진 줄은 버튼을 끈 채 보인다.
	void Setup(int32 InRowIndex, const FText& InLabel, const FText& InDetail, bool bEnabled);

	// 줄 앞 그림(선택, 비동기 로드). 그림이 비거나 ImgIcon 이 없으면 접는다.
	void SetIcon(const TSoftObjectPtr<UTexture2D>& Icon);

	UButton* GetButton() const { return BtnRow; }
	// Setup 에 받은 줄 번호(AddRow 의 RowIndex).
	int32 GetRowIndex() const { return RowIndex; }

	FVBOnListRowClicked OnRowClicked;

protected:
	virtual void NativeConstruct() override;

	// UMG 이름 바인딩. 버튼과 글은 필수라 이름이 틀리면 WBP 컴파일이 실패한다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnRow;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtLabel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtDetail;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgIcon;

private:
	UFUNCTION()
	void HandleClicked();

	int32 RowIndex = INDEX_NONE;
};
