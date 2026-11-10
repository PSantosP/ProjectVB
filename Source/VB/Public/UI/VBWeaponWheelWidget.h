#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBWeaponWheelWidget.generated.h"

class UVBWeaponWheelComponent;

/**
 * 무기 바퀴 그림. 상태는 갖지 않는다 - UVBWeaponWheelComponent 의 보이는 칸, 고른 칸을 읽어 고리로 그린다.
 * 구성될 때 스스로 소유 AVBPlayerController 의 바퀴에 바인딩한다. 해체 때 해제하므로 다시 구성되면 다시 바인딩한다.
 */
UCLASS()
class VB_API UVBWeaponWheelWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	// 다른 소유자, 시험용. 소유자가 AVBPlayerController 면 구성 때 스스로 부른다.
	void BindWheel(UVBWeaponWheelComponent* Component);
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel")
	FLinearColor RingColor = FLinearColor(0.025f, 0.035f, 0.05f, 0.9f);
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel")
	FLinearColor SelectionColor = FLinearColor(0.12f, 0.65f, 0.8f, 0.95f);
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel")
	float RingRadius = 140.f;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel")
	float RingThickness = 85.f;
private:
	void Refresh();
	TWeakObjectPtr<UVBWeaponWheelComponent> Wheel;
	FDelegateHandle ChangedHandle;
};
