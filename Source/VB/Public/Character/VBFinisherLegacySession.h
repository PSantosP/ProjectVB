// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class UActorComponent;
class UContextualAnimSceneActorComponent;
class UVBFinisherConfig;
struct FContextualAnimSceneBindings;

// 옛 ContextualAnimation 플러그인으로 처형 장면을 돌리고 피해자를 제자리로 데려온다(UVBFinisherComponent 가 값으로 든다).
class VB_API FVBFinisherLegacySession final
{
public:
	// 액터의 플러그인 컴포넌트. 없으면 nullptr 이고 여기서 만들지 않는다.
	static UContextualAnimSceneActorComponent* FindSceneComponent(const AActor* Actor);
	// 플러그인 기준으로 그 액터가 진행 중인 장면 안에 있으면 true 다.
	static bool IsActorInScene(const AActor* Actor);
	// 역할에 묶인 액터. 장면이 없으면 nullptr 이다.
	static AActor* GetParticipant(const AActor* Owner, FName Role);
	// 장면의 참가 액터를 OutActors 뒤에 붙인다.
	static void AppendParticipants(const AActor* Owner, TArray<AActor*>& OutActors);
	// 공격자 몽타주의 재생 위치(초). 장면이 없으면 -1 이다.
	static float GetPlaybackTime(const AActor* Owner);
	// 장면의 다음 구간으로 전환한다. 전환하지 못하면 false 다.
	static bool TransitionSection(const AActor* Owner, FName SectionName);
	// 두 액터를 모두 장면에서 뺀다. 정리와 발화는 플러그인의 장면 이탈 알림을 받는 쪽이 한다.
	static void EarlyOut(const AActor* Owner);

	// 장면을 시작하고 공격자 쪽 플러그인 컴포넌트를 돌려준다. 실패하면 nullptr 이다.
	// 두 액터는 SceneActorComponent 를 미리 들고 있어야 한다(BP_VBCharacter, AVBEnemyBase 가 붙인다).
	UContextualAnimSceneActorComponent* Start(const UVBFinisherConfig& Config, AActor& Owner, AActor& Target, UActorComponent& TickOwner);
	// 자리 맞춤 한 틱. 남은 어긋남이 없으면 TickOwner 의 틱을 끈다.
	void TickAlign(float DeltaTime, UActorComponent& TickOwner);

private:
	// 씬 자산이 정한 자리를 읽어 피해자 자리 맞춤을 시작한다. 역할을 못 찾으면 건너뛴다.
	void AlignVictimToScene(const UVBFinisherConfig& Config, const FContextualAnimSceneBindings& Bindings, AActor& Owner, AActor& Target,
		UActorComponent& TickOwner);
	// 피해자를 Desired 로 데려오기 시작한다. 시간이 0 이면 그 자리에서 다 민다.
	void BeginAlign(AActor& Victim, const FTransform& Desired, float BlendTime, UActorComponent& TickOwner);

	// 남은 보정량. 공격자의 틱이 소비한다.
	FVector AlignOffset = FVector::ZeroVector;
	float AlignYaw = 0.0f;
	float AlignTime = 0.0f;
	TWeakObjectPtr<AActor> AlignActor;
};
