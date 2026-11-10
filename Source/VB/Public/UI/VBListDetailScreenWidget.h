// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "VBListDetailScreenWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UVBListRowWidget;
class UWidget;

/**
 * 목록과 상세로 된 메뉴 화면의 부모. 줄 위젯, 빈 글, 첫 줄 초점, 줄과 동작 사이 패드 이동, 다시 그리는 순서를 맡는다.
 * 줄이 가리키는 값과 상세 내용은 맡지 않는다. 자식이 BuildRows, RefreshDetail, Select 로 답한다.
 */
UCLASS(Abstract)
class VB_API UVBListDetailScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	// 고른 값이 줄에 남아 있으면 두고, 없으면 첫 줄로 옮긴다. 줄이 없으면 None 이 된다.
	template <typename KeyType, typename NoneType>
	static void KeepSelectionOrFirst(const TArray<KeyType>& Rows, KeyType& Selected, const NoneType& None)
	{
		if (Rows.Contains(Selected))
		{
			return;
		}

		if (Rows.IsEmpty())
		{
			Selected = None;
		}
		else
		{
			Selected = Rows[0];
		}
	}

protected:
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 줄을 다시 짓고 초점을 새 첫 줄로 옮긴다(초점이 있던 줄이 지워진다).
	void Rebuild();

	// 줄만 다시 짓고 초점은 그대로 둔다. 초점이 지워지지 않는 버튼에 있을 때 쓴다.
	void RebuildRows();

	// AddRow 로 줄을 채우고 선택을 맞춘 뒤 줄 수를 돌려준다.
	virtual int32 BuildRows() PURE_VIRTUAL(UVBListDetailScreenWidget::BuildRows, return 0;);

	// 지금 고른 줄로 상세를 그린다.
	virtual void RefreshDetail() PURE_VIRTUAL(UVBListDetailScreenWidget::RefreshDetail, );

	// 줄을 눌렀다. RowIndex 는 AddRow 에 준 번호다.
	virtual void Select(int32 RowIndex) PURE_VIRTUAL(UVBListDetailScreenWidget::Select, );

	// 고른 줄에 쓰는 동작 버튼들(앞이 주 동작). 목록 칸은 아래 이동이 밖으로 안 나가서 패드가 동작에 닿는 길을 이것으로 건다.
	virtual TArray<UWidget*> GetDetailActions() const { return {}; }

	// 켜진 줄 하나를 더한다. EntryBox 나 EntryWidgetClass 가 없으면 nullptr.
	UVBListRowWidget* AddRow(int32 RowIndex, const FText& Label, const FText& Detail);

	// 목록 한 줄의 WBP(공용 목록 줄). 비면 목록이 비어 보이고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBListRowWidget> EntryWidgetClass;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> EntryBox;

	// 줄이 없을 때만 보인다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtEmpty;

private:
	// 줄을 눌렀다: 고르고(Select) 쓸 수 있는 첫 동작으로 초점을 옮긴다. 고른 뒤 할 일은 동작뿐이다.
	void HandleRowClicked(int32 RowIndex);

	// 줄의 오른쪽(마지막 줄은 아래도)과 동작의 왼쪽에 사용자 지정 패드 이동을 건다. 갈 곳은 패드를 누르는 순간 정해진다.
	void WireDetailNavigation();

	// 줄에서 오른쪽/아래: 누르는 순간 보이고 켜진 첫 동작. 동작 상태가 줄 고르기 밖에서 바뀌어도 낡은 대상을 가리키지 않는다.
	UFUNCTION()
	UWidget* NavigateToAction(EUINavigation Direction);

	// 동작에서 왼쪽: 마지막에 누른 줄, 그 번호가 없으면 첫 줄.
	UFUNCTION()
	UWidget* NavigateToRow(EUINavigation Direction);

	// 보이고 켜진 첫 동작. 없으면 nullptr(패드는 제자리).
	UWidget* FirstUsableAction() const;

	// 마지막에 누른 줄 번호(AddRow 의 RowIndex). 다시 그려도 지우지 않는다 - 쓰고 남은 물건은 같은 번호로 다시 그려진다.
	int32 LastClickedRow = INDEX_NONE;
};
