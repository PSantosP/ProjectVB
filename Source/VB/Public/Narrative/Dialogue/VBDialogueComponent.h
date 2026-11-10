// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBDialogueComponent.generated.h"

class UVBDialogueAsset;
struct FVBDialogueNode;
struct FVBRuleContext;

/** 소유 클라이언트가 그릴 현재 대사. 서버가 보내 준 것 그대로다 - 클라이언트가 계산하지 않는다. */
USTRUCT()
struct VB_API FVBDialogueView
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBDialogueAsset> Dialogue = nullptr;

	UPROPERTY()
	FName NodeId;

	// 선택지마다 고를 수 있나(노드의 Choices 와 같은 순서). 조건은 서버가 봤다.
	UPROPERTY()
	TArray<bool> ChoiceAvailable;

	bool IsActive() const { return Dialogue != nullptr; }
	const FVBDialogueNode* GetNode() const;
};

DECLARE_MULTICAST_DELEGATE(FVBOnDialogueViewChanged);

namespace VBDialogueCombatGate
{
	// 전투 중 대화 거절의 유일한 판정. 대화가 있고, 전투 중 허용이 꺼져 있고, 플레이어를 노리는 적이 있으면 거절한다.
	VB_API bool ShouldRefuse(const UVBDialogueAsset* Dialogue, bool bPlayerEngaged);
}

/**
 * 대화 진행을 관리한다(서버). AVBPlayerController 에 붙고, 소유 클라이언트는 ClientShowNode 로 그리고 버튼은 요청만 보낸다.
 * 요청에 노드 이름을 같이 보내 중복 클릭이 다음 노드에 적용되지 않게 한다. 화면은 모르고 OnViewChanged 만 알린다.
 * 게임을 멈추지 않으며, 적이 노리고 있으면(UVBCombatCoordinatorSubsystem::IsEngaged) 열지 않는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBDialogueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBDialogueComponent();

	// 폰, 컨트롤러 어느 쪽을 줘도 그 플레이어의 대화 컴포넌트. 없으면 nullptr.
	static UVBDialogueComponent* FindFor(const AActor* Actor);

	// 서버에서만. 진입점이 하나도 참이 아니거나, 이미 대화 중이거나, 전투 중(IsBlockedByCombat)이면 false. Speaker 는 말을 건 대상(사라지면 대화가 끝난다).
	bool StartDialogue(const UVBDialogueAsset* Dialogue, AActor* Speaker);

	// 서버에서만 뜻이 있다. 이 컨트롤러의 폰을 적이 노리고 있어서 이 대화를 열 수 없나. 화자가 거절 알림을 낼지 고를 때도 이것을 묻는다.
	bool IsBlockedByCombat(const UVBDialogueAsset* Dialogue) const;

	// 서버에서만. 대화 중이 아니면 아무것도 안 한다.
	void EndDialogue();

	bool IsInDialogue() const { return ServerDialogue != nullptr; }

	// 표시가 부른다. 판단은 서버가 다시 한다.
	void RequestChoose(int32 ChoiceIndex);
	void RequestContinue();
	void RequestLeave();

	// 소유 클라이언트에서 그릴 데이터. 대화 중이 아니면 IsActive() 가 거짓이다.
	const FVBDialogueView& GetView() const { return View; }

	// 소유 클라이언트에서 View 가 바뀌었을 때(새 노드, 끝). PC 는 이것으로 화면을 여닫고, 화면은 이것으로 다시 그린다.
	FVBOnDialogueViewChanged OnViewChanged;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(Server, Reliable)
	void ServerChoose(FName NodeId, int32 ChoiceIndex);

	UFUNCTION(Server, Reliable)
	void ServerContinue(FName NodeId);

	UFUNCTION(Server, Reliable)
	void ServerLeave();

	UFUNCTION(Client, Reliable)
	void ClientShowNode(const UVBDialogueAsset* Dialogue, FName NodeId, const TArray<bool>& ChoiceAvailable);

	UFUNCTION(Client, Reliable)
	void ClientEndDialogue();

	// 서버: 노드에 진입한다 - 효과 적용, 사건 방송, 선택지 판정, 소유 클라이언트에 보내기. 없는 노드면 끝낸다.
	void EnterNode(FName NodeId);

	// 서버: 요청이 지금 노드에 대한 것이고 말 건 대상이 아직 있나. 아니면 버린다(대상이 사라졌으면 끝낸다).
	const FVBDialogueNode* ValidateRequest(FName NodeId, const TCHAR* What);

	FVBRuleContext MakeRuleContext() const;

	UPROPERTY()
	TObjectPtr<const UVBDialogueAsset> ServerDialogue;

	FName ServerNodeId;
	TWeakObjectPtr<AActor> ServerSpeaker;

	// 소유 클라이언트가 그릴 데이터. 리슨 서버 호스트에서는 서버 상태와 한 객체에 같이 있다 - 그래서 둘을 따로 둔다.
	UPROPERTY()
	FVBDialogueView View;
};
