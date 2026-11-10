#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponWheelComponent.generated.h"

class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UVBUIManagerSubsystem;
class UUserWidget;
class AVBCharacter;
class APawn;
class UVBWeaponStateComponent;
struct FInputActionValue;

USTRUCT(BlueprintType)
struct FVBWeaponWheelEntry
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|WeaponWheel")
	EVBWeaponType Weapon = EVBWeaponType::None;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|WeaponWheel")
	FText Label;
};

/**
 * 무기 바퀴를 관리한다(로컬 플레이어 컨트롤러). 누르고 있는 동안 열고, 커서(마우스, 스틱)로 칸을 고르고, 떼면 고른 무기를 소환 요청한다.
 * 열린 동안 조준 입력 문맥을 얹고 세계를 SlowMotionScale 로 늦춘다. 그림은 UVBWeaponWheelWidget 이 이 컴포넌트의 상태만 읽어 그린다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBWeaponWheelComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBWeaponWheelComponent();
	void BindInput(UEnhancedInputComponent* Input);
	void SetPresentation(UUserWidget* Widget);
	void Close(bool bCommit);
	bool IsOpen() const { return bOpen; }
	FVector2D GetCursor() const { return Cursor; }
	int32 GetSelectedSlot() const { return SelectedSlot; }
	const TArray<FVBWeaponWheelEntry>& GetVisibleEntries() const { return VisibleEntries; }
	FSimpleMulticastDelegate OnChanged;

	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel|Input")
	TObjectPtr<UInputAction> WheelAction;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel|Input")
	TObjectPtr<UInputAction> AimAction;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel|Input")
	TObjectPtr<UInputMappingContext> AimContext;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel|Input")
	int32 AimPriority = 100;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel", meta=(ClampMin="0.01", ClampMax="1.0"))
	float SlowMotionScale = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel", meta=(ClampMin="1.0"))
	float CursorRadius = 140.f;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel", meta=(ClampMin="0.0"))
	float DeadRadius = 35.f;
	UPROPERTY(EditDefaultsOnly, Category="VB|WeaponWheel", meta=(ClampMin="0.01"))
	float MouseSensitivity = 3.f;

	// 휠 칸: 가진 무기만, 정의 순서대로, 이름은 DisplayName(비면 enum 이름 + 경고).
	static TArray<FVBWeaponWheelEntry> CollectEntries(const UVBWeaponStateComponent& Weapons);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void BeginHold();
	void Open();
	void Complete();
	void Cancel();
	void Aim(const FInputActionValue& Value);
	void CheckAvailability();
	bool CanOpen() const;
	UFUNCTION()
	void OnPawnChanged(APawn* OldPawn, APawn* NewPawn);
	TWeakObjectPtr<AVBCharacter> OpenedPawn;
	TWeakObjectPtr<UUserWidget> Presentation;
	TWeakObjectPtr<UVBUIManagerSubsystem> UIManager;
	FDelegateHandle UIChangedHandle;
	FName DilationHandle;
	TArray<FVBWeaponWheelEntry> VisibleEntries;
	FVector2D Cursor = FVector2D::ZeroVector;
	int32 SelectedSlot = INDEX_NONE;
	bool bOpen = false;
	bool bHoldAllowed = false;
};
