// 시험 전용 reflection 로딩 process - UHT 때문에 VBLoadingScreenTests.cpp 와 함께 옮긴다
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "Player/VBPlayerController.h"
#include "UI/Loading/VBLoadingProcess.h"
#include "VBLoadingScreenTestProbe.generated.h"

class UWorld;

UCLASS()
class UVBLoadingScreenTestProbe : public UObject, public IVBLoadingProcess
{
    GENERATED_BODY()

public:
    bool bWanted = true;
    FString Reason = TEXT("Test preparation pending");
    mutable int32 QueryCalls = 0;
    TFunction<bool(FString&)> OnQuery;

    // 실제 world identity 가 있어야 제품의 GetWorld/teardown 필터가 그대로 돈다
    UPROPERTY()
    TObjectPtr<UWorld> World;

    virtual UWorld* GetWorld() const override { return World.Get(); }

    virtual bool ShouldShowLoadingScreen(FString& OutReason) const override
    {
        ++QueryCalls;
        if (OnQuery)
        {
            return OnQuery(OutReason);
        }

        OutReason = Reason;
        return bWanted;
    }
};

// 직접 가진 stack world context 만 넘기고 UGameInstance::Init 은 부르지 않는다
UCLASS()
class UVBLoadingScreenTestGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    void SetContextForTests(FWorldContext* Context) { WorldContext = Context; }
};

// OS 창이나 빌린 viewport 없이 실제 Show/Hide 위젯 호출을 기록한다
UCLASS()
class UVBLoadingScreenTestViewport : public UGameViewportClient
{
    GENERATED_BODY()
public:
    TSharedPtr<SWidget> Attached;
    int32 Adds = 0;
    int32 Removes = 0;
    virtual void AddViewportWidgetContent(TSharedRef<SWidget> Content, int32 ZOrder = 0) override
    {
        ++Adds;
        Attached = Content;
    }

    virtual void RemoveViewportWidgetContent(TSharedRef<SWidget> Content) override
    {
        ++Removes;
        if (Attached == Content)
        {
            Attached.Reset();
        }
    }
};

// 로딩 주인의 PC 존재 공백을 시험한다 - AVBPlayerController 의 역할 수락은 시험하지 않는다
UCLASS()
class AVBLoadingScreenTestPlayerProcess : public APlayerController, public IVBLoadingProcess
{
    GENERATED_BODY()
public:
    bool bWanted = false;
    virtual bool ShouldShowLoadingScreen(FString& OutReason) const override
    {
        OutReason = TEXT("Test PC preparation");
        return bWanted;
    }
};

UCLASS()
class AVBLoadingScreenTestVBController : public AVBPlayerController
{
    GENERATED_BODY()
public:
    // 실제 protected override 를 부른다 - possession/startup 수명을 지어내지 않는다
    void SetPawnForTests(APawn* InPawn) { SetPawn(InPawn); }
};

UCLASS()
class AVBLoadingScreenTestAbilityPawn : public APawn, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    AVBLoadingScreenTestAbilityPawn()
    {
        AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("LoadingTestAbilitySystem"));
    }

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
    UPROPERTY()
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;
};
