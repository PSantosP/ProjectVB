// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBComponentSubscription.h"
#include "UI/VBListDetailScreenWidget.h"
#include "VBStationScreenWidget.generated.h"

class UTextBlock;
class UVBInventoryComponent;
class UVBStationSessionComponent;

/**
 * 서버 세션(상점, 제작대)의 보기를 그리는 화면의 부모. 보기 구독, 닫기 요청, 가진 돈 글을 맡는다.
 * 여닫기는 PC 가 관리한다. 뒤로 가기와 닫기 버튼은 서버에 보내는 요청일 뿐이다.
 */
UCLASS(Abstract)
class VB_API UVBStationScreenWidget : public UVBListDetailScreenWidget
{
	GENERATED_BODY()

public:
	UVBStationScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	// 세션을 찾아 보기 신호에 묶는다. 첫 그리기는 자식이 선택 값을 비운 뒤 한다.
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual bool NativeOnHandleBackAction() override;

	// 이 화면을 움직이는 세션 컴포넌트.
	virtual UVBStationSessionComponent* FindSession() const PURE_VIRTUAL(UVBStationScreenWidget::FindSession, return nullptr;);

	// 활성인 동안 묶인 세션. 없으면 nullptr.
	template <typename SessionType>
	SessionType* GetSession() const
	{
		return Cast<SessionType>(Session.Get());
	}

	// 가진 돈 글을 지금 값으로 맞춘다.
	void RefreshCurrency(const UVBInventoryComponent* Inventory);

	// 가진 돈. {Name} = 돈 아이템의 표시 이름, {Count} = 가진 수.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText CurrencyFormat;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtCurrency;

private:
	TVBComponentSubscription<UVBStationSessionComponent> Session;
};
