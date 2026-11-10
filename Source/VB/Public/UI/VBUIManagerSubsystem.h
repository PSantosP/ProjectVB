// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "Components/SlateWrapperTypes.h"   // ESlateVisibility
#include "Containers/Ticker.h"
#include "CommonInputModeTypes.h"   // ECommonInputMode
#include "Settings/VBFrameRateRequest.h"
#include "VBUIManagerSubsystem.generated.h"

class UCommonActivatableWidget;
class UWidget;
class UVBPrimaryLayout;
class UVBScreenWidget;
struct FStreamableHandle;
class UCommonUIActionRouterBase;

struct FVBUIInputAvailability
{
	bool bAvailable = false;
	bool bBlocked = true;
};

/**
 * 화면을 여닫는 유일한 입구(로컬 플레이어마다 하나). 화면 수명, 게임 정지, 전환 중 입력 멈춤을 맡는다.
 * 입력 모드, 커서는 CommonUI 라우터 몫이다. 정지 중엔 틱이 없으므로 전부 이벤트로 돈다.
 */
// 화면 없이 끝난 push. 이미 열렸거나 여는 중인 태그를 무시한 경우는 알리지 않는다.
enum class EVBScreenPushEnd : uint8
{
	Failed, // route/layout 없음, 클래스 로드 거절, 실패, 또는 삽입 실패.
	Cancelled, // PopScreen 이나 layout 해체로 대기 중 로드를 거뒀다.
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnScreenPushEnded, FGameplayTag /*ScreenTag*/, EVBScreenPushEnd /*Reason*/);

UCLASS()
class VB_API UVBUIManagerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	FVBUIInputAvailability QueryInputAvailability(const APlayerController* Controller) const;
	FSimpleMulticastDelegate OnInputAvailabilityChanged;

	// push 가 화면 없이 끝나면 요청한 쪽이 정리하거나 다시 시도한다.
	FVBOnScreenPushEnded OnScreenPushEnded;
	// 밖에서 입력 무시 상태(SetIgnoreMove/LookInput)를 바꾼 직후 부른다. 구독자는 폴링하지 않는다.
	void NotifyInputPolicyChanged();
	// 위젯, PC, 로컬 플레이어 어느 쪽에서든 찾는다. 로컬 플레이어가 없으면 null.
	static UVBUIManagerSubsystem* Get(const UObject* WorldContextObject);
	// UI 매핑을 켜 두는 입력 모드인가. Game 에선 끈다(키를 소비해 같은 키의 게임 동작을 가린다).
	static bool UsesUIInputMapping(ECommonInputMode ActiveMode) { return ActiveMode != ECommonInputMode::Game; }

	virtual void Deinitialize() override;
	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	// 화면 태그로 비동기 로드해 연다(로드 중 입력 멈춤). 이미 떠 있거나 여는 중이면 무시한다.
	UFUNCTION(BlueprintCallable, Category="VB|UI", meta=(GameplayTagFilter="UI.Screen"))
	void PushScreen(FGameplayTag ScreenTag);

	// 그 화면을 내린다. 여는 중이면 로드를 취소한다. 화면 스스로는 DeactivateWidget 으로 닫는다.
	UFUNCTION(BlueprintCallable, Category="VB|UI", meta=(GameplayTagFilter="UI.Screen"))
	void PopScreen(FGameplayTag ScreenTag);

	UFUNCTION(BlueprintPure, Category="VB|UI", meta=(GameplayTagFilter="UI.Screen"))
	bool IsScreenOpen(FGameplayTag ScreenTag) const;

	// 그 화면이 어느 층에든 떠 있으면 돌려준다. 없으면 null 이다.
	UCommonActivatableWidget* FindOpenScreen(FGameplayTag ScreenTag) const;

	// 이유 이름별로 이 플레이어의 모든 입력을 막거나 푼다. 이유는 고정 문자열이어야 한다(엔진 표가 줄지 않는다).
	void SetInputSuspended(FName Reason, bool bSuspended);

	// 한 층을 통째로 감추거나 보인다. 화면은 활성인 채로 남는다.
	// 화면 자신도 감춘다. CommonUI 의 맨 위 판정이 화면 자신의 가시성만 봐서, 부모만 감추면 뒤에 뜬 화면이 맨 위가 못 된다.
	void SetLayerHidden(FGameplayTag LayerTag, bool bHidden);
	// 자막 영역을 유지하며 게임 HUD 만 감추고 손잡이가 끝나면 원래 가시성을 돌린다.
	FGuid BeginHUDHide(APlayerController& Controller);
	void EndHUDHide(FGuid Token);
	bool AttachPresentation(UWidget& Widget);


	UVBPrimaryLayout* GetLayout() const { return Layout; }

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBUIMessagingTestAccess;
	friend class FVBCinematicHUDTestAccess;
#endif
	struct FInputPolicyScope
	{
		explicit FInputPolicyScope(UVBUIManagerSubsystem& InOwner);
		~FInputPolicyScope();
		UVBUIManagerSubsystem& Owner;
	};
	bool NotifyAfterTreeUpdate(float DeltaSeconds);
	void ScheduleTreeUpdateNotification();
	TWeakObjectPtr<UCommonUIActionRouterBase> InputRouter;
	FDelegateHandle InputConfigChangedHandle;
	FDelegateHandle RouterActionsChangedHandle;
	FTSTicker::FDelegateHandle InputTreeTickerHandle;
	TSet<FName> SuspendedInputReasons;
	int32 InputPolicyUpdateDepth = 0;
	bool bDeinitializing = false;
	bool IsLayerHidden(FGameplayTag LayerTag) const;
	void ApplyLayerHidden(FGameplayTag LayerTag);
	TSet<FGameplayTag> LegacyHiddenLayers;
	TSet<FGuid> HUDHideRequests;
	TMap<FGameplayTag, ESlateVisibility> PreviousLayerVisibility;
	void CreateLayout(APlayerController* OwningController);
	void DestroyLayout();
	void ApplyUIInputMapping(bool bAdd);
	// 라우터의 입력 모드에 맞춰 UI 매핑을 켜거나 끈다(레이아웃이 없으면 끈다).
	void RefreshUIInputMapping();

	void FinishPush(FGameplayTag ScreenTag);
	void CancelPendingPush(FGameplayTag ScreenTag);
	static FName MakePushReason(FGameplayTag ScreenTag);

	void WatchScreen(UVBScreenWidget& Screen);
	void HandleScreenActivated(TWeakObjectPtr<UVBScreenWidget> Screen);
	void HandleScreenDeactivated(TWeakObjectPtr<UVBScreenWidget> Screen);
	void ApplyPause();

	UPROPERTY(Transient)
	TObjectPtr<UVBPrimaryLayout> Layout;

	TWeakObjectPtr<APlayerController> LayoutOwner;

	// 여는 중인 화면. 같은 화면을 두 번 열지 않고, PopScreen 이 로드를 취소하는 데 쓴다.
	TMap<FGameplayTag, TSharedPtr<FStreamableHandle>> PendingPushes;

	// 활성화 알림을 이미 건 화면. 위젯 재사용 때 두 번 걸지 않으려고 둔다.
	TSet<TWeakObjectPtr<UVBScreenWidget>> WatchedScreens;

	// 지금 정지를 원하는 활성 화면. 집합이라 알림이 두 번 와도 한 번으로 친다.
	TSet<TWeakObjectPtr<UVBScreenWidget>> PausingScreens;

	// 마지막으로 적용한 정지 상태. 뒤집힐 때만 SetGamePaused 를 부른다.
	bool bPauseApplied = false;

	// 정지 화면이 떠 있는 동안 든 메뉴 프레임 제한 요청.
	FVBFrameRateRequestHandle MenuFrameRateRequest;

	// SetLayerHidden 이 감춘 화면과 원래 가시성. 화면마다 원래 값이 달라 그대로 돌려준다.
	TMap<TWeakObjectPtr<UCommonActivatableWidget>, ESlateVisibility> HiddenScreenVisibility;
};
