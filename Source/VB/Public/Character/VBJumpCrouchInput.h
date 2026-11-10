// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/VBSlideJumpRequest.h"

class AVBCharacter;
class UVBAbilitySystemComponent;
class UVBGASPSmartObjectComponent;
class UVBHeroComponent;
class UVBInteractorComponent;
class UVBLocomotionIntentComponent;
class UVBPlayerRagdollComponent;
class UVBTraversalComponent;

// 점프와 앉기 키 이벤트를 어디로 보낼지 정하고, 점프 키 유지와 미끄럼 점프 요청을 가진다. 캐릭터가 값으로 가지고 게임 스레드에서만 쓴다.
class VB_API FVBJumpCrouchInput
{
public:
	// 키 이벤트를 처리하는 동안만 참조하는 캐릭터의 구성 요소. Body 외에는 없을 수 있다.
	struct FParts
	{
		AVBCharacter* Body = nullptr;
		UVBAbilitySystemComponent* AbilitySystem = nullptr;
		UVBInteractorComponent* Interactor = nullptr;
		UVBGASPSmartObjectComponent* SmartObject = nullptr;
		UVBPlayerRagdollComponent* PlayerRagdoll = nullptr;
		UVBTraversalComponent* Traversal = nullptr;
		UVBLocomotionIntentComponent* LocomotionIntent = nullptr;
		UVBHeroComponent* Hero = nullptr;
	};

	// 지상이면 넘기를 먼저 시도하고 아니면 점프, 공중이면 눌림 표시만 세우고, 미끄럼 중이면 미끄럼 점프 요청에 맡긴다.
	void JumpPressed(const FParts& Parts);
	// 눌림 표시를 내리고 점프를 멈춘다(미끄럼 점프 요청이 살아 있으면 그 끝까지 미룬다).
	void JumpReleased(AVBCharacter& Body);
	// 앉기 토글. 매달림, 등반, 사다리에서는 놓기, 질주 중이면 미끄럼, 몸통을 쓰는 동작 중에는 막는다(자세 관문).
	void CrouchPressed(const FParts& Parts);
	// 미끄럼을 끊고 누른 점프 요청을 한 Tick 진행하고 돌아온 행동을 캐릭터에 적용한다.
	void TickSlideJump(AVBCharacter& Body, bool bSceneAllowed);

	// 들고 있는 미끄럼 점프 요청이 없다.
	bool IsSlideJumpIdle() const { return SlideJumpRequest.IsIdle(); }
	// 미끄럼 점프 요청이 지금 장면에서 새 점프를 막는가.
	bool BlocksSlideJump(bool bSceneAllowed) const { return SlideJumpRequest.BlocksJump(bSceneAllowed); }
	// 점프 키가 눌려 있나(소유 클라 전용) - 공중 맨틀의 관문.
	bool IsJumpHeld() const { return bJumpInputHeld; }

private:
	// 점프 키가 눌려 있나.
	bool bJumpInputHeld = false;
	// 미끄럼을 끊고 누른 점프 하나 - 미끄럼은 다음 이동 갱신에서 일어서므로 그때까지 들고 있다가 이어 준다.
	FVBSlideJumpRequest SlideJumpRequest;
};
