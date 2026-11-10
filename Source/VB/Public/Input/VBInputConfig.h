// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "InputActionValue.h"
#include "VBInputConfig.generated.h"

class UInputAction;

/**
 * 영웅이 직접 처리하는 입력의 약속(태그와 값 형식). Native 줄에 올 수 있는 태그의 유일한 목록이다.
 * bMayBeAbilityInput: 능력 목록에 와도 되는 Native 태그.
 */
struct FVBNativeInputContract
{
	FGameplayTag InputTag;
	EInputActionValueType ValueType = EInputActionValueType::Boolean;
	bool bMayBeAbilityInput = false;
};

/** 입력 행동 하나와 그 입력 태그(Lyra FLyraInputAction). */
USTRUCT(BlueprintType)
struct VB_API FVBInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Input", meta=(Categories="Input"))
	FGameplayTag InputTag;
};

/**
 * 플레이어 Pawn 의 입력 행동 표(Lyra ULyraInputConfig). 태그로 ASC 에 전달하는 입력은 AbilityInputActions 한 줄이면 된다.
 * 키 배치는 장치별 IMC 와 사용자 키 설정이 맡는다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 태그가 정확히 같은 줄의 행동. 없으면 nullptr(기록은 찾는 쪽이 한다).
	const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag) const;
	const UInputAction* FindAbilityInputActionForTag(const FGameplayTag& InputTag) const;

	static TConstArrayView<FVBNativeInputContract> GetNativeInputContract();

	// 잘못된 줄의 목록. 비면 쓸 수 있는 자산이다.
	TArray<FText> FindProblems() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	// 영웅이 직접 처리하는 입력(이동, 시점, 점프, 공격, 상호작용 등).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Input", meta=(TitleProperty="InputTag"))
	TArray<FVBInputAction> NativeInputActions;

	// 누름/놓음을 태그로 ASC 에 전달하기만 하는 입력.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Input", meta=(TitleProperty="InputTag"))
	TArray<FVBInputAction> AbilityInputActions;
};
