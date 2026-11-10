// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AI/VBBTService_UpdateCombat.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h" // ResolveSelectedKey 가 완전 타입을 요구한다
#include "GameFramework/Pawn.h"
#include "AI/VBCombatTypes.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AI/VBEnemyGaitControl.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystemGlobals.h" // 포커스 게이트 - 폰의 ASC 로 공격 중인지 묻는다

UVBBTService_UpdateCombat::UVBBTService_UpdateCombat(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = "Update Combat Coordination";

	// 없으면 bNotifyTick 이 꺼져 서비스가 조용히 아무것도 안 한다.
	INIT_SERVICE_NODE_NOTIFY_FLAGS();

	// 관측 주기 기본값. 실제 값은 트리 자산의 노드 프로퍼티다.
	Interval = 0.2f;
	RandomDeviation = 0.0f;

	// 선택자가 받을 키 타입을 제한한다(잘못된 키로 조용히 무동작이 되지 않게).
	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, TargetActorKey),
		AActor::StaticClass());
	// 열거형 키는 자산 enum 과 네이티브 enum 둘 다 허용한다.
	CombatRoleKey.AddEnumFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, CombatRoleKey),
		StaticEnum<EVBCombatRole>());
	CombatRoleKey.AddNativeEnumFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, CombatRoleKey),
		TEXT("EVBCombatRole"));
	SlotLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, SlotLocationKey));
	DistanceToTargetKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, DistanceToTargetKey));
	AttackRangeKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, AttackRangeKey));
}

void UVBBTService_UpdateCombat::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	// 선택자를 블랙보드 키 ID 로 해석한다. 빠지면 읽기/쓰기가 조용히 실패한다.
	if (const UBlackboardData* BlackboardAsset = GetBlackboardAsset())
	{
		TargetActorKey.ResolveSelectedKey(*BlackboardAsset);
		CombatRoleKey.ResolveSelectedKey(*BlackboardAsset);
		SlotLocationKey.ResolveSelectedKey(*BlackboardAsset);
		DistanceToTargetKey.ResolveSelectedKey(*BlackboardAsset);
		AttackRangeKey.ResolveSelectedKey(*BlackboardAsset);
	}
	else
	{
		TargetActorKey.InvalidateResolvedKey();
		CombatRoleKey.InvalidateResolvedKey();
		SlotLocationKey.InvalidateResolvedKey();
		DistanceToTargetKey.InvalidateResolvedKey();
		AttackRangeKey.InvalidateResolvedKey();
	}
}

void UVBBTService_UpdateCombat::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AAIController* AIController = OwnerComp.GetAIOwner();
	APawn* Pawn = AIController ? AIController->GetPawn() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!AIController || !Pawn || !Blackboard)
	{
		return;
	}

	// 컴포넌트는 캐시한다. 다른 폰을 빙의한 경우는 약참조로 안 잡히므로 소유자를 대조한다.
	FVBUpdateCombatMemory* Memory = CastInstanceNodeMemory<FVBUpdateCombatMemory>(NodeMemory);
	UVBEnemyCombatComponent* CombatComponent = Memory ? Memory->CombatComponent.Get() : nullptr;
	if (CombatComponent && CombatComponent->GetOwner() != Pawn)
	{
		CombatComponent = nullptr;
	}

	if (!CombatComponent)
	{
		CombatComponent = Pawn->FindComponentByClass<UVBEnemyCombatComponent>();
		if (Memory)
		{
			Memory->CombatComponent = CombatComponent;
		}
	}

	if (!CombatComponent)
	{
		return;
	}

	// 사망 게이트. BT 는 사망 후에도 잠시 돌므로 그룹과 포커스에서 시체를 뺀다.
	if (UVBAbilitySystemStatics::IsActorDead(Pawn))
	{
		CombatComponent->LeaveCombat();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		return;
	}

	AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName));

	// 블랙보드 값이 기준이며 멱등이므로 매 주기 전달해도 된다.
	CombatComponent->SetCombatTarget(Target);

	// 걸음. 대상이 없을 때도 불러야 걷는 상태로 돌아간다.
	VBEnemyGaitControl::Update(*CombatComponent);

	if (!Target)
	{
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		Blackboard->SetValueAsEnum(CombatRoleKey.SelectedKeyName, static_cast<uint8>(EVBCombatRole::Idle));
		return;
	}

	// 컨트롤러 포커스가 곧 폰의 회전 목표다(bUseControllerDesiredRotation). 대기 중에도 플레이어를 보게 한다.
	// 경직, 다운 중과 처형 창 동안(공격 중 제외)은 포커스를 끊어 캐릭터가 플레이어 쪽으로 돌지 않게 한다.
	const UAbilitySystemComponent* PawnASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	const bool bWindowOwnsFacing = UVBAbilitySystemStatics::IsFinisherWindowArmed(PawnASC) &&
		!UVBAbilitySystemStatics::IsAttacking(PawnASC);
	if (UVBAbilitySystemStatics::IsStaggered(PawnASC) || bWindowOwnsFacing)
	{
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
	}
	else
	{
		AIController->SetFocus(Target, EAIFocusPriority::Gameplay);
	}

	// 접근 정지 거리(계획된 공격의 시작 거리 또는 AttackCommitDistance). 폰이 바뀌어도 낡지 않게 매 주기 쓴다.
	//   계획이 바뀐 순간은 컴포넌트가 PushAttackStartDistance 로 직접 쓴다.
	Blackboard->SetValueAsFloat(AttackRangeKey.SelectedKeyName, CombatComponent->GetAttackStartDistance());

	Blackboard->SetValueAsEnum(CombatRoleKey.SelectedKeyName, static_cast<uint8>(CombatComponent->GetRole()));
	Blackboard->SetValueAsVector(SlotLocationKey.SelectedKeyName, CombatComponent->GetSlotLocation());
	// 수평 거리(역할 판정과 같은 기준). Float 키라 명시 축소 변환한다.
	Blackboard->SetValueAsFloat(DistanceToTargetKey.SelectedKeyName,
		static_cast<float>(FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation())));
}

uint16 UVBBTService_UpdateCombat::GetInstanceMemorySize() const
{
	return sizeof(FVBUpdateCombatMemory);
}

void UVBBTService_UpdateCombat::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	EBTMemoryInit::Type InitType) const
{
	// placement new 로 생성자를 명시적으로 돌린다(0 초기화에 기대지 않는다).
	new (NodeMemory) FVBUpdateCombatMemory();
}

void UVBBTService_UpdateCombat::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	EBTMemoryClear::Type CleanupType) const
{
	CastInstanceNodeMemory<FVBUpdateCombatMemory>(NodeMemory)->~FVBUpdateCombatMemory();
}

FString UVBBTService_UpdateCombat::GetStaticServiceDescription() const
{
	return FString::Printf(TEXT("Target: %s -> Role/Token/Slot"), *TargetActorKey.SelectedKeyName.ToString());
}
