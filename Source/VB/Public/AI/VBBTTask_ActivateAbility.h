// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "VBBTTask_ActivateAbility.generated.h"

class UAbilitySystemComponent;
struct FAbilityEndedData;

/**
 * 폰의 ASC 에서 InputTag 에 맞는 어빌리티를 발동하는 BT 태스크.
 * 종료 대기 상태를 들고 있으므로 노드를 인스턴싱한다(bCreateNodeInstance).
 */
UCLASS()
class VB_API UVBBTTask_ActivateAbility : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UVBBTTask_ActivateAbility();

	// 발동할 어빌리티의 InputTag. 비우면 폰의 전투 컴포넌트가 정한 공격 태그를 쓴다.
	UPROPERTY(EditAnywhere, Category="VB|Ability")
	FGameplayTag AbilityTag;

	// 어빌리티가 끝날 때까지 태스크를 붙잡을지. false 면 즉시 Succeeded/Failed.
	//   공격 길이를 BTTask_Wait 로 이중 저작하지 않으려고 둔다.
	UPROPERTY(EditAnywhere, Category="VB|Ability")
	bool bWaitForAbilityEnd = false;

	// 취소로 끝난 어빌리티를 성공으로 볼지 여부. 기본 false - 경직으로 잘린 공격은 실패다.
	UPROPERTY(EditAnywhere, Category="VB|Ability", meta=(EditCondition="bWaitForAbilityEnd"))
	bool bSucceedOnCancel = false;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 중단 경로. 델리게이트를 놓아 다음 활성화의 종료가 잘못 들어오지 않게 한다.
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 모든 종료 경로의 수렴점. 여기서도 구독을 해제한다.
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;

	// BT 에디터에서 노드 이름 표시
	virtual FString GetStaticDescription() const override;

private:
	// 이 노드가 발동할 InputTag. 노드 설정 우선, 비면 폰의 아키타입 설정.
	FGameplayTag ResolveAbilityTag(const APawn* Pawn) const;

	// ASC OnAbilityEnded 콜백. 우리가 켠 그 스펙일 때만 반응한다.
	void OnAbilityEnded(const FAbilityEndedData& EndedData);

	// 구독 해제. 두 번 불러도 안전하다(핸들 무효 검사).
	void UnbindAbilityEnded();

	// 대기 중인 활성화의 스펙. 콜백이 다른 어빌리티의 종료인지 가리는 데 쓴다.
	FGameplayAbilitySpecHandle WaitingSpecHandle;

	// 구독 대상 ASC. 약참조인 이유: 폰이 먼저 파괴되면 해제 시점에 이미 없다.
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;

	// FinishLatentTask 대상. 콜백이 비동기로 오므로 실행 시점의 트리 컴포넌트를 들고 있어야 한다.
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

	FDelegateHandle AbilityEndedHandle;

	// 종료 통지를 기다리는 중인가. 콜백이 중복/지연 도착해도 한 번만 처리하게 하는 래치.
	bool bWaitingForAbilityEnd = false;

	// ExecuteTask 안에서 활성화 중인가. 같은 스택에서 즉시 끝나면 FinishLatentTask 대신 반환값으로 답한다.
	bool bInsideExecute = false;

	// 위 동기 종료가 실제로 일어났는가. ExecuteTask 가 반환값으로 결과를 돌려주는 데 쓴다.
	bool bEndedDuringExecute = false;

	// 마지막 종료가 취소였는가. bSucceedOnCancel 과 함께 최종 결과를 정한다.
	bool bLastEndWasCancelled = false;
};
