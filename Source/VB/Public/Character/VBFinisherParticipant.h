// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UAnimMontage;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UVBAbilitySystemComponent;

// 이 컴퓨터에서 처형을 재생하는 참가자 하나가 임시로 가져온 것을 기록하고 같은 기록으로 되돌린다.
// 배열 번호는 이 컴퓨터의 참가자 순서이고, 복제된 결과의 RoleIndex 와 같다는 보장은 없다.
struct VB_API FVBFinisherParticipant
{
	// 공격자 메시가 피해자보다 늦게 틱하게 하고 서로의 이동 충돌을 무시한다. 이번에 새로 건 것만 표시한다.
	void LinkVictim(USkeletalMeshComponent* OwnerMesh, UPrimitiveComponent* OwnerRoot, AActor& Victim);
	// LinkVictim 이 새로 건 틱 순서와 충돌 무시만 푼다.
	void UnlinkVictim(USkeletalMeshComponent* OwnerMesh, UPrimitiveComponent* OwnerRoot, AActor& Victim) const;
	// 임시로 가져온 몽타주 소유권을 가져올 때의 ASC 에 되돌린다.
	void ReleaseMontageOwnership(UObject* Holder) const;

	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UAnimMontage> Montage;
	// 재생 동안 몽타주 소유권을 임시로 가져온 ASC. 액터에서 다시 찾지 않고 이 값으로 되돌린다. 피해자 쪽을 공격자보다 먼저 되돌린다.
	TWeakObjectPtr<UVBAbilitySystemComponent> AbilitySystem;
	// 재생에 성공한 인스턴스 번호만 든다. 0 은 실제 번호일 수 있어 없음은 INDEX_NONE 이다.
	int32 MontageInstanceId = INDEX_NONE;
	bool bAddedTickPrerequisite = false;
	bool bAddedOwnerCollisionIgnore = false;
	bool bAddedVictimCollisionIgnore = false;
};
