// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PoseSearch/PoseSearchResult.h"
#include "Character/VBFinisherLegacySession.h"
#include "Character/VBFinisherParticipant.h"
#include "Character/VBFinisherSelection.h"
#include "Character/VBInteractionReplay.h"
#include "VBFinisherComponent.generated.h"

class UAnimMontage;
class UContextualAnimSceneActorComponent;
class UVBFinisherConfig;
class UMultiAnimAsset;
class UAnimInstance;
class UAnimSequenceBase;
class USkeletalMeshComponent;
class UPrimitiveComponent;

/** 처형 장면이 끝났다(성공, 이탈 모두 한 번). 플러그인 타입이 어빌리티로 새지 않게 우리 델리게이트로 옮긴다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVBFinisherEndedDelegate);

// 피해자 notify가 물리 전환 시점에 도달했다. 인자는 그 시점의 저작 루트 속도(월드 cm/s)다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBInteractionVictimLanded, FVector, LaunchVelocity);

/**
 * 처형 설정이 이 기계에 도착했다(끝나면 nullptr). 듣는 쪽이 어느 액터인지 몰라도 한 번만 구독하도록 static 이다.
 * PIE 는 월드가 여럿이라 듣는 쪽이 Attacker->GetWorld() 로 자기 월드인지 가른다.
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBFinisherConfigArrived, AActor* /*Attacker*/,
	const UVBFinisherConfig* /*Config*/);

DECLARE_MULTICAST_DELEGATE_OneParam(FVBInteractionParticipantChanged, AActor*);

// 처형 요청을 받아 서버에서 시작과 끝을 확정하고, 확정한 처형을 각 컴퓨터에서 재생하게 하는 컴포넌트다.
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBFinisherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBFinisherComponent();

	// 옛 장면 경로로 시작한다(서버 전용). 실패하면 false 이고 아무 상태도 안 남긴다.
	bool StartFinisher(const UVBFinisherConfig* Config, AActor* Target);
	// 모션매칭 경로로 시작한다. false 는 피해 없는 취소이고, 옛 경로로 가라는 뜻이 아니다.
	bool StartInteractionFinisher(AActor* Target);
	// 옛 장면의 다음 단계로 전환한다(프롬프트 성공 등). 장면이 안 돌고 있으면 false 다.
	bool TransitionSection(FName SectionName);
	// 중간 이탈. 두 액터 모두 멈춘다. 장면이 안 돌고 있으면 아무 일도 안 한다.
	void EarlyOut();

	// 이 액터가 처형 안에 있으면 true 다.
	UFUNCTION(BlueprintPure, Category="VB|Finisher")
	bool IsPlaying() const;

	// 진행 중인 처형의 설정. 없으면 nullptr 이다. 카메라와 어빌리티가 연출 값을 읽는다.
	UFUNCTION(BlueprintPure, Category="VB|Finisher")
	const UVBFinisherConfig* GetActiveConfig() const { return ActiveConfig; }

	// 처형 고르기와 성립 검사의 담당 객체. 어빌리티가 자기 공격을 끊기 전에 묻는다.
	const FVBFinisherSelection& GetSelection() const { return Selection; }
	// 역할의 참가 액터. 없으면 nullptr 이다.
	AActor* GetParticipant(FName Role) const;
	// 살아 있는 참가 액터 전부. 호출자는 경로를 구분하지 않는다.
	void GetParticipants(TArray<AActor*>& OutActors) const;
	// 공격자 재생 위치(초). 재생 중이 아니면 -1 이다.
	float GetPlaybackTime() const;
	// 마지막 처형이 피해 없이 취소됐으면 true 다.
	bool WasLastFinisherCancelled() const { return bLastFinisherCancelled; }

	// 그 액터가 어느 경로든 처형 안에 있으면 true 다. 피해자에는 이 컴포넌트가 없어서 정적이다.
	static bool IsActorInScene(const AActor* Actor);
	// 플러그인의 SceneActorComponent. 없으면 nullptr 이고 원격 도착 순서 때문에 여기서 만들지 않는다.
	static UContextualAnimSceneActorComponent* FindSceneActorComponent(const AActor* Actor);
	// 그 액터에 붙은 컴포넌트. 없으면 nullptr 이고 처형을 못 하는 캐릭터다.
	static UVBFinisherComponent* FindFor(const AActor* Actor);
	// 저작된 native notify 를 해당 참가자의 현재 세션에만 전달한다.
	static bool NotifyInteractionRagdoll(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, int32 MontageInstanceId);

	// 장면이 끝나면 발화한다(서버 전용). 어빌리티가 피해 적용과 종료를 한다.
	UPROPERTY(BlueprintAssignable, Category="VB|Finisher")
	FVBFinisherEndedDelegate OnFinisherEnded;

	// 피해자 notify 의 물리 전환 시점(서버, 세션당 한 번). 공격자 몽타주는 계속 재생한다.
	UPROPERTY(BlueprintAssignable, Category="VB|Finisher")
	FVBInteractionVictimLanded OnInteractionVictimLanded;

	// 설정이 도착했을 때 모든 컴퓨터에서 발화한다(클라 포함).
	static FVBFinisherConfigArrived OnConfigArrived;

	// 이 컴퓨터에서 참가자의 상호작용 재생 상태가 바뀌면 알린다.
	static FVBInteractionParticipantChanged OnInteractionParticipantChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 이 캐릭터가 쓸 수 있는 처형 DB 의 포즈 검색 색인을 미리 만들기 시작한다.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	// 옛 장면의 피해자 자리 맞춤을 한 틱 진행한다.
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 피해자를 래그돌로 놓을 때의 발사 속도 배율(notify 시점 루트 속도 x 이 값).
	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher", meta=(ClampMin="0.1", ClampMax="10.0"))
	float InteractionRagdollLaunchScale = 1.0f;

private:
	friend class FVBFinisherLifecycleTest;
	// 플러그인의 OnLeftSceneDelegate 를 받아 우리 델리게이트로 옮긴다. 그 사이에서 상태도 비운다.
	UFUNCTION()
	void HandleLeftScene(UContextualAnimSceneActorComponent* SceneActorComponent);

	// 상호작용 처형이 끝났다(공격자 몽타주 종료). 끝은 시작을 건 공격자 한 곳에서만 정한다.
	UFUNCTION()
	void HandleInteractionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation);

	// 전환 notify 없이 피해자 재생이 끝나면 피해 없이 세션을 취소한다.
	UFUNCTION()
	void HandleInteractionVictimMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation);

	// 공격자 또는 놓이기 전 피해자의 재생이 끊기면 세션을 취소한다(서버).
	UFUNCTION()
	void HandleInteractionBlendOut(UAnimMontage* Montage, bool bInterrupted, int32 Generation);

	// 참가자가 사라지면 세션을 닫는다. 클라의 relevancy 이탈이면 재현을 다시 시도한다.
	UFUNCTION()
	void HandleInteractionParticipantDestroyed(AActor* Actor);

	// 두 역할을 이 컴퓨터에서 재생한다. 실패하면 설치한 것을 되돌리고 false 다.
	bool PlayInteractionLocally(const TArray<FPoseSearchBlueprintResult>& Results, float ElapsedSeconds);
	struct FInteractionPlayPlan;
	// 결과 둘을 검사해 참가자별 준비물을 모은다. 하나라도 어긋나면 아무것도 설치하지 않고 false 다.
	bool BuildInteractionPlayPlan(const TArray<FPoseSearchBlueprintResult>& Results, float ElapsedSeconds,
		FInteractionPlayPlan& Plan) const;
	// 참가자, 골격, 재생 설정이 이 장면을 틀 수 있는지 본다. 실패하면 경고를 남긴다.
	bool CanPlayInteractionResult(const FPoseSearchBlueprintResult& Result, AActor* Actor, bool bForeignAbilitySystem,
		const UAnimInstance* Anim, const UAnimMontage* Montage, bool bActorAlreadyClaimed) const;
	// 참가자 기록, 소유권 등록, 결과 설치를 한 번에 한다.
	void InstallInteractionParticipants(const FInteractionPlayPlan& Plan);
	// 참가자의 몽타주 소유권을 가져온다. 실패하거나 도중에 세션이 바뀌면 false 다.
	bool AcquireParticipantMontages(const FInteractionPlayPlan& Plan, int32 StartGeneration);
	// 건너뛰지 않는 참가자의 몽타주를 튼다. 하나라도 못 틀면 세션을 버리고 false 다.
	bool PlayParticipantMontages(const FInteractionPlayPlan& Plan);
	// 참가자마다 몽타주 끝, 블렌드 아웃 델리게이트를 건다.
	void BindInteractionMontageDelegates(const FInteractionPlayPlan& Plan);
	// 장면이 끝나는 방식 - 완료, 취소는 OnFinisherEnded 를 알리고, 버림은 아무도 시작을 듣지 못한 세션이라 알리지 않는다.
	enum class EVBInteractionEnd : uint8 { Completed, Cancelled, Abandoned };
	// 모든 참가자를 되돌리고 세션을 닫는다. 서버는 끝을 복제하고 End 에 따라 알린다.
	void FinishInteraction(EVBInteractionEnd End);
	// 참가자 하나에 임시로 건 재생, 등록, 소유권, 틱 순서, 충돌 무시를 되돌린다.
	void TearDownParticipant(int32 Index, USkeletalMeshComponent* OwnerMesh, UPrimitiveComponent* OwnerRoot);
	// 참가자와 몽타주 인스턴스가 현재 세션과 맞을 때만 피해자를 놓는다.
	bool HandleRagdollNotify(AActor* Victim, UAnimSequenceBase* Animation, int32 MontageInstanceId);
	// 피해자를 놓는다(걸쇠, 몽타주 슬롯 해제, 서버 알림 한 번). 래그돌 notify 와 공격자 자연 종료가 모두 여기로 온다.
	void ReleaseVictim(int32 Index, UAnimInstance* Anim, UAnimMontage* Montage);
	// 피해자를 놓는 순간의 저작 루트 속도(월드 cm/s)에 배율을 곱해 돌려준다.
	FVector ComputeVictimLaunch(UAnimInstance* Anim, UAnimMontage* Montage) const;
	// 복제된 세션을 이 컴퓨터에서 재현한다. 참가자가 준비되지 않았으면 마감까지 재시도한다.
	void TryReplayInteraction();
	// 서버가 놓은 피해자를 이 컴퓨터에서도 놓는다. 피해는 재방송하지 않는다.
	void ApplyReplicatedVictimRelease();

	// 복제된 세션 상태를 이 컴퓨터의 재생에 반영한다.
	UFUNCTION()
	void OnRep_InteractionReplay();

	UPROPERTY(ReplicatedUsing=OnRep_InteractionReplay)
	FVBInteractionReplay InteractionReplay;

	// 이 컴퓨터가 InteractionReplay.SessionId 를 어디까지 따라갔는지. 참조가 아직 안 풀렸으면 짧게 재시도한다.
	FTimerHandle InteractionReplayRetryTimer;
	int32 ReplayRetrySession = 0; // 재시도 타이머가 기다리는 세션
	double ReplayRetryDeadline = 0.0;
	int32 LocalInteractionSession = 0; // 이 컴퓨터에서 지금 재생 중인 세션(0 = 없음)
	int32 FinishedInteractionSession = 0; // 이 컴퓨터가 마지막으로 끝낸 세션 - 늦게 온 복제가 그것을 다시 틀지 않게
	int32 LocalGeneration = 0; // 시작, 끝마다 올린다 - 지난 세션의 늦은 몽타주 콜백을 거른다
	// 이 컴퓨터에서 피해자가 이미 놓였나(걸쇠). 정본은 InteractionReplay.bVictimReleased 다.
	bool bInteractionVictimLanded = false;
	bool bFinishingInteraction = false;
	bool bLastFinisherCancelled = false;
	// 이 컴퓨터에서 재생 중인 참가자들. 비어 있으면 새 경로가 안 돌고 있다.
	TArray<FVBFinisherParticipant> InteractionParticipants;

	// GameThread에서만 접근한다. 종료/파괴 때 해제하며, notify마다 월드를 검색하지 않는다.
	static TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UVBFinisherComponent>> ParticipantOwners;

	// 조기 이탈에서 상호작용 몽타주를 끄는 시간(초). 0 이면 두 캐릭터가 튄다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Finisher")
	float InteractionEarlyOutBlendTime = 0.25f;

	// 도착 알림과 확인 로그만 낸다 - 여기서 아무것도 시작하지 않는다.
	UFUNCTION()
	void OnRep_ActiveConfig();

	// 이 컴퓨터에서 도는 처형의 설정 - 연출이 읽는 한 곳. 장면이 끝나면 비운다.
	//   고르기가 무작위라 클라가 스스로 못 메워서 복제한다.
	UPROPERTY(ReplicatedUsing=OnRep_ActiveConfig)
	TObjectPtr<const UVBFinisherConfig> ActiveConfig = nullptr;

	// 고르기, 성립 검사. 이 컴포넌트와 수명이 같다.
	FVBFinisherSelection Selection;

	// 옛 플러그인 장면과 피해자 자리 맞춤. 이 컴포넌트와 수명이 같다.
	FVBFinisherLegacySession LegacySession;
};
