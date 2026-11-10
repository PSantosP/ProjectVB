#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "AbilitySystem/VBAttributeSubscription.h"
#include "CommonInputTypeEnum.h"
#include "UI/VBPlayerStateBinding.h"
#include "VBCinematicSkillGaugeWidget.generated.h"

class UCommonInputSubsystem;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;
class UProgressBar;
class UTextBlock;
class UVBCinematicSkillChargeComponent;

// 소유 PlayerState 의 자원을 읽고, 키 재지정이 반영되는 입력 힌트를 갱신한다.
UCLASS()
class VB_API UVBCinematicSkillGaugeWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void InitializeWithASC(UAbilitySystemComponent* ASC);
	UFUNCTION(BlueprintPure, Category="VB|UI|CinematicSkill")
	float GetGaugePercent() const { return GaugePercent; }
	UFUNCTION(BlueprintPure, Category="VB|UI|CinematicSkill")
	bool IsGaugeFull() const { return bGaugeFull; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|CinematicSkill")
	TObjectPtr<const UInputAction> SkillAction;
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|CinematicSkill")
	TObjectPtr<const UInputAction> PadChordAction;
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UProgressBar> GaugeBar;
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyHint;
	UPROPERTY(Transient, meta=(BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> FullGlow;

private:
	void HandleAttributeChanged(const FOnAttributeChangeData& Data);
	void HandleInputMethodChanged(ECommonInputType Type);
	void UnbindASC();
	UFUNCTION()
	void Refresh();
	UFUNCTION()
	void RefreshKeyHint();

	FVBPlayerStateBinding PlayerStateSet;
	FVBAttributeSubscription CurrentSubscription;
	FVBAttributeSubscription MaximumSubscription;
	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	TWeakObjectPtr<UVBCinematicSkillChargeComponent> Charge;
	TWeakObjectPtr<UCommonInputSubsystem> InputSource;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSource;
	FDelegateHandle InputMethodHandle;
	float GaugePercent = 0.0f;
	bool bGaugeFull = false;
	bool bReportedNoMaximum = false;
};
