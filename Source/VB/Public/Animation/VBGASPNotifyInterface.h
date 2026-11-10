// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h"
#include "UObject/Interface.h"
#include "VBGASPNotifyInterface.generated.h"

/** GASP 이른 전환 notify 의 native 계약 - locomotion AnimInstance 가 구현한다 */
UINTERFACE(BlueprintType)
class VB_API UVBGASPNotifyInterface : public UInterface
{
	GENERATED_BODY()
};

class VB_API IVBGASPNotifyInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="VB|GASP|Notify")
	EVBGait GetGASPNotifyGait() const;

	/** 고른 전환 요청을 true 로 켠다 - 끄는 것은 animation transition 이 맡는다 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="VB|GASP|Notify")
	void RequestGASPEarlyTransition(bool bToLoop);
};
