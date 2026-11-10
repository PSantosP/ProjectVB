// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CommonInputSubsystem.h"
#include "InputMappingContext.h"
#include "VBInputDeviceManager.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBInputDeviceManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBInputDeviceManager();
	// 그 액터에 붙은 것(BP_VBCharacter 가 붙인다). 없으면 nullptr.
	static UVBInputDeviceManager* FindFor(const AActor* Actor);

	// 이 컴포넌트가 마지막으로 IMC 를 건 장치 종류. CommonInput 의 지금 값과 다를 수 있다 - 터치는 IMC 를 바꾸지 않아 여기 남지 않는다.
	//   시작하지 않은 Pawn(CachedPC 참고)에서는 MouseAndKeyboard 로 남고 알림도 오지 않는다. 컴포넌트가 없는 Pawn 에는 Pawn IMC 가 걸리지 않는다.
	ECommonInputType GetCurrentDeviceType() const;
	// 위 값이 Gamepad 인가.
	bool IsUsingGamepad() const;
	// 그 장치 종류에 설정된 IMC 자산(지금 켜진 IMC 가 아니다). 키보드, 패드만 있고 Touch 와 그 밖은 nullptr -
	//   터치는 IMC 를 바꾸지 않고 입력 행동에 직접 주입한다.
	const UInputMappingContext* GetMappingContext(ECommonInputType DeviceType) const;
	
	// 장치 종류가 바뀔 때, 그리고 패드 종류 안에서 패드 이름만 바뀔 때(Xbox -> PS5) - 키 그림을 다시 고르라는 신호다.
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInputDeviceChanged, ECommonInputType, NewDeviceType);
	UPROPERTY(BlueprintAssignable, Category="VB|Input")
	FOnInputDeviceChanged OnInputDeviceChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	
	UPROPERTY(EditDefaultsOnly, Category="VB|Input")
	TObjectPtr<UInputMappingContext> IMC_KeyboardMouse;
	
	UPROPERTY(EditDefaultsOnly, Category="VB|Input")
	TObjectPtr<UInputMappingContext> IMC_Gamepad;
	
	// IMC 를 거는 우선순위. Pawn 의 IMC 는 하나만 켜지므로 0 이다.
	int32 InputMappingPriority = 0;

private:
	ECommonInputType CurrentDeviceType = ECommonInputType::MouseAndKeyboard;
	// 마지막으로 본 패드 이름. CommonInput 은 이름만 바뀌어도 같은 Gamepad 종류로 알려 와서, 이것과 비교해야 그림 갱신을 놓치지 않는다.
	FName CurrentGamepadName = NAME_None;
	// 로컬 컨트롤러. BeginPlay 에서 한 번만 잡는다(다시 잡는 곳은 없다) - 그 뒤의 빙의 바뀜은 따라가지 않는다.
	//   로컬 조종 Pawn 이 아니거나(서버의 원격 Pawn, 시뮬레이션 사본) 그때 컨트롤러가 없으면 IMC 도 구독도 하지 않는다.
	TWeakObjectPtr<APlayerController> CachedPC;

	// BeginPlay 에서: 지금 장치 종류로 IMC 를 걸고 CommonInput 의 바뀜 알림을 구독한다.
	void InitializeInputDetection();
	// CommonInput의 장치 변경 알림을 처리한다.
	void HandleInputMethodChanged(ECommonInputType NewInputType);
	void SwitchToKeyboardMouse();
	void SwitchToGamepad();
	void ApplyMappingContext(UInputMappingContext* NewIMC, UInputMappingContext* OldIMC);
};
