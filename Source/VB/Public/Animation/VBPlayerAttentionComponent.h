#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBPlayerAttentionComponent.generated.h"

class APawn;
class UVBInteractorComponent;
class UVBLookAtInterestComponent;

UENUM(BlueprintType)
enum class EVBPlayerAttentionSource : uint8 { None, Conversation, Interaction, Interest };

USTRUCT()
struct FVBPlayerAttentionTarget
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AActor> Actor = nullptr;
	UPROPERTY() FVector LocalPoint = FVector::ZeroVector;
	UPROPERTY() FName Socket = NAME_None;
	UPROPERTY() EVBPlayerAttentionSource Source = EVBPlayerAttentionSource::None;
};

/** 서버가 시선 대상을 고르고, 모든 기계의 LookAt layer 가 그것을 쓴다 */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VB_API UVBPlayerAttentionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBPlayerAttentionComponent();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Look At") bool SetConversationTarget(AActor* Actor);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Look At") void ClearConversationTarget(AActor* ExpectedActor);
	UFUNCTION(BlueprintPure, Category="Look At") AActor* GetSelectedActor() const { return Selected.Actor; }
	UFUNCTION(BlueprintPure, Category="Look At") EVBPlayerAttentionSource GetSelectedSource() const { return Selected.Source; }
	bool ResolveAttention(FVector& OutLocation, bool bConversationOnly) const;
	bool IsPoseSuppressed() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bAutomaticAttention = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float AcquireDistance = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float ReleaseDistance = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", ClampMax="89", Units="deg"))
	float AcquireHalfAngle = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", ClampMax="89", Units="deg"))
	float ReleaseHalfAngle = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float ConversationMaxDistance = 600.f;
	/** 플레이어가 대상에 이 거리만큼 다가오면 자동 상호작용 시선을 놓는다 (reach/prompt anchor 는 그대로) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm"))
	float InteractionGazeMinDistance = 125.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm"))
	float InteractionGazeMaxDropFromHead = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="s"))
	float InteractionGazeResumeDelay = 0.65f;
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	void UpdateAttention();
	// 상호작용이 캐릭터를 제어하는 동안 자동 시선이 돌아올 시각을 뒤로 민다.
	void HoldGazeWhileInteracting(const UVBInteractorComponent* Interactor);
	// 대화 상대가 보이면 고르고 true.
	bool TrySelectConversation();
	// 가장 가까운 상호작용 대상이 보이면 고르고 true.
	bool TrySelectInteraction(const APawn& Pawn, const UVBInteractorComponent& Interactor);
	// 지금 대상을 유지하거나 더 나은 관심 대상을 찾는다. 없으면 nullptr.
	UVBLookAtInterestComponent* FindBestInterest(const APawn& Pawn) const;
	// 찾은 관심 대상을 현재 대상으로 고른다. nullptr 이면 시선을 비운다.
	void SelectInterest(UVBLookAtInterestComponent* Best);
	bool IsUsableActor(const AActor* Actor) const;
	bool IsVisible(const AActor* Actor, const FVector& Location) const;
	bool IsInView(const FVector& Location, float Distance, float HalfAngle) const;
	bool IsEligibleInterest(const UVBLookAtInterestComponent* Interest, bool bRetain) const;
	void Select(AActor* Actor, const FVector& WorldPoint, FName Socket, EVBPlayerAttentionSource Source);
	UPROPERTY(Replicated) FVBPlayerAttentionTarget Selected;
	TWeakObjectPtr<AActor> Conversation;
	TWeakObjectPtr<UVBLookAtInterestComponent> CurrentInterest;
	float InteractionGazeResumeTime = 0.f;
};
