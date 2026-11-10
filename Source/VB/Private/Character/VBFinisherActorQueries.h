// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UAnimInstance;
class USkeletalMeshComponent;
class IVBInteractionAnimInterface;

// 처형의 선택(FVBFinisherSelection)과 재생(UVBFinisherComponent)이 같이 쓰는 액터 조회.상태도 정책도 없다 -
//   같은 조회를 두 곳에 베끼지 않으려고 한 번만 둔다. 모듈 안에서만 쓴다(Private 헤더).
namespace VBFinisherActorQueries
{
	// 역할 이름. PSIA 자산의 Items[].Role 과 글자까지 같아야 한다 - 다르면 짝이 조용히 안 지어진다.
	extern const FName RoleAttacker;
	extern const FName RoleVictim;

	// 액터의 바디 메시. 못 찾으면 nullptr 이고 만들지 않는다. FindComponentByClass 는 처음 찾은 스켈레탈 메시를 돌려줘
	// 무기, 장신구 메시에 처형이 걸릴 수 있어, ACharacter 가 이름으로 가진 바디 메시를 쓴다.
	USkeletalMeshComponent* FindBodyMesh(const AActor* Actor);
	// 바디 메시의 AnimInstance. 못 찾으면 nullptr.
	UAnimInstance* FindAnimInstance(const AActor* Actor);
	// 상호작용 결과를 들 수 있는 AnimInstance. 플레이어와 적이 형제 클래스라 인터페이스로 묻는다.
	IVBInteractionAnimInterface* FindInteractionAnim(const AActor* Actor);
}
