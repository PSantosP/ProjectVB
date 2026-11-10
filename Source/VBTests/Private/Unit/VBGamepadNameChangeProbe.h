// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonInputTypeEnum.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UObject/Object.h"
#include "VBGamepadNameChangeProbe.generated.h"

class UInputMappingContext;

// CommonInput이 격리 월드의 실제 알림 경로를 사용하게 한다.
UCLASS(Transient)
class UVBGamepadNameChangeViewport : public UGameViewportClient
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<UWorld> FixtureWorld;
	virtual UWorld* GetWorld() const override { return FixtureWorld.Get(); }
};

UCLASS()
class UVBGamepadNameChangeProbe : public UObject
{
	GENERATED_BODY()
public:
	int32 DeviceChanges = 0;
	int32 ContextAdds = 0;
	int32 ContextRemoves = 0;
	ECommonInputType LastDevice = ECommonInputType::MouseAndKeyboard;

	// 알림을 받는 순간 Action 에 매핑된 키 - 구독자(키 안내)가 그 순간 읽는 값과 같다. Input/Action 이 비면 안 읽는다.
	TWeakObjectPtr<const UEnhancedInputLocalPlayerSubsystem> Input;
	TWeakObjectPtr<const UInputAction> Action;
	TArray<FKey> KeysAtBroadcast;

	UFUNCTION()
	void OnDeviceChanged(ECommonInputType Type)
	{
		++DeviceChanges;
		LastDevice = Type;
		if (Input.IsValid() && Action.IsValid())
		{
			KeysAtBroadcast = Input->QueryKeysMappedToAction(Action.Get());
		}
	}
	// UHT 는 UFUNCTION 매개변수 이름을 요구한다(주석 처리한 이름은 "Expected name" 으로 막힌다).
	UFUNCTION()
	void OnContextAdded(const UInputMappingContext* MappingContext) { ++ContextAdds; }
	UFUNCTION()
	void OnContextRemoved(const UInputMappingContext* MappingContext) { ++ContextRemoves; }
};
