// 시험 전용 reflected 타입 - 게임 코드는 이 헤더에 기대지 않는다.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "VBGameplayMessageTestProbe.generated.h"

class UVBAsyncAction_ListenForGameplayMessage;

USTRUCT()
struct FVBGameplayMessageTestPayload
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Value = 0;
};

USTRUCT()
struct FVBGameplayMessageTestDerivedPayload : public FVBGameplayMessageTestPayload
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Extra = 0;
};

USTRUCT()
struct FVBGameplayMessageTestOtherPayload
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Value = 0;
};

// 멤버 listener overload 들의 실제 UObject 주인이자 async dynamic multicast 의 UFUNCTION 대상 - 일반 C++ mock 으로는 수명을 못 잰다.
UCLASS()
class UVBGameplayMessageTestProbe : public UObject
{
    GENERATED_BODY()

public:
    int32 MemberCalls = 0;
    int32 LastValue = 0;
    FGameplayTag LastChannel;
    TFunction<void(UVBAsyncAction_ListenForGameplayMessage*, FGameplayTag)> OnAsync;

    void ReceiveMember(FGameplayTag Channel, const FVBGameplayMessageTestPayload& Payload)
    {
        ++MemberCalls;
        LastValue = Payload.Value;
        LastChannel = Channel;
    }

    UFUNCTION()
    void ReceiveAsync(UVBAsyncAction_ListenForGameplayMessage* Action, FGameplayTag Channel)
    {
        if (OnAsync)
        {
            OnAsync(Action, Channel);
        }
    }
};
