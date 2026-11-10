// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "VBUIMessagingSubsystem.generated.h"

class UVBUIMessagingSubsystem;

// 받아들인 요청은 한 번 끝난다. 약한 요청자와 해체 때는 조용히 사라질 수 있다.
UENUM()
enum class EVBConfirmationResult : uint8
{
	Confirmed,
	Declined,
	Cancelled,
	Failed,
};

// 문구는 부르는 쪽이 쓴다. 버튼 글이 비면 대화 상자 기본값을 쓴다.
USTRUCT()
struct FVBConfirmationRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|UI")
	FText Header;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	FText Body;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	FText ConfirmLabel;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	FText DeclineLabel;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	bool bOfferDecline = true;

	UPROPERTY(EditAnywhere, Category="VB|UI")
	bool bFocusDecline = false;
};

// handle 은 로컬 messaging subsystem 하나에 속한다. 0 = 요청 없음.
struct FVBConfirmationHandle
{
	uint32 Id = 0;

	bool IsValid() const { return Id != 0; }
	bool operator==(const FVBConfirmationHandle& Other) const { return Id == Other.Id; }
};

DECLARE_DELEGATE_OneParam(FVBConfirmationResultDelegate, EVBConfirmationResult);

// 요청 동작마다 대기 질문 하나. 완료 여부는 messaging subsystem 에 묻는다.
struct VB_API FVBConfirmationRequester
{
	// 동기 Failed 는 false 를 돌려주기 전에 콜백한다. messaging subsystem 이 없으면 콜백도 없다.
	bool Ask(const UObject* Requester, const FVBConfirmationRequest& Request, FVBConfirmationResultDelegate OnResult);
	void Cancel(const UObject* Requester);
	void SetBody(const UObject* Requester, const FText& Body);
	bool IsPending(const UObject* Requester) const;
	FVBConfirmationHandle GetHandle() const { return Handle; }

private:
	FVBConfirmationHandle Handle;
	// 요청자의 LocalPlayer 가 바뀌어도 저장한 ID 를 발급한 subsystem 을 그대로 유지한다.
	TWeakObjectPtr<UVBUIMessagingSubsystem> Owner;
	uint64 Revision = 0;
};

enum class EVBScreenPushEnd : uint8;

/** LocalPlayer 마다 하나인 확인 질문 FIFO.
 * 표시, 입력은 UIManager 가, ID, FIFO 순서, 결과는 이 서브시스템이 맡는다.
 * 결과는 CreateUObject/CreateWeakLambda 로 약하게 묶는다. 로컬 답은 서버 권한이 아니다.
 */
UCLASS()
class VB_API UVBUIMessagingSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UVBUIMessagingSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// 콜백 없이 버린다. 해체 때 요청자 위젯을 깨우면 안 된다.
	virtual void Deinitialize() override;

	// 동기 Failed 는 무효 handle 을 돌려주므로 부르는 쪽은 그것으로 잠그지 않는다.
	FVBConfirmationHandle ShowConfirmation(const FVBConfirmationRequest& Request, FVBConfirmationResultDelegate OnResult);
	void SetBody(FVBConfirmationHandle Handle, const FText& Body);
	void Cancel(FVBConfirmationHandle Handle);
	bool IsPending(FVBConfirmationHandle Handle) const;

	const FVBConfirmationRequest* GetPresented(FVBConfirmationHandle& OutHandle) const;
	// Shown 인 맨 앞만 답할 수 있다. 낡은, 대기, Requested handle 은 무시한다.
	void Answer(FVBConfirmationHandle Handle, EVBConfirmationResult Result);
	void NotifyDialogActivated();
	void NotifyDialogDeactivated();

	FSimpleMulticastDelegate OnPresentedChanged;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBUIMessagingTestAccess;
#endif

	struct FPendingConfirmation
	{
		FVBConfirmationHandle Handle;
		FVBConfirmationRequest Request;
		FVBConfirmationResultDelegate OnResult;
	};

	enum class EDialogState : uint8 { Idle, Requested, Shown };

	void Present();
	void SchedulePresent();
	bool HandlePresentTick(float DeltaSeconds);
	void Resolve(int32 Index, EVBConfirmationResult Result);
	void FailAll(const TCHAR* Why);
	void HandleScreenPushEnded(FGameplayTag ScreenTag, EVBScreenPushEnd Reason);
	int32 FindIndex(FVBConfirmationHandle Handle) const;

	TArray<FPendingConfirmation> Queue;
	uint32 NextId = 1;
	EDialogState DialogState = EDialogState::Idle;
	// 닫기, 결과 콜백 중의 새 표시는 이 서브시스템이 보유한 다음 틱 Ticker 한 번으로 미룬다.
	int32 PresentDeferDepth = 0;
	bool bDeinitializing = false;
	FTSTicker::FDelegateHandle PresentTicker;
	FDelegateHandle PushEndedHandle;
};
