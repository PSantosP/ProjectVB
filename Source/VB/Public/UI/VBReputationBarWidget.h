// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VBAttributeSubscription.h"
#include "VBReputationBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UAbilitySystemComponent;
struct FOnAttributeChangeData;

/**
 * 명성 바 - UVBReputationAttributeSet::Reputation 하나만 읽기 전용으로 구독해 바와 숫자를 갱신한다
 * 메뉴 위젯과 관심사를 나누려고 따로 두었고, UVBHealthBarWidget 과 같은 모양이다
 * 소유 위젯이 ASC 준비 뒤 InitializeWithASC 를 부른다
 */
UCLASS()
class VB_API UVBReputationBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ASC 에서 명성 변화를 받아 바를 갱신한다. 다른 ASC 면 옛 구독을 떼고 묶는다. null 이면 아무 일도 하지 않는다.
	void InitializeWithASC(UAbilitySystemComponent* ASC);

protected:
	virtual void NativeDestruct() override;

	// BindWidget: BP 디자이너에서 같은 이름의 위젯과 자동 연결. 없으면 컴파일이 실패한다 -
	//   이 위젯의 존재 이유가 바 하나이므로 누락은 무시하지 않고 명확하게 실패시킨다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> ReputationBar;

	// 숫자 표시(선택). 바만 있어도 동작한다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> ReputationText;

private:
	friend class FVBReputationBarTestAccess;

	void OnReputationChanged(const FOnAttributeChangeData& Data);
	void UpdateBar();

	// 해체되면 뗀다. 묶는 쪽(일시정지 메뉴)이 열 때마다 다시 준다.
	FVBAttributeSubscription ReputationChanged;

	// 비율 계산용 캐시. 어트리뷰트를 매번 되묻지 않는다.
	float CurrentReputation = 0.0f;
};
